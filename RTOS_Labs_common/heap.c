// filename *************************heap.c ************************
// Implements memory heap for dynamic memory allocation.
// Follows standard malloc/calloc/realloc/free interface
// for allocating/unallocating memory.

// Jacob Egner 2008-07-31
// modified 8/31/08 Jonathan Valvano for style
// modified 12/16/11 Jonathan Valvano for 32-bit machine
// modified August 10, 2014 for C99 syntax

/* This example accompanies the book
   "Embedded Systems: Real Time Operating Systems for ARM Cortex M Microcontrollers",
   ISBN: 978-1466468863, Jonathan Valvano, copyright (c) 2015

 Copyright 2015 by Jonathan W. Valvano, valvano@mail.utexas.edu
    You may use, edit, run or distribute this file
    as long as the above copyright notice remains

 THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 VALVANO SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL,
 OR CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 For more information about my classes, my research, and my books, see
 http://users.ece.utexas.edu/~valvano/
 */


#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../inc/CortexM.h"
#include "../RTOS_Labs_common/heap.h"


#define HEAP_SIZE 4096

typedef struct
{
    int16_t prev_size; // Size of the previous block
    int16_t next_size; // Size of the next block
} BlockHeader;


uint8_t heap[HEAP_SIZE];
void* heap_start = ((void*)heap);
void* heap_end = ((void*)(heap+HEAP_SIZE));

//******** Heap_Init *************** 
// Initialize the Heap
// input: none
// output: always 0
// notes: Initializes/resets the heap to a clean state where no memory
//  is allocated.
int32_t Heap_Init(void)
{
    BlockHeader* initial_block = (BlockHeader*)heap_start;
    initial_block->prev_size = 0;
    initial_block->next_size = (int) sizeof(BlockHeader) - HEAP_SIZE;
    return 0;
}


//******** Heap_Malloc *************** 
// Allocate memory, data not initialized
// input: 
//   desiredBytes: desired number of bytes to allocate
// output: void* pointing to the allocated memory or will return NULL
//   if there isn't sufficient space to satisfy allocation request
void* Heap_Malloc(int32_t desiredBytes)
{
    if (desiredBytes <= 0) return NULL;
    
    if (desiredBytes % 4)
        desiredBytes += 4 - desiredBytes % 4;

    long sr = StartCritical();

    BlockHeader* current = (BlockHeader*)heap_start;
    while ((uint8_t*)current < heap + HEAP_SIZE)
    {
        if (current->next_size < 0 && -current->next_size >= desiredBytes)
        {
            int remaining = -current->next_size - desiredBytes - sizeof(BlockHeader);

            if (remaining > (int) sizeof(BlockHeader))
            {
                current->next_size = desiredBytes;
                BlockHeader* next = (BlockHeader*)((uint8_t*)current + sizeof(BlockHeader) + desiredBytes);
                next->prev_size = desiredBytes;
                next->next_size = -remaining;
            }
            else
            {
                current->next_size *= -1;
                BlockHeader* next = (BlockHeader*)((uint8_t*)current + sizeof(BlockHeader) + current->next_size);
                if((uint8_t*)next < heap + HEAP_SIZE)
                    next->prev_size = current->next_size;
            }
            EndCritical(sr);
            return (uint8_t*)current + sizeof(BlockHeader);
        }
        current = (BlockHeader*)((uint8_t*)current + abs(current->next_size) + sizeof(BlockHeader));
    }
    EndCritical(sr);
    return NULL;
}


//******** Heap_Calloc *************** 
// Allocate memory, data are initialized to 0
// input:
//   desiredBytes: desired number of bytes to allocate
// output: void* pointing to the allocated memory block or will return NULL
//   if there isn't sufficient space to satisfy allocation request
//notes: the allocated memory block will be zeroed out
void* Heap_Calloc(int32_t desiredBytes)
{  
    void* ptr = Heap_Malloc(desiredBytes);
    if (ptr) memset(ptr, 0, desiredBytes);
    return ptr;
}


//******** Heap_Realloc *************** 
// Reallocate buffer to a new size
//input: 
//  oldBlock: pointer to a block
//  desiredBytes: a desired number of bytes for a new block
// output: void* pointing to the new block or will return NULL
//   if there is any reason the reallocation can't be completed
// notes: the given block may be unallocated and its contents
//   are copied to a new block if growing/shrinking not possible
void* Heap_Realloc(void* oldBlock, int32_t desiredBytes)
{
    if (oldBlock == NULL)
        return Heap_Malloc(desiredBytes);

    BlockHeader* block = (BlockHeader*)((uint8_t*)oldBlock - sizeof(BlockHeader));
    int32_t currentSize = block->next_size;
    
    if (currentSize >= desiredBytes)
        return oldBlock;
    
    void* newBlock = Heap_Malloc(desiredBytes);
    if (newBlock)
    {
        memcpy(newBlock, oldBlock, currentSize);
        Heap_Free(oldBlock);
    }
    return newBlock;
}


//******** Heap_Free *************** 
// return a block to the heap
// input: pointer to memory to unallocate
// output: 0 if everything is ok, non-zero in case of error (e.g. invalid pointer
//     or trying to unallocate memory that has already been unallocated
int32_t Heap_Free(void* pointer)
{
    if (pointer == NULL || pointer < heap_start || pointer > heap_end)
        return -1;

    long sr = StartCritical();

    BlockHeader* block = (BlockHeader*)((uint8_t*)pointer - sizeof(BlockHeader));
    block->next_size = -abs(block->next_size);
    
    // Merge with the next block if it is free
    BlockHeader* next = (BlockHeader*)((uint8_t*)block + sizeof(BlockHeader) + abs(block->next_size));
    if ((uint8_t*)next < heap + HEAP_SIZE && next->next_size < 0) {
        block->next_size += next->next_size - sizeof(BlockHeader);
    }
    
    // Merge with the previous block if it is free
    if(block->prev_size != 0)
    {
        BlockHeader* prev = (BlockHeader*)((uint8_t*)block - block->prev_size - sizeof(BlockHeader));
        if ((uint8_t*)prev >= heap && prev->next_size < 0) {
            prev->next_size += block->next_size - sizeof(BlockHeader);
        }
    }
    
    EndCritical(sr);
    return 0;

}


//******** Heap_Stats *************** 
// return the current status of the heap
// input: reference to a heap_stats_t that returns the current usage of the heap
// output: 0 in case of success, non-zeror in case of error (e.g. corrupted heap)
int32_t Heap_Stats(heap_stats_t *stats)
{
    if (stats == NULL)
        return -1;
    
    stats->size = HEAP_SIZE;
    stats->free = 0;
    stats->used = 0;
    
    BlockHeader* current = (BlockHeader*)heap_start;
    while ((uint8_t*)current < heap + HEAP_SIZE)
    {
        if (current->next_size < 0) {
            stats->free -= current->next_size;
        } else {
            stats->used += current->next_size;
        }
        current = (BlockHeader*)((uint8_t*)current + abs(current->next_size) + sizeof(BlockHeader));
    }
    return 0;
}
