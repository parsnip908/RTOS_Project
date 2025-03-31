// filename ************** eFile.c *****************************
// High-level routines to implement a solid-state disk 
// Students implement these functions in Lab 4
// Jonathan W. Valvano 1/12/20

//LAB 5 USES ITS OWN EFILE, THIS ONE WILL NOT BE USED

#include <stdint.h>
#include <string.h>
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/eDisk.h"
#include "../RTOS_Labs_common/eFile.h"
#include <stdio.h>

#define NUM_SECTORS 2048
#define MAX_LENGTH_NAME 7
#define MAX_FILES 10
#define INIT_SECTORS 10 //How many sectors initialization doe takes up
#define BUFFER_SIZE 10 //Number of sectors in RAM
#define SECTOR_SIZE 512 //Size of sector in bytes

//File definition
//File should be assigned one sector when it is created if empty
typedef struct File File; 
struct File {
	uint8_t filled; //If there is actually a file here (0 = false, 1 = true)
  char name[MAX_LENGTH_NAME+1]; //Add 1 for null pointer?
	int start_sector;
	uint32_t size;
};

//Provides info on each sector (If it is filled, and what does each part link to)
struct sector {
	uint8_t filled; //1 is filled, 0 is empty
	int link; //Next sector number to link to (Use indexes since interfaces with eDisk use indexes) -1 means no link
};

struct FileSystem {
	//Directory (At least 10 files). This is a list of files, one level
	struct File directory[MAX_FILES];

	//File allocation table
	struct sector FAT[NUM_SECTORS];
};

//Global file system
struct FileSystem fileSys;

//Global RAM variable, list of sectors
uint8_t RAMBuffer[BUFFER_SIZE][SECTOR_SIZE]; //eDisk should read into this, use uint8_t since each represents a byte

//Global variable for what file is open
int openFileIndex = -1;
typedef enum {NONE, R, W} mode;
mode openMode = NONE;
int readIndex = 0; //What character is currently being read

//Global variable for reading dir
int dirOpen = 0;
int dirIndex = 0;

//Find free sector helper
int Find_Sector(){
	//Find and set start sector for file
	int sectorIndex = -1;
	for(int i = 0; i < NUM_SECTORS; i++){
		if(fileSys.FAT[i].filled == 0){
			sectorIndex = i;
			break;
		}
	}
	return sectorIndex;
}

//Find file helper (returns the index of file in list)
int Find_File(const char name[]){
	int fileIndex = -1;
	for(int i = 0; i < MAX_FILES; i++){
		//Compare strings for name match
		if(strcmp(name, fileSys.directory[i].name) == 0){
			fileIndex = i;
			break;
		}
	}
	return fileIndex;
}

//---------- eFile_Init-----------------
// Activate the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure (already initialized)
int eFile_Init(void){ // initialize file system
	long crit = StartCriticalTime();
 //Check disk status, make sure SD card is plugged in
	DSTATUS status = eDisk_Status(0); //Param is physical drive number (which is 0). Identify multiple cards if needed
	
	//If successful, initialize the disk
	if(status == 0){
		//Init stuff, might have to add other stuff
		EndCriticalTime(crit);
		return eDisk_Init(0); //Return initialization status. Now you should be able to use any of the disk functions to access disk
	} else {
		EndCriticalTime(crit);
		return 1; //Fail
	}
	EndCriticalTime(crit);
  return 1; //Fail
}

//---------- eFile_Format-----------------
// Erase all files, create blank directory, initialize free space manager
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Format(void){ // erase disk, add format
 	/*
	Clear the disk, setup the directory, set up any other data structures
	File allocation table is stored on sector as well. Directory with all the file names and stuff will be stored on the disk
	2 Parts: 
		Directory and Table, provides information on where (sector) each file is stored at
		Each file themselves ("x" sectors assigned to each file)
	
	Only one level directory needed
	If SD card is plugged into a computer, the computer will ask to reformat, which can delete the custom filesys from TM4C
	*/
	long crit = StartCriticalTime();
	
	//Clear local filesys data structure. Don't need to clear blocks to SD card. Just clear filesys and the stuff there will be replaced on subsequent writes	
	//Close file
	openFileIndex = -1;
	openMode = NONE;
	
	//Set everything empty
	for(int i = 0; i < NUM_SECTORS; i++){
		//Clear sectors
		fileSys.FAT[i].filled = 0;
		fileSys.FAT[i].link = 0;
	}
	for(int i = 0; i < MAX_FILES; i++){
		//Clear files
		fileSys.directory[i].filled = 0;
	}
		
	//Fill up sectors needed for init
	for(int i = 0; i < INIT_SECTORS; i++){
		//Set sector filled
		fileSys.FAT[i].filled = 1;
		
		//Link the sectors
		if(i < INIT_SECTORS - 1){
			fileSys.FAT[i].link = i + 1;
		}
	}
	fileSys.FAT[INIT_SECTORS -1].link = -1; //Link last sector to -1
		
	//Store data about file system (FAT and Directory) into first 10 sectors on disk (Metadata)
	DRESULT result = eDisk_Write(0, (uint8_t*) &fileSys, 0, INIT_SECTORS);
	if(result != 0){EndCriticalTime(crit); return 1;}
	
	//Clear RAM
	for(int i = 0; i < BUFFER_SIZE; i++){
		for(int j = 0; j < SECTOR_SIZE; j++){
			RAMBuffer[i][j] = NULL;
		}
	}
	
	EndCriticalTime(crit);
  return 0;   
}

//---------- eFile_Mount-----------------
// Mount the file system, without formating
// Input: none
// Output: 0 if successful and 1 on failure
int eFile_Mount(void){ // initialize file system
	//Return file system structs from disk, read into local variable
	long crit = StartCriticalTime();
	DRESULT res =  eDisk_Read(0, (uint8_t*) &fileSys, 0, INIT_SECTORS);
	EndCriticalTime(crit);
	return res;
}


//---------- eFile_Create-----------------
// Create a new, empty file with one allocated block
// Input: file name is an ASCII string up to seven characters 
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Create( const char name[]){  // create new file, make it empty 
	long crit = StartCriticalTime();
	
	//Find empty file index
	int fileIndex = -1;
	for(int i = 0; i < MAX_FILES; i++){
		if(fileSys.directory[i].filled == 0){
			fileIndex = i;
			break;
		}
	}
	if(fileIndex == -1){EndCriticalTime(crit); return 1;} //File create fail
	fileSys.directory[fileIndex].filled = 1; //Fill the file slot in directory list
	
	//Set file name, assume it is the right size
	strcpy(fileSys.directory[fileIndex].name, name);
	
	//Find and set start sector for file
	int sectorIndex = Find_Sector();
	if(sectorIndex == -1){EndCriticalTime(crit); return 1;} //Find sector fail
	fileSys.directory[fileIndex].start_sector = sectorIndex; //Set start sector
	fileSys.FAT[sectorIndex].filled = 1; //Fill sector
	fileSys.FAT[sectorIndex].link = -1; //Set empty link
	
	//Set size
	fileSys.directory[fileIndex].size = 0;
	
	//Nothing to write to SD card for empty file, just update file system metadata
	DRESULT result = eDisk_Write(0, (uint8_t*) &fileSys, 0, INIT_SECTORS);
	if(result != 0){EndCriticalTime(crit); return 1;}
	
	EndCriticalTime(crit);
  return 0; //Create success
}


//---------- eFile_WOpen-----------------
// Open the file, read into RAM last block
// Input: file name is an ASCII string up to seven characters
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WOpen( const char name[]){      // open a file for writing 
	long crit = StartCriticalTime();
	
	//Check if file is already open
	if(openFileIndex != -1){EndCriticalTime(crit); return 1;}
	
	//Look for file
	int fileIndex = Find_File(name);
	if(fileIndex == -1){EndCriticalTime(crit); return 1;} //File create fail
	if(fileSys.directory[fileIndex].size > BUFFER_SIZE * SECTOR_SIZE){EndCriticalTime(crit); return 1;} //File too big to read
	openFileIndex= fileIndex; //Open file
	openMode = W;
	
	//Read contents of open file into RAM
	int ramSector = 0; //Sector in RAM to read to
	int readSector = fileSys.directory[fileIndex].start_sector; //Sector in disk to read from, put into RAM
	while(readSector != -1){ //Read through linked list of sectors until a null sector is reached
		DRESULT result = eDisk_ReadBlock(RAMBuffer[ramSector], readSector); //Read file sector into RAM sector
		if(result != 0){EndCriticalTime(crit); return 1;}
		readSector = fileSys.FAT[readSector].link; //Read the next sector linked to
		ramSector++; //Index to next RAM sector
	}
	
	EndCriticalTime(crit);
  return 0; //Successful open
}

//---------- eFile_Write-----------------
// save at end of the open file
// Input: data to be saved
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Write( const char data){
	//Only write one char at a time, write to RAM then save to disk (maybe can save here, maybe can just save when closed). Plan: Write to disk here
		long crit = StartCriticalTime();
	
		//Check if file is open for write
		if(openFileIndex == -1 || openMode != W){EndCriticalTime(crit); return 1;} //File not open
		if(fileSys.directory[openFileIndex].size + 1 > BUFFER_SIZE * SECTOR_SIZE){EndCriticalTime(crit); return 1;} //File exceeds max RAM
		
		//Calculate location to write to
		int ramSectorIndex = 0; //What sector in RAM to write to
		int fileSectorIndex = fileSys.directory[openFileIndex].start_sector; //What sector in file to write to (To write to disk)
		int writeLocation = fileSys.directory[openFileIndex].size; //Where in current RAM sector to write to
		while(writeLocation > SECTOR_SIZE){ //Index thru sectors as needed
			ramSectorIndex++; //Next sector in RAM
			fileSectorIndex = fileSys.FAT[fileSectorIndex].link; //Next sector in filesys
			writeLocation = writeLocation - SECTOR_SIZE;
		}
		
		//Determine if new sector is needed
		if(writeLocation == SECTOR_SIZE){
			//New sector needed, write to the first location of a new sector
			
			//Disk write, save new disk sector
			int newSectorIndex = Find_Sector();
			if(newSectorIndex == -1){EndCriticalTime(crit); return 1;} //New sector cannot be found
			fileSys.FAT[newSectorIndex].filled = 1; //Fill new sector
			fileSys.FAT[fileSectorIndex].link = newSectorIndex; //Link prev sector to new sector
			fileSys.FAT[newSectorIndex].link = -1; //Link new sector to -1
			
			//Create new sector, write onto disk
			uint8_t newSector[SECTOR_SIZE];
			for(int i = 0; i < SECTOR_SIZE; i++){newSector[i] = NULL;}
			newSector[0] = data;
			DRESULT result = eDisk_WriteBlock(newSector, newSectorIndex);
			if(result != 0){EndCriticalTime(crit); return 1;}
			
			//RAM write
			RAMBuffer[ramSectorIndex + 1][0] = data;
			fileSys.directory[openFileIndex].size++; //Increment file size
			
		} else {
			//Write to current sector
			writeLocation++;
			fileSys.directory[openFileIndex].size++; //Increment file size
			
			//RAM write
			RAMBuffer[ramSectorIndex][writeLocation] = data;
			
			//Disk write, save last sector of RAM to current sector of disk
			DRESULT result = eDisk_WriteBlock(RAMBuffer[ramSectorIndex], fileSectorIndex);
			if(result != 0){EndCriticalTime(crit); return 1;}
		}
		
		//Update metadata
		DRESULT resultMeta = eDisk_Write(0, (uint8_t*) &fileSys, 0, INIT_SECTORS);
		if(resultMeta != 0){EndCriticalTime(crit); return 1;}
		
		EndCriticalTime(crit);
    return 0; //Successful write
}

//---------- eFile_WClose-----------------
// close the file, left disk in a state power can be removed
// Input: none
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_WClose(void){ // close the file for writing
	long crit = StartCriticalTime();
	
	//Close file
  openFileIndex = -1;
	openMode = NONE;
	
	//Clear RAM
	for(int i = 0; i < BUFFER_SIZE; i++){
		for(int j = 0; j < SECTOR_SIZE; j++){
			RAMBuffer[i][j] = 0;
		}
	}
	EndCriticalTime(crit);
  return 0; 
}


//---------- eFile_ROpen-----------------
// Open the file, read first block into RAM 
// Input: file name is an ASCII string up to seven characters
// Output: 0 if successful and 1 on failure (e.g., trouble read to flash)
int eFile_ROpen( const char name[]){      // open a file for reading 
	long crit = StartCriticalTime();
	
	//Check if file is already open
	if(openFileIndex != -1){EndCriticalTime(crit); return 1;}
	
	//Look for file
	int fileIndex = Find_File(name);
	if(fileIndex == -1){EndCriticalTime(crit); return 1;} //File create fail
	if(fileSys.directory[fileIndex].size > BUFFER_SIZE * SECTOR_SIZE){EndCriticalTime(crit); return 1;} //File too big to read
	openFileIndex= fileIndex; //Open file
	openMode = R;
	
	//Read contents of open file into RAM
	int ramSector = 0; //Sector in RAM to read to
	int readSector = fileSys.directory[fileIndex].start_sector; //Sector in disk to read from, put into RAM
	while(readSector != -1){ //Read through linked list of sectors until a empty sector is reached
		DRESULT result = eDisk_ReadBlock(RAMBuffer[ramSector], readSector); //Read file sector into RAM sector
		if(result != 0){EndCriticalTime(crit); return 1;}
		readSector = fileSys.FAT[readSector].link; //Read the next sector linked to
		ramSector++; //Index to next RAM sector
	}
	
	//Set reading index
	readIndex = 0;
	EndCriticalTime(crit);
  return 0; //Successful read
}
 
//---------- eFile_ReadNext-----------------
// retreive data from open file
// Input: none
// Output: return by reference data
//         0 if successful and 1 on failure (e.g., end of file)
int eFile_ReadNext( char *pt){       // get next byte 
	long crit = StartCriticalTime();
	
  //Check for overflow
	if(readIndex + 1 > BUFFER_SIZE * SECTOR_SIZE){EndCriticalTime(crit); return 1;}
	
	//Check file open
	if(openFileIndex == -1 || openMode != R){EndCriticalTime(crit); return 1;}
	
	//Read from RAM
	int readSectorIndex = readIndex; //What index within sector to read 
	int readSector = 0; //What sector to read
	while(readSectorIndex > SECTOR_SIZE){
		readSector++;
		readSectorIndex = readSectorIndex - SECTOR_SIZE;
	}
	
	*pt = RAMBuffer[readSector][readSectorIndex]; //Read to pointer
	readIndex++;
	EndCriticalTime(crit);
  return 0; //Read success
}
    
//---------- eFile_RClose-----------------
// close the reading file
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_RClose(void){ // close the file for writing
	long crit = StartCriticalTime();
	
  //Close file
  openFileIndex = -1;
	openMode = NONE;
	readIndex = 0;
	
	//Clear RAM
	for(int i = 0; i < BUFFER_SIZE; i++){
		for(int j = 0; j < SECTOR_SIZE; j++){
			RAMBuffer[i][j] = 0;
		}
	}
	EndCriticalTime(crit);
  return 0;
}


//---------- eFile_Delete-----------------
// delete this file
// Input: file name is a single ASCII letter
// Output: 0 if successful and 1 on failure (e.g., trouble writing to flash)
int eFile_Delete( const char name[]){  // remove this file 
	long crit = StartCriticalTime();
	
	//Look for file
	int fileIndex = Find_File(name);
	if(fileIndex == -1){EndCriticalTime(crit); return 1;} //File find fail

	//Remove from filesys (Future file create will override other fields)
	fileSys.directory[fileIndex].filled = 0;
	
	//Find and remove sectors
	int sectorIndex = fileSys.directory[fileIndex].start_sector;
	while(sectorIndex != -1){
		int curIndex = sectorIndex;
		sectorIndex = fileSys.FAT[sectorIndex].link; //Next sector
		fileSys.FAT[curIndex].filled = 0; //Remove current index
		fileSys.FAT[curIndex].link = -1;	//Remove current index link
	}
	
	//Update metadata
	DRESULT result = eDisk_Write(0, (uint8_t*) &fileSys, 0, INIT_SECTORS);
	if(result != 0){EndCriticalTime(crit); return 1;}
	
	EndCriticalTime(crit);
  return 0;	//Delete success
}                             


//---------- eFile_DOpen-----------------
// Open a (sub)directory, read into RAM
// Input: directory name is an ASCII string up to seven characters
//        (empty/NULL for root directory)
// Output: 0 if successful and 1 on failure (e.g., trouble reading from flash)
int eFile_DOpen( const char name[]){ // open directory
	long crit = StartCriticalTime();
  dirOpen = 1;
	dirIndex = 0;
	EndCriticalTime(crit);
  return 0;   // replace
}
  
//---------- eFile_DirNext-----------------
// Retreive directory entry from open directory
// Input: none
// Output: return file name and size by reference
//         0 if successful and 1 on failure (e.g., end of directory)
int eFile_DirNext( char *name[], unsigned long *size){  // get next entry 
  long crit = StartCriticalTime();
	//Check if dir open or exceeds index
	if(dirOpen == 0 || dirIndex > MAX_FILES){EndCriticalTime(crit); return 1;}
	
	//Find next open file in dir
	while(fileSys.directory[dirIndex].filled == 0){
		//Next file, check index
		dirIndex++;
		if(dirIndex > MAX_FILES){EndCriticalTime(crit); return 1;}
	}
	
	//File found
	*name = (char*) fileSys.directory[dirIndex].name;
	*size = fileSys.directory[dirIndex].size;
	
	//Increment index
	dirIndex++;
	
	EndCriticalTime(crit);
  return 0;   // replace
}

//---------- eFile_DClose-----------------
// Close the directory
// Input: none
// Output: 0 if successful and 1 on failure (e.g., wasn't open)
int eFile_DClose(void){ // close the directory
  long crit = StartCriticalTime();
	dirOpen = 0;
	dirIndex = 0;
	EndCriticalTime(crit);
  return 0;   // replace
}


//---------- eFile_Unmount-----------------
// Unmount and deactivate the file system
// Input: none
// Output: 0 if successful and 1 on failure (not currently mounted)
int eFile_Unmount(void){ 
   //This is used to update init sectors on SD card, but you can choose to either do this continuously or just at the end
	long crit = StartCriticalTime();
	DRESULT res = eDisk_Write(0, (uint8_t*) &fileSys, 0, INIT_SECTORS); 
	EndCriticalTime(crit);
	return res;
}
