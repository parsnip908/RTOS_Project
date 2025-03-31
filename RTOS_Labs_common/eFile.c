// filename ************** eFile.c *****************************
// High-level routines to implement a solid-state disk 
// Students implement these functions in Lab 4
// Jonathan W. Valvano 1/12/20
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/eFile.h"

#define BLOCK_SIZE 512
#define FAKE_BLOCK_SIZE 8192

#define FAT_EOC 0xFFFF
/** Maximum filename length (including the NULL character) */
#define FS_FILENAME_LEN 16



struct __attribute__((__packed__)) superblock
{
    char signature[8];
    uint16_t num_blocks;
    uint16_t root_dir_index;
    uint16_t data_block_index;
    uint16_t num_data_blocks;
    uint8_t num_FAT_blocks;
};

struct __attribute__((__packed__)) file
{
    char filename[FS_FILENAME_LEN];
    uint32_t size;
    uint16_t first_block;
    uint8_t unused_padding[10];
};

struct __attribute__((__packed__)) open_file // packed ensured sequential bytes
{
    char filename[FS_FILENAME_LEN];
    uint32_t size;
    uint16_t first_block;
    uint16_t offset;
    // uint16_t curr_block;
    uint8_t root_dir_id;
    uint8_t rw;
};

/** Maximum number of files in the root directory */
// #define FS_FILE_MAX_COUNT (BLOCK_SIZE/sizeof(struct file))
#define FS_FILE_MAX_COUNT 16

/** Maximum number of open files */
#define FS_OPEN_MAX_COUNT 1

struct superblock _superblock;

struct superblock* superblock = &_superblock;
struct file root_dir[FS_FILE_MAX_COUNT];
uint16_t file_count; // num files in root directory
struct open_file open_file_table[FS_OPEN_MAX_COUNT];

int curr_FAT_index;
uint16_t curr_FAT_block[FAKE_BLOCK_SIZE/2];

uint8_t bounce_buf[FAKE_BLOCK_SIZE];
int data_buf_index = -1;
// bool data_buf_valid;

typedef enum {
    eFile_OFF = 0,
    eFile_INIT,
    eFile_MOUNTED
} status;
status curr_status = 0;

int get_FAT(uint16_t index)
{
    if(index == 0 || index > superblock->num_data_blocks)
        return -1;

    int req_block = index / (BLOCK_SIZE/2);
    if(req_block >= superblock->num_FAT_blocks)
        return -1;

    if(req_block != curr_FAT_index) 
    {
        eDisk_WriteBlock((uint8_t*) curr_FAT_block, curr_FAT_index+1);
        eDisk_ReadBlock((uint8_t*) curr_FAT_block, req_block+1);
        curr_FAT_index = req_block;
    }

    return 0;
}

uint16_t read_FAT(uint16_t index)
{
    if(get_FAT(index))
        return FAT_EOC;
    return curr_FAT_block[index % (BLOCK_SIZE/2)];
}

uint16_t write_FAT(uint16_t index, uint16_t new_val)
{
    if(get_FAT(index))
        return FAT_EOC;
    uint16_t old_val = curr_FAT_block[index % (BLOCK_SIZE/2)];
    curr_FAT_block[index % (BLOCK_SIZE/2)] = new_val;
    return old_val;
}

int get_data_block(uint16_t index)
{
    if(index >= superblock->num_data_blocks)
        return -1;
    eDisk_ReadBlock(bounce_buf, index + superblock->data_block_index);
    return 0;
}

int write_data_block(uint16_t index)
{
    if(index >= superblock->num_data_blocks)
        return -1;
    eDisk_WriteBlock(bounce_buf, index + superblock->data_block_index);
    return 0;
}

uint16_t alloc_data_block()
{
    uint16_t free_data_block = 1;
    for(; free_data_block < superblock->num_data_blocks; free_data_block++)
    {
        if(read_FAT(free_data_block) == 0)
            break;
    }
    if(free_data_block == superblock->num_data_blocks)
    {
        // printf("Disk full\n");
        return FAT_EOC;
    }
    write_FAT(free_data_block, FAT_EOC);
    return free_data_block;
}


//---------- eFile_Init-----------------
// Activate the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure (already initialized)
int eFile_Init(void) // initialize file system
{
    if(curr_status) return -1;

    int error = eDisk_Init(0);
    if(error) return error;

    curr_status = eFile_INIT;
    return 0;
}

//---------- eFile_Format-----------------
// Erase all files, create blank directory, initialize free space manager
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Format(void) // erase disk, add format
{
    if(curr_status != eFile_INIT)
        return -1;
    memcpy(superblock, "RTOS_FS_", 8);

    int sector_count, sector_size, block_size;
    disk_ioctl(0, GET_SECTOR_COUNT, &sector_count);
    disk_ioctl(0, GET_SECTOR_SIZE, &sector_size);
    disk_ioctl(0, GET_BLOCK_SIZE, &block_size);

    printf("format: %d %d %d", sector_count, sector_size, block_size);

    superblock->num_blocks = sector_count > 0xFF00 ? 0xFF00 : sector_count;
    // superblock->num_FAT_blocks = ceil(superblock->num_data_blocks*2 / (float) BLOCK_SIZE);

    superblock->num_FAT_blocks = 0;
    for(int i = 0; i<10; i++)
        superblock->num_FAT_blocks = (((superblock->num_blocks - superblock->num_FAT_blocks - 2)*2-1) / BLOCK_SIZE) + 1;

    superblock->num_data_blocks = superblock->num_blocks - superblock->num_FAT_blocks - 2;
    superblock->root_dir_index = superblock->num_FAT_blocks + 1;
    superblock->data_block_index = superblock->num_FAT_blocks + 2;

    eDisk_ReadBlock(bounce_buf, 0);
    memcpy(bounce_buf, superblock, sizeof(struct superblock));
    memset(bounce_buf + sizeof(struct superblock), 0 , BLOCK_SIZE - sizeof(struct superblock));
    eDisk_WriteBlock(bounce_buf, 0);

    memset(root_dir, 0, sizeof(root_dir));
    memset(bounce_buf, 0, sizeof(bounce_buf));
    eDisk_WriteBlock(bounce_buf, superblock->root_dir_index);


    for(int i = 0; i < superblock->num_data_blocks; i++)
        write_FAT(i, 0);

    return 0;
}

//---------- eFile_Mount-----------------
// Mount the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure
int eFile_Mount(void) // initialize file system
{
    if(curr_status != eFile_INIT)
        return -1;

    if (eDisk_ReadBlock(bounce_buf, 0))
        return -1;

    // superblock = realloc(buf, sizeof(struct superblock));
    memcpy(superblock, bounce_buf, sizeof(struct superblock));

    if( memcmp(superblock, "RTOS_FS_", 8) ||
        // superblock->num_blocks != block_disk_count() || 
        ceil(superblock->num_data_blocks*2 / (float) BLOCK_SIZE) != superblock->num_FAT_blocks ||
        superblock->num_FAT_blocks + superblock->num_data_blocks + 2 != superblock->num_blocks ||
        superblock->num_FAT_blocks + 1 != superblock->root_dir_index ||
        superblock->num_FAT_blocks + 2 != superblock->data_block_index )
    {
        // block_disk_close();
        // free(superblock);
        // superblock = NULL;
        return -1;
    }

    eDisk_ReadBlock((uint8_t*) bounce_buf, superblock->root_dir_index);
    memcpy(root_dir, bounce_buf, sizeof(root_dir));
    file_count = 0;
    for(; file_count < FS_FILE_MAX_COUNT; file_count++)
    {
        if(root_dir[file_count].filename[0] == 0)
            break;
    }

    eDisk_ReadBlock((uint8_t*) curr_FAT_block, 1);
    curr_FAT_index = 0;

    if(curr_FAT_block[0] != FAT_EOC)
    {
        if(curr_FAT_block[0] == 0)
            curr_FAT_block[0] = FAT_EOC;
        else
        {
            // error: wdym the root dir is bigger than one block??
            // block_disk_close();
            // free(superblock);
            // superblock = NULL;
            return -1;
        }
    }

    curr_status = eFile_MOUNTED;
    return 0;
}


//---------- eFile_Create-----------------
// Create a new, empty file with one allocated block
// Input: file name is an ASCII string up to seven characters 
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Create( const char filename[])  // create new file, make it empty 
{
    if (//superblock == NULL || 
        file_count == FS_FILE_MAX_COUNT || 
        *filename == '\0' || 
        strlen(filename) >= FS_FILENAME_LEN) 
    {
        // printf("bruh why you even tryin\n");
        return -1;
    }
    for (int i = 0; i < file_count; i++) 
    {
        if (!strcmp(root_dir[i].filename, filename))
        {
            // printf("dup file\n");
            return -1;
        }
    } 
    strcpy(root_dir[file_count].filename, filename);
    root_dir[file_count].size = 0;
    root_dir[file_count].first_block = FAT_EOC;
    file_count++;
    // printf("file count is now %d\n", file_count);
    
    return eDisk_WriteBlock((uint8_t*) root_dir, superblock->root_dir_index);
}


//---------- eFile_WOpen-----------------
// Open the file, read into RAM last block
// Input: file name is an ASCII string up to seven characters
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WOpen( const char filename[])      // open a file for writing 
{
    if (//superblock == NULL || 
        // file_count == FS_OPEN_MAX_COUNT || 
        *filename == '\0' || 
        strlen(filename) >= FS_FILENAME_LEN) 
    {
        // printf("failed here\n");
        return -1;
    }
    int file_id = -1;
    for (int i = 0; i < file_count; i++) 
    {
        if (!strcmp(root_dir[i].filename, filename)) 
        {
            file_id = i;
            break;
        }
    } 
    if (file_id == -1)
    {
        // printf("no file named '%s'\n", filename);
        // printf("root dir 0 filename: %s\n", root_dir[0].filename);
        return -1;
    }

    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) 
    {
        if (!open_file_table[i].filename[0]) // entry is empty/available (first byte is zero)
        { 
            // insert file into open_file_table
            strcpy(open_file_table[i].filename, filename);
            open_file_table[i].size = root_dir[file_id].size;
            open_file_table[i].first_block = root_dir[file_id].first_block;
            open_file_table[i].offset = root_dir[file_id].size;
            // open_file_table[i].curr_block = open_file_table[i].first_block;
            open_file_table[i].root_dir_id = file_id;
            open_file_table[i].rw = 2;
            return i; // assign and return an fd for the opened file
        }
    }
    // printf("failed bruh\n");
    return -1; // open_file_table is full
}

//---------- eFile_Write-----------------
// save at end of the open file
// Input: data to be saved
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Write( const char data)
{
    // if (superblock == NULL || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !open_file_table[fd].filename[0] || buf == NULL) // if fd's entry in open_file_table is available, then the fd is not "open"
    //     return -1;
    if(!(open_file_table[0].rw & 2))
        return -1;
    // if(count == 0) return 0;

    const int fd = 0;
    // size_t count = 1;
    // int8_t* bounce_buf = malloc(BLOCK_SIZE);



    unsigned int block_to_write = open_file_table[fd].offset / BLOCK_SIZE;
    unsigned int offset_in_block = open_file_table[fd].offset % BLOCK_SIZE;
    uint16_t data_block_index = open_file_table[fd].first_block;
    // uint16_t next_data_block = open_file_table[fd].first_block;
    if(open_file_table[fd].first_block == FAT_EOC)
    {
        uint16_t free_data_block = alloc_data_block();
        if(free_data_block == FAT_EOC) 
            return -1;

        open_file_table[fd].first_block = free_data_block;
        root_dir[open_file_table[fd].root_dir_id].first_block = free_data_block;
        data_block_index = free_data_block;
        data_buf_index = data_block_index;
        memset(bounce_buf, 0, sizeof(bounce_buf));
    }
    else if(open_file_table[fd].size % BLOCK_SIZE == 0)
    {
        int curr_num_blocks = (((int) open_file_table[fd].size)-1) / BLOCK_SIZE + 1;
        // printf("Write: %d blocks owned, %d needed\n", curr_num_blocks, exp_num_blocks);
        for(int i = 1; i < curr_num_blocks; i++)
            data_block_index = read_FAT(data_block_index);

        uint16_t free_data_block = alloc_data_block();
        if(free_data_block == FAT_EOC) 
        {
            return -1;
        }
        write_FAT(data_block_index, free_data_block);
        data_block_index = free_data_block;
        write_data_block(data_buf_index);
        get_data_block(data_block_index);
        data_buf_index = data_block_index;
        root_dir[open_file_table[fd].root_dir_id].size = open_file_table[fd].size;
    }
    else
    {
        // data_block_index = open_file_table[fd].first_block;
        for (unsigned int i = 0; i < block_to_write; i++)
            data_block_index = read_FAT(data_block_index);
    }

    bounce_buf[offset_in_block] = (uint8_t) data;

    // update the size of file
    open_file_table[fd].size++;
    // root_dir[open_file_table[fd].root_dir_id].size++;
    
    // for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) //check if file is open twice
    // {
    //     if(open_file_table[fd].root_dir_id == open_file_table[i].root_dir_id && i != fd)
    //     {
    //         open_file_table[i].size = open_file_table[fd].size;
    //         open_file_table[i].first_block = open_file_table[fd].first_block;
    //     }
    // }

    open_file_table[fd].offset ++;
    return 0;
}

//---------- eFile_WClose-----------------
// close the file, left disk in a state power can be removed
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WClose(void){ // close the file for writing
    // if (superblock == NULL || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !open_file_table[fd].filename[0]) // if fd's entry in open_file_table is available, then the fd is not "open"
    //     return -1;
    root_dir[open_file_table[0].root_dir_id].size = open_file_table[0].size;
    memset(&open_file_table[0], 0, sizeof(struct open_file)); // clear fd's entry in the open_file_table
    
    return write_data_block(data_buf_index) || eDisk_WriteBlock((uint8_t*) root_dir, superblock->root_dir_index);
}


//---------- eFile_ROpen-----------------
// Open the file, read first block into RAM 
// Input: file name is an ASCII string up to seven characters
// Output: 0 if successful and 1 on failure (e.g., trouble read to flash)
int eFile_ROpen( const char filename[]){      // open a file for reading 
    if (//superblock == NULL || 
        // file_count == FS_OPEN_MAX_COUNT || 
        *filename == '\0' || 
        strlen(filename) >= FS_FILENAME_LEN) 
    {
        // printf("failed here\n");
        return -1;
    }
    int file_id = -1;
    for (int i = 0; i < file_count; i++) 
    {
        if (!strcmp(root_dir[i].filename, filename)) 
        {
            file_id = i;
            break;
        }
    } 
    if (file_id == -1)
    {
        // printf("no file named '%s'\n", filename);
        // printf("root dir 0 filename: %s\n", root_dir[0].filename);
        return -1;
    }

    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) 
    {
        if (!open_file_table[i].filename[0]) // entry is empty/available (first byte is zero)
        { 
            // insert file into open_file_table
            strcpy(open_file_table[i].filename, filename);
            open_file_table[i].size = root_dir[file_id].size;
            open_file_table[i].first_block = root_dir[file_id].first_block;
            open_file_table[i].offset = 0;
            // open_file_table[i].curr_block = open_file_table[i].first_block;
            open_file_table[i].root_dir_id = file_id;
            open_file_table[i].rw = 1;
            return i; // assign and return an fd for the opened file
        }
    }
    // printf("failed bruh\n");
    return -1; // open_file_table is full
}
 
//---------- eFile_ReadNext-----------------
// retreive data from open file
// Input: none
// Output: return by reference data
//         0 if successful and 1 on failure (e.g., end of file)
int eFile_ReadNext( char *pt){       // get next byte 
    // if (superblock == NULL || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !open_file_table[fd].filename[0] || buf == NULL) // if fd's entry in open_file_table is available, then the fd is not "open"
    //     return -1;
    const int fd = 0;
    if(!(open_file_table[fd].rw & 1))
        return -1;
    
    if(open_file_table[fd].offset >= open_file_table[fd].size)
        return -1;


    unsigned int block_to_read = open_file_table[fd].offset / BLOCK_SIZE;
    unsigned int offset_in_block = open_file_table[fd].offset % BLOCK_SIZE;
    uint16_t data_block_index = open_file_table[fd].first_block;


    for (unsigned int i = 0; i < block_to_read; i++)
        data_block_index = read_FAT(data_block_index);

    if(data_buf_index != data_block_index)
    {
        get_data_block(data_block_index);
        data_buf_index = data_block_index;
    }
    *pt = bounce_buf[offset_in_block];

    open_file_table[fd].offset++;
    return 0;
}
        
//---------- eFile_RClose-----------------
// close the reading file
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_RClose(void) // close the file for writing
{
    // if (superblock == NULL || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !open_file_table[fd].filename[0]) // if fd's entry in open_file_table is available, then the fd is not "open"
    //     return -1;
        
    memset(&open_file_table[0], 0, sizeof(struct open_file)); // clear fd's entry in the open_file_table
    return eDisk_WriteBlock((uint8_t*) root_dir, superblock->root_dir_index);
}


//---------- eFile_Delete-----------------
// delete this file
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Delete( const char filename[])  // remove this file 
{
    if (//superblock == NULL ||
        *filename == '\0' ||
        strlen(filename) >= FS_FILENAME_LEN) 
    {
        // printf("bruh why you even tryin\n");
        return -1;
    }
    int file_id = -1;
    for (int i = 0; i < file_count; i++) 
    {
        if (!strcmp(root_dir[i].filename, filename)) 
        {
            file_id = i;
            break;
        }
    } 
    if (file_id == -1)
    {
        // printf("bruh404 file not found\n");
        return -1;
    }
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) // if filename is inside of open_file_table, then it is open
    {
        if (open_file_table[i].filename[0] != 0 && open_file_table[i].root_dir_id == file_id)
        {
            // printf("bruh im still working leave me alone.\n");
            return -1;
        }
    }
    // perform delete by moving last file into deleted file's position
    // Use first_data_block to get first location in FAT
    uint16_t next_data_block = root_dir[file_id].first_block;

    while (next_data_block != FAT_EOC)
        next_data_block = write_FAT(next_data_block, 0);

    file_count--;
    if(file_id != file_count)
    {
        memcpy(&root_dir[file_id], &root_dir[file_count], sizeof(struct file));
        for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
        {
            if (open_file_table[i].root_dir_id == file_count)
                open_file_table[i].root_dir_id = file_id; // last file that was moved is open, so update open file table
        }
    }
    memset(&root_dir[file_count], 0, sizeof(struct file));

    // printf("file count is now %d\n", file_count);
    eDisk_WriteBlock((uint8_t*) root_dir, superblock->root_dir_index);
    return 0;
}                             


static int dir_entry_num = 0;
//---------- eFile_DOpen-----------------
// Open a (sub)directory, read into RAM
// Input: directory name is an ASCII string up to seven characters
//        (empty/NULL for root directory)
// Output: 0 if successful and 1 on failure (e.g., trouble reading from flash)
int eFile_DOpen( const char filename[]) // open directory
{
    if(filename == NULL || filename[0] == '\0')
    {
        dir_entry_num = 0;
        return 0;
    }
    else
        return 1;   // replace
}
    
//---------- eFile_DirNext-----------------
// Retreive directory entry from open directory
// Input: none
// Output: return file name and size by reference
//         0 if successful and 1 on failure (e.g., end of directory)
int eFile_DirNext( char *filename[], unsigned long *size)  // get next entry 
{
    if (dir_entry_num >= file_count)
        return 1;   // replace
    // memcpy(filename, root_dir[dir_entry_num].filename, FS_FILENAME_LEN);
    *filename = root_dir[dir_entry_num].filename;
    *size = root_dir[dir_entry_num].size;
    dir_entry_num++;
    return 0;
}

//---------- eFile_DClose-----------------
// Close the directory
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_DClose(void) // close the directory
{    
    return 0;   // replace
}


//---------- eFile_Unmount-----------------
// Unmount and deactivate the file system
// Input: none
// Output: 0 if successful and 1 on failure (not currently mounted)
int eFile_Unmount(void){ 
    if(curr_status != eFile_MOUNTED || open_file_table[0].filename[0] != 0)
        return -1;

    eDisk_WriteBlock((uint8_t*) curr_FAT_block, curr_FAT_index+1);
    eDisk_WriteBlock((uint8_t*) root_dir, superblock->root_dir_index);

    curr_status = eFile_INIT;
    return 0;
}
