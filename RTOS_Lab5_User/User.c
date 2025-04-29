// User.c
// Runs on LM4F120/TM4C123
// Standalone user-level process example

// #include <stdio.h>
#include <stdint.h>
// #include "inc/hw_types.h"
// #include "inc/tm4c123gh6pm.h"

#include "OS.h"
#include "Display.h"

#define PF2     (*((volatile uint32_t *)0x40025010))
#define PF3     (*((volatile uint32_t *)0x40025020))


#define TEST 0

#if (TEST == 0)

unsigned int line = 0;

void thread(void)
{
  unsigned int id;
	
  id = OS_Id();
  PF3 ^= 0x08;
  Display_Message(0,line++, "Thread: ", id);
  PF3 ^= 0x08;
  OS_Sleep(2000);
  PF3 ^= 0x08;
  Display_Message(0,line++, "Dying: ", id);
  PF3 ^= 0x08;
  OS_Kill();
}

int main(void)
{
  unsigned int id;
  unsigned long time;
	
  id = OS_Id();
  PF2 ^= 0x04;
  Display_Message(0,line++, "Hello: ", id);
  PF2 ^= 0x04;
  OS_AddThread(thread, 128, 1);
  PF2 ^= 0x04;
  time = OS_Time();
  PF2 ^= 0x04;
  OS_Sleep(1000);
  PF2 ^= 0x04;
  time = (((OS_TimeDifference(time, OS_Time()))/1000ul)*125ul)/10000ul;
  Display_Message(0,line++, "Sleep: ", time);
  PF2 ^= 0x04;
  OS_Kill();
}

#elif (TEST == 1)
//*****************Test thread allocation*************************
// Test thread allocation expansion, what happens if you have more than 5 threads
uint32_t Count1;   // number of times thread1 loops
uint32_t Count2;   // number of times thread2 loops
uint32_t Count3;   // number of times thread3 loops
uint32_t Count4;   // number of times thread4 loops

void Thread1(void)
{
  for(Count1 = 0; Count1 < 100; Count1++)
  {
    Display_Message(0,1,"Count1 =",Count1); 
    OS_Sleep(50);
  }
  OS_Kill();
}

void Thread2(void)
{
  for(Count2 = 0; Count2 < 100; Count2++)
  {
    Display_Message(0,2,"Count2 =",Count2); 
    OS_Sleep(50);
  }
  OS_Kill();
}

void Thread3(void)
{
  for(Count3 = 0; Count3 < 100; Count3++)
  {
    Display_Message(0,3,"Count3 =",Count3); 
    OS_Sleep(50);
  }
  OS_Kill();
}

void Thread4(void)
{
  for(Count4 = 0; Count4 < 100; Count4++)
  {
    Display_Message(0,4,"Count4 =",Count4); 
    OS_Sleep(50);
  }
  OS_Kill();
}
// void ThreadIdle(void){         
//   IdleCount = 0;          
//   while(1) {
//     IdleCount++;
//     PD0 ^= 0x01;
//     WaitForInterrupt();
//   }
// }

int main(void){  
  // create initial foreground threads
  int NumCreated = 0;
  NumCreated += OS_AddThread(&Thread1,128,3);
  OS_Sleep(150);
  NumCreated += OS_AddThread(&Thread2,128,3); 
  OS_Sleep(150);
  NumCreated += OS_AddThread(&Thread3,128,3);  
  OS_Sleep(150);
  NumCreated += OS_AddThread(&Thread4,128,3);
  OS_Kill();
}

#elif (TEST == 2)
//*****************Test illegal access*************************
// Test array out of bound accesses
int arr[5];  
void ThreadOutOfBounds(void)
{
  uint32_t index = 0;        
  Display_Message(0,0,"bad indexing ", 0); 
  while(1)
  {
    index++;
    arr[index] = 5;
  }
}

void ThreadIllegalAccess(void)
{
  Display_Message(0,1,"Bad ptr time ", 0); 
  int* bad_ptr = (int*) 0x00000800;
  *bad_ptr = 5;
}

int StackBomb(int num)
{
  // if(num > 500) return num;
  StackBomb(num +1);
  arr[num%5] = num;
  return num;
}

void ThreadStackBomb(void)
{
  Display_Message(0,2,"THE STACK BO-BOMBS ", 0); 
  StackBomb(0);
}

int main(void){  
  int NumCreated = 0;
  NumCreated += OS_AddThread(&ThreadIllegalAccess,128,3);
  OS_Sleep(50);
  NumCreated += OS_AddThread(&ThreadOutOfBounds,128,3);
  OS_Sleep(50);
  NumCreated += OS_AddThread(&ThreadStackBomb,128,3);
  OS_Kill();

}

#endif
