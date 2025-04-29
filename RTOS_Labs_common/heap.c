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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../inc/CortexM.h"
#include "../RTOS_Labs_common/heap.h"


#define HEAP_SIZE 4096

//Conversion from bytes to words (round up)
int32_t BYTES_TO_WORDS(int32_t bytes){
	return (bytes + 3) / 4;
}

//Global vars
int32_t GlobalHeap[HEAP_SIZE]; //Should this be unsigned or signed?

//******** Heap_Init *************** 
// Initialize the Heap
// input: none
// output: always 0
// notes: Initializes/resets the heap to a clean state where no memory
//  is allocated.
int32_t Heap_Init(void)
{
    //Go thru entire heap, make whole thing a free slot
	for(int i = 0; i < HEAP_SIZE; i++){
		GlobalHeap[i] = 0;
	}
	GlobalHeap[0] = -1 * HEAP_SIZE;
	GlobalHeap[HEAP_SIZE - 1] = -1 * HEAP_SIZE;
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
   //Edge case: invalid size
	if(desiredBytes <= 0){return 0;}
	printf("malloc %d \n", desiredBytes);
	
	//Size calculation
	int32_t curPos = 0; //Value: Current index of search, should point to the start header of a block Init: 0, start header of first block
	int32_t desiredWords = BYTES_TO_WORDS(desiredBytes); //Convert desiredBytes to words
	int32_t desiredSpace = desiredWords + 2; //How many words to allocate including headers
	
	//Search free block
	while(curPos < HEAP_SIZE){
		//Test if block is free and valid (negative num, absolute val larger than size)
		if(GlobalHeap[curPos] < 0 && -1 * GlobalHeap[curPos] >= desiredSpace){ //Find free block: Header value of block is negative, Header value * -1 larger than desiredSpace
			// printf("%d\n", curPos);
			//Record szie, start and end location of free block
			int32_t sizeFreeBlock = GlobalHeap[curPos] * -1; //Total size of free block as a positive number
			int32_t startFreeBlock = curPos; //Starting index of free block
			int32_t endFreeBlock = curPos + sizeFreeBlock - 1; //Ending index of free block
			
			//Allocate new block
			int32_t startTakenBlock = startFreeBlock; //Start header of taken block
			int32_t endTakenBlock = startFreeBlock + desiredSpace - 1; //End header of taken block
			GlobalHeap[startTakenBlock] = desiredSpace; //Store amount of space taken
			GlobalHeap[endTakenBlock] = desiredSpace;
			
			//Set headers for rest of memory space in allocated block
			int32_t remainingSize = sizeFreeBlock - desiredSpace; //Remaining size left in the free block
			if(remainingSize >= 3) {  
					//If remaining size is big enough for a new free block, allocate the remaining free block
			    GlobalHeap[endTakenBlock + 1] = remainingSize * -1; //Start free block
			    GlobalHeap[endFreeBlock] = remainingSize * -1; //End free block
			} else {
			    //If the remaining free block is too small, allocate the entire original free block 
			    GlobalHeap[startTakenBlock] = sizeFreeBlock;
			    GlobalHeap[endFreeBlock] = sizeFreeBlock;
			}
			
			//Return pointer to allocated space 
			return (void*)&GlobalHeap[curPos + 1];
		}
		
		//find next block
		curPos = curPos + abs((int32_t) GlobalHeap[curPos]);
	}
 
  return NULL;   // NULL
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
	//If malloc successful, initialize data
	if(ptr != NULL){
		// //Use a word ptr (int32) instead of a void ptr (void is a generic pointer, compiler doesn't know how big the data is)
		// int32_t* wordPtr = (int32_t*)ptr;
        
		// //Calculate num words
		// int32_t numWords = BYTES_TO_WORDS(desiredBytes);
		
		// //Clear words
		// for(int i = 0; i < numWords; i++){
		// 	wordPtr[i] = 0;
		// }
		// return ptr;
		memset(ptr, 0, BYTES_TO_WORDS(desiredBytes));
	}
	return ptr;   // NULL
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
    //Edge cases: NULL and size == 0
	if(oldBlock == NULL){return Heap_Malloc(desiredBytes);} //Malloc if old block null
	if(desiredBytes == 0){Heap_Free(oldBlock); return NULL;}
	
	//Old block data
	int32_t* oldBlockStartHeader = (int32_t*)oldBlock - 1; //Start header of old block
	int32_t oldSizeWords = *oldBlockStartHeader; //Size in words (with headers)
	int32_t newSizeWords = BYTES_TO_WORDS(desiredBytes) + 2; //New size, with headers
	int32_t* oldBlockEndHeader = oldBlockStartHeader + *oldBlockStartHeader - 1; //End header of the old block
	
	//Edge case: Block is freed
	if(*oldBlockStartHeader <= 0) {return NULL;}
	
	//If size equal
	if(oldSizeWords == newSizeWords){return oldBlock;}
	
	//Shrink data
	if(newSizeWords < oldSizeWords){
		//Reduce size of current block
		*oldBlockStartHeader = newSizeWords; //Set old block start header
		*(oldBlockStartHeader + newSizeWords - 1) = newSizeWords; //Set old block end header (new location)
		
		//Make the rest of the chunk free, try to merge with below
		int32_t* freeBlockStartHeader = oldBlockStartHeader + newSizeWords; //One after old block end header
		*freeBlockStartHeader = -(oldSizeWords - newSizeWords);
		int32_t* freeBlockEndHeader = freeBlockStartHeader + *freeBlockStartHeader * -1 - 1;
		*freeBlockEndHeader = *freeBlockStartHeader;
		
		//Try to merge with block below
		int32_t* nextBlockStartHeader = freeBlockEndHeader + 1;
		if(*nextBlockStartHeader < 0){
			//Merge if the next block is also free
			int32_t* nextBlockEndHeader = nextBlockStartHeader + (*nextBlockStartHeader * -1) - 1;
			
			//Combine blocks by setting start and end headers
			int32_t combinedSizeNegative = *freeBlockStartHeader + *nextBlockStartHeader;
			*freeBlockStartHeader = combinedSizeNegative;
			*nextBlockEndHeader = combinedSizeNegative;
		}
		
		//Return pointer
		return oldBlock;
	}
	
	//Grow data in place
	int32_t* nextBlockStartHeader = oldBlockEndHeader + 1;
	int32_t freeBlockSize = *nextBlockStartHeader * -1; //Should be positive if block free, negative if block taken (words)
	//Determine the next block is free and there is enough size to grow down
	if(freeBlockSize > 0 && oldSizeWords + freeBlockSize >= newSizeWords){
		//End header of next block (provided it is free)
		int32_t* nextBlockEndHeader = nextBlockStartHeader + (*nextBlockStartHeader * -1) - 1;
		int32_t nextBlockFreeSize = oldSizeWords + freeBlockSize - newSizeWords; //Size of the free block after reduction (words, positive) newSizeWords > oldSizeWords
		
		//Grow current block down
		*oldBlockStartHeader = newSizeWords;
		*(oldBlockStartHeader + newSizeWords - 1) = newSizeWords; //Set new old block end header (new location)
				
		//If enough space remains, reduce the free block, or else, allocate everything
		if(nextBlockFreeSize >= 3) {
			//Reduce new free block size
			*(oldBlockStartHeader + newSizeWords) = nextBlockFreeSize * -1; //Start header of free block
			*nextBlockEndHeader = nextBlockFreeSize * -1; //End header of free block
		} else {
			//Not enough space left for a free block, use all space for allocated block
			*oldBlockStartHeader = oldSizeWords + freeBlockSize; //Allocated block start 
			*nextBlockEndHeader = oldSizeWords + freeBlockSize; //Allocated block end
		}
		
		//Return pointer
		return oldBlock;
	}
	
	//Try to malloc
	void* newBlock = Heap_Malloc(desiredBytes);
	
	//Copy data if malloc success
	if(newBlock != NULL){
		int32_t* source = (int32_t*) oldBlock;
		int32_t* destination = (int32_t*) newBlock;
		
		//Copy data via loop
		for(int i = 0; i < oldSizeWords - 2; i++){ //Remove 2, don't wanna copy headers
			destination[i] = source[i];
		}
		
		//Return
		Heap_Free(oldBlock);
		return newBlock;
	}
	
  return 0;   // NULL
}


//******** Heap_Free *************** 
// return a block to the heap
// input: pointer to memory to unallocate
// output: 0 if everything is ok, non-zero in case of error (e.g. invalid pointer
//     or trying to unallocate memory that has already been unallocated
int32_t Heap_Free(void* pointer)
{
   //Edge case: null
	if(pointer == NULL){return 1;}
		
	//Get block data
	int32_t* blockStartHeader = (int32_t*)pointer - 1; //Start Header
	int32_t* blockEndHeader = blockStartHeader + *blockStartHeader - 1; //End header
	int32_t blockSizeWord = *blockStartHeader; //Size in words (including headers), should be positive (not freed)
	
	//Check if freed
	if(blockSizeWord <= 0){return 1;}
	
	//Mark block free
	*blockStartHeader = -1 * *blockStartHeader;
	*blockEndHeader = -1 * *blockEndHeader;
	
	//Try merge with above block
	int32_t* aboveBlockEndHeader = blockStartHeader - 1;
	if(*aboveBlockEndHeader < 0){
		//Merge possible, get headers of above block
		int32_t* aboveBlockStartHeader = aboveBlockEndHeader + *aboveBlockEndHeader + 1; //Find start header
		blockSizeWord = blockSizeWord - *aboveBlockEndHeader; //Add to block size (should be positive)
		
		//Combine into new block
		blockStartHeader = aboveBlockStartHeader;
		*blockStartHeader = -1 * blockSizeWord;
		*blockEndHeader = -1 * blockSizeWord;
	}
	
	//Try to merge with below block
	int32_t* belowBlockStartHeader = blockEndHeader + 1;
	if(*belowBlockStartHeader < 0){
		//Merge possible, get headers of below block
		int32_t* belowBlockEndHeader = belowBlockStartHeader - *belowBlockStartHeader - 1; //Find end header
		blockSizeWord = blockSizeWord - *belowBlockStartHeader; //Add to block size (should be positive)
		
		//Combine into new block
		blockEndHeader = belowBlockEndHeader;
		*blockStartHeader = -1 * blockSizeWord;
		*blockEndHeader = -1 * blockSizeWord;
	}
 
  return 0;   
}


//******** Heap_Stats *************** 
// return the current status of the heap
// input: reference to a heap_stats_t that returns the current usage of the heap
// output: 0 in case of success, non-zeror in case of error (e.g. corrupted heap)
int32_t Heap_Stats(heap_stats_t *stats)
{
    //Edge case: Null ptr
	if(stats == NULL){return 1;}
	
	//Size calculation
	int32_t curPos = 0; //Value: Current index of search, should point to the start header of a block Init: 0, start header of first block
	
	//Set initial values
	stats->size = HEAP_SIZE * 4; //Stores size as bytes (Each heap index is a word, so x4)
  stats->used = 0;
  stats->free = 0;
	
	//While still within heap size
	while(curPos < HEAP_SIZE){
		//Get size of the current block in words (each word is index of heap)
		int32_t blockSizeWords = abs(GlobalHeap[curPos]);
		
		//Test if block is free
		if(GlobalHeap[curPos] > 0){ //Allocated
			stats->used = stats->used + blockSizeWords * 4 -8; //Convert to byte (x4)
		}else{ //Free
			stats->free = stats->free + blockSizeWords * 4 - 8; //For every free block, the headers (2 words) are not free 
		}
		
		//Index search
		curPos = curPos + blockSizeWords;
	}
	
  return 0;   
}


// Helper function: Round up to next power of 2
static uint32_t round_up_pow2(uint32_t x) {
    if (x == 0) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
    return x;
}

// Main aligned allocation function for MPU
void* Heap_MallocAlignedPow2(int32_t desiredBytes){
    if(desiredBytes <= 0) return NULL;
    if(desiredBytes < 32) desiredBytes = 32;
    
    uint32_t regionSizeBytes = round_up_pow2((uint32_t)desiredBytes);
    uint32_t alignmentBytes = regionSizeBytes;
    
    printf("malloc aligned %d %u \n", desiredBytes, regionSizeBytes);
    int32_t curPos = 0;
    int32_t desiredWords = BYTES_TO_WORDS(regionSizeBytes); // User data size in words
    int32_t desiredSpace = desiredWords + 2; // +2 for header/footer

    while(curPos < HEAP_SIZE){
        if(GlobalHeap[curPos] < 0 && -1 * GlobalHeap[curPos] >= desiredSpace){
            int32_t sizeFreeBlock = -GlobalHeap[curPos];
            uintptr_t startAddress = (uintptr_t)(&GlobalHeap[curPos + 1]);
            uintptr_t alignedAddress = (startAddress + (alignmentBytes - 1)) & ~(alignmentBytes - 1);
            int32_t alignmentPaddingWords = BYTES_TO_WORDS(alignedAddress - startAddress);

            // prevent a padding block that is too small.
            if(alignmentPaddingWords > 0 && alignmentPaddingWords <= 5)
            {
            	alignedAddress += alignmentBytes;
            	alignmentPaddingWords += BYTES_TO_WORDS(alignmentBytes);
            }

            // check if necessary padding and allocation fit into free block
            if(alignmentPaddingWords >= 0 && alignmentPaddingWords + desiredSpace <= sizeFreeBlock){
                // Create a padding block if needed
                if(alignmentPaddingWords > 0){
                    GlobalHeap[curPos] = -alignmentPaddingWords;
                    GlobalHeap[curPos + alignmentPaddingWords - 1] = -alignmentPaddingWords;
                    curPos += alignmentPaddingWords;
                    sizeFreeBlock -= alignmentPaddingWords;
                }

                // Allocate the aligned block
                GlobalHeap[curPos] = desiredSpace;
                GlobalHeap[curPos + desiredSpace - 1] = desiredSpace;

                int32_t remainingSize = sizeFreeBlock - desiredSpace;
                if(remainingSize >= 3){
                    GlobalHeap[curPos + desiredSpace] = -remainingSize;
                    GlobalHeap[curPos + sizeFreeBlock - 1] = -remainingSize;
                } else {
                    // Not enough space for a free block, allocate the whole chunk
                    GlobalHeap[curPos] = sizeFreeBlock;
                    GlobalHeap[curPos + sizeFreeBlock - 1] = sizeFreeBlock;
                }

                return (void*)(&GlobalHeap[curPos + 1]);
            }
        }

        // Move to next block
        curPos = curPos + abs(GlobalHeap[curPos]);
    }

    return NULL; // Failed
}


int32_t Heap_GetAlloc(void* pointer)
{
	if(pointer == NULL){return 0;}
		
	//Get block data
	int32_t* blockStartHeader = (int32_t*)pointer - 1; //Start Header
	int32_t blockSizeWord = *blockStartHeader; //Size in words (including headers), should be positive (not freed)

	return (blockSizeWord-2) * 4;
}