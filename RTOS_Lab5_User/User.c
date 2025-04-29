// User.c
// Runs on LM4F120/TM4C123
// Standalone user-level process example

#include <stdio.h>
#include <stdint.h>
#include "inc/hw_types.h"
#include "inc/tm4c123gh6pm.h"

#include "OS.h"
#include "Display.h"

#define PF2     (*((volatile uint32_t *)0x40025010))
#define PF3     (*((volatile uint32_t *)0x40025020))

unsigned int line = 0;

//Test 0 is regular, test 1 = failure, test 2 = outofbounds
#define Test 2

//Regular user
#if Test == 0
void thread(void)
{
  unsigned int id;
	
  id = OS_Id();
  PF3 ^= 0x08;
  Display_Message(0,line++, "Thread: ", id);
  PF3 ^= 0x08;
  OS_Sleep(2000);
  PF3 ^= 0x08;
  Display_Message(0,line++, "Thread dying: ", id);
  PF3 ^= 0x08;
  OS_Kill();
}

int main(void)
{
  unsigned int id;
  unsigned long time;
	
  id = OS_Id();
  PF2 ^= 0x04;
  Display_Message(0,line++, "Hello world: ", id);
  PF2 ^= 0x04;
  OS_AddThread(thread, 128, 1);
  PF2 ^= 0x04;
  time = OS_Time();
  PF2 ^= 0x04;
  OS_Sleep(1000);
  PF2 ^= 0x04;
  time = (((OS_TimeDifference(time, OS_Time()))/1000ul)*125ul)/10000ul;
  Display_Message(0,line++, "Sleep time: ", time);
  PF2 ^= 0x04;
  OS_Kill();
}
//Illegal access fail
#elif Test == 1
void thread(void)
{
  unsigned int id;
	
  id = OS_Id();
  PF3 ^= 0x08;
  Display_Message(0,line++, "Thread: ", id);
  PF3 ^= 0x08;
  OS_Sleep(2000);
  PF3 ^= 0x08;
  Display_Message(0,line++, "Trigger thread error: ", id);
  PF3 ^= 0x08;
  //Illegal access failure
  int* ptr = 0x00800;
  *ptr = 5;
  OS_Kill();
}

int main(void)
{
  unsigned int id;
  unsigned long time;
	
  id = OS_Id();
  PF2 ^= 0x04;
  Display_Message(0,line++, "Hello world: ", id);
  PF2 ^= 0x04;
  OS_AddThread(thread, 128, 1);
  PF2 ^= 0x04;
  time = OS_Time();
  PF2 ^= 0x04;
  OS_Sleep(1000);
  PF2 ^= 0x04;
  time = (((OS_TimeDifference(time, OS_Time()))/1000ul)*125ul)/10000ul;
  Display_Message(0,line++, "Sleep time: ", time);
  PF2 ^= 0x04;
  OS_Kill();
}
//Out of bounds fail
#elif Test == 2
void thread(void)
{
  unsigned int id;
	
  id = OS_Id();
  PF3 ^= 0x08;
  Display_Message(0,line++, "Thread: ", id);
  PF3 ^= 0x08;
  OS_Sleep(2000);
  PF3 ^= 0x08;
  Display_Message(0,line++, "Trigger thread error: ", id);
  PF3 ^= 0x08;
  //Illegal access failure
  int arr[5];  
  uint32_t index = 0;        
  for(;;){
    index++;
    arr[index] = 5;
  }
  OS_Kill();
}

int main(void)
{
  unsigned int id;
  unsigned long time;
	
  id = OS_Id();
  PF2 ^= 0x04;
  Display_Message(0,line++, "Hello world: ", id);
  PF2 ^= 0x04;
  OS_AddThread(thread, 128, 1);
  PF2 ^= 0x04;
  time = OS_Time();
  PF2 ^= 0x04;
  OS_Sleep(1000);
  PF2 ^= 0x04;
  time = (((OS_TimeDifference(time, OS_Time()))/1000ul)*125ul)/10000ul;
  Display_Message(0,line++, "Sleep time: ", time);
  PF2 ^= 0x04;
  OS_Kill();
}
#endif
