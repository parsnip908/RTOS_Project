// *************os.c**************
// EE445M/EE380L.6 Labs 1, 2, 3, and 4 
// High-level OS functions
// Students will implement these functions as part of Lab
// Runs on LM4F120/TM4C123
// Jonathan W. Valvano 
// Jan 12, 2020, valvano@mail.utexas.edu


#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
// #include <stdatomic.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/PLL.h"
#include "../inc/LaunchPad.h"
// #include "../inc/Timer4A.h"
// #include "../inc/WTimer0A.h"
// #include "../inc/ADCT0ATrigger.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/heap.h"
#include "../RTOS_Labs_common/osasm.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Labs_common/queue.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../inc/driverlib/timer.h"
#include "../inc/driverlib/interrupt.h"
#include "../inc/driverlib/systick.h"
#include "../inc/driverlib/mpu.h"

#define STACK_SIZE 128*4
#define MIN_PRIORITY 4

#define IDLE_FORCE_PERIOD 100

// --- status macros ----
#define READY 0
#define RUNNING 1
#define BLOCKED 2
#define ASLEEP 3
#define EXITED 4

#define WTIMER0_BASE 0x40037000
// #define TIME_1MS 80000
// #define OS_PROFILE

//Thread globals for dynamic allocation
TCB_t** tcbs; //Pointer to TCB
uint32_t numThreads = 0;
uint32_t numThreadsCap = 5; //Initial capacity of threads (Serves as MAXTHREADS but dynamic)
uint8_t **stacks; //Pointer to TCB stacks

//Process globals for dynamic allocation
PCB_t** pcbs; //Pointer to PCB
uint32_t numProcs = 0;
uint32_t numProcsCap = 3; //Initial capacity of processes (Serves as MAXPROCS but dynamic)
PCB_t* newPCB = NULL;

queue_t ready_queues[MIN_PRIORITY];
queue_t special_queue;

TCB_t *RunPt = NULL;
// PCB_t *currPCB = NULL;
int OS_Status = 0;

volatile uint8_t idleCountdown = 0;

uint8_t stack_init[16*4] = {
  0x01, 0x00, 0x00, 0x00, //PSR <- TODO: what to put here
  0x00, 0x00, 0x00, 0x00, //PC <- must be updated to start of task
  0x14, 0x14, 0x14, 0x14, //LR <- must be updated to OS_Exit
  // 0x00, 0x00, 0x00, 0x00, //SP not present
  0x12, 0x12, 0x12, 0x12, //IP <- TODO: what do put here
  0x03, 0x03, 0x03, 0x03, //R3
  0x02, 0x02, 0x02, 0x02,
  0x01, 0x01, 0x01, 0x01,
  0x55, 0xAA, 0x55, 0xAA, //R0
  0x11, 0x11, 0x11, 0x11, //R11
  0x10, 0x10, 0x10, 0x10,
  0x09, 0x09, 0x09, 0x09,
  0x08, 0x08, 0x08, 0x08,
  0x07, 0x07, 0x07, 0x07, //R7
  0x06, 0x06, 0x06, 0x06,
  0x05, 0x05, 0x05, 0x05,
  0x04, 0x04, 0x04, 0x04
};

uint64_t OS_currTime;

uint64_t intDisableTimer;
uint32_t maxIntDisableTime;
uint64_t totalIntDisableTime;
uint64_t startTime;
bool timerRunning = false;

/*------------------------------------------------------------------------------
  Helper functions to increase thread and process cap (1 on success, 0 on failure)
 *------------------------------------------------------------------------------*/
int expandThreads(void){
	//Reallocate with new capacity
	uint32_t newCap = numThreadsCap + 3;
	TCB_t** newThreadPool = (TCB_t**)Heap_Realloc(tcbs, newCap * sizeof(TCB_t*));
	
	//Test if successful
	if(newThreadPool == NULL){
		//Fail
		return -1;
	}
	else{
		//Success
		tcbs = newThreadPool;
		// //Init new slots
    // for(uint32_t i = numThreadsCap; i < newCap; i++){
		// 	tcbs[i].status = EXITED;
    //   tcbs[i].sp = NULL;
		// }
		for(uint32_t i = numThreadsCap; i < newCap; i++){
      tcbs[i] = NULL;
    }
		//Set new cap
		numThreadsCap = newCap;
		return 0;
	}
}

int expandProcesses(void){
	//Reallocate with new capacity
	uint32_t newProcCap = numProcsCap + 3;
	PCB_t** newPcbs = (PCB_t**)Heap_Realloc(pcbs, newProcCap * sizeof(PCB_t*));
	
	//Test if successful
	if(newPcbs == NULL){
		//Fail
		return 0;
	}
	else{
		//Success
		pcbs = newPcbs;
		//Initialize empty PCBs
    for (uint32_t i = numProcsCap; i < newProcCap; ++i){
      // pcbs[i].text = NULL;
      // pcbs[i].data = NULL;
      pcbs[i] = NULL;
    }
		//Set new cap
		numProcsCap = newProcCap;
		return 1;
	}
}

void intDisableTimerStart(void)
{
  if(!timerRunning)
  {
    intDisableTimer = OS_Time();
    // PD1 |= 0x02;
    timerRunning = true;
  }
}

void intDisableTimerEnd(void)
{
  uint64_t curr_time = OS_Time();
  uint64_t timeDiff = curr_time - intDisableTimer;
  totalIntDisableTime += timeDiff;
  if(timeDiff > maxIntDisableTime)
    maxIntDisableTime = timeDiff;
  // PD1 &= ~0x02;
  timerRunning = false;
}
/*------------------------------------------------------------------------------
  MPU fault handler
  Should be initialized to trigger on a MPU fault
 *------------------------------------------------------------------------------*/
// void MPU_FaultHandler(void) {
//   //Implement fault behavior
// }

/*
//Switch privilege to user and set global
void MPU_SetUnPrivilege(void) {
  MPU_SetUnPrivilegeASM();
  RunPt_Access = USER;
}

//Switch privilege to kernel and set global
void MPU_SetPrivilege(void) {
  MPU_SetPrivilegeASM();
  RunPt_Access = KERNEL;
}
*/

//Global vars for defining what regions to set 
//For MPU initialization, regions 0 and 7 are usually used for general memory access
//Peripheral region: protects peripheral registers, makes sure only kernel threads can access them
#define PERIPHERAL_REGION 7
#define PERIPHERAL_BASE_ADDR 0x40000000
//                       Size of region     | no code execution   | privileged (kernel) read write | enable region 
#define PERIPHERAL_FLAGS (MPU_RGN_SIZE_1G | MPU_RGN_PERM_NOEXEC | MPU_RGN_PERM_PRV_RW_USR_RW     | MPU_RGN_ENABLE)

#define STACK_REGION 6
#define STACK_FLAGS (MPU_RGN_SIZE_512B | MPU_RGN_PERM_NOEXEC | MPU_RGN_PERM_PRV_RW_USR_RW)

#define TEXT_REGION 5
#define TEXT_FLAGS (MPU_RGN_PERM_EXEC | MPU_RGN_PERM_PRV_RO_USR_RO)

#define DATA_REGION 4
#define DATA_FLAGS (MPU_RGN_PERM_EXEC | MPU_RGN_PERM_PRV_RW_USR_RW)

//Background region: protects entire region, create privileged RW access for code not covered by other regions, separate kernel and user
#define BACKGROUND_REGION 0
#define BACKGROUND_BASE_ADDR 0x0000000
//                       Size of region   | allow code execution | privileged (kernel) read write | enable region 
#define BACKGROUND_FLAGS (MPU_RGN_SIZE_1G | MPU_RGN_PERM_EXEC    | MPU_RGN_PERM_PRV_RW_USR_NO     | MPU_RGN_ENABLE)

//Initialize fault handler and MPU regions
void MPU_Init(void){
  MPUDisable();
  //Set whatever region is needed (subject to change)
  MPURegionSet(PERIPHERAL_REGION, PERIPHERAL_BASE_ADDR, PERIPHERAL_FLAGS);
  MPURegionSet(BACKGROUND_REGION, BACKGROUND_BASE_ADDR, BACKGROUND_FLAGS);
  IntEnable(4);
}

/*------------------------------------------------------------------------------
  MPU set functions
  Should set privilege access bit and record in global var
 *------------------------------------------------------------------------------*/
//MPU Globals
Access RunPt_Access = USER;

/*------------------------------------------------------------------------------
  Systick Interrupt Handler
  SysTick interrupt happens every 10 ms
  used for preemptive thread switch
 *------------------------------------------------------------------------------*/

unsigned long OS_LockScheduler(void){
  // lab 4 might need this for disk formating
  return 0;// replace with solution
}
void OS_UnLockScheduler(unsigned long previous){
  // lab 4 might need this for disk formating
}

void SysTick_Clear(void)
{
  STCURRENT = 0;
}

void SysTick_Init(unsigned long period)
{
  // Counts = 0;
  SysTickDisable();
  SysTickPeriodSet(period-1);
  // SysTickIntRegister(&SysTick_Handler);
  SysTickIntEnable();
  SysTick_Clear();

  // STCTRL = 0; // disable SysTick during setup
  // STRELOAD = period-1;// reload value
  // STCURRENT = 0; // any write to current clears it
  SYSPRI3 = (SYSPRI3&0x00FFFFFF)|0xE0000000; // priority 2
  // STCTRL = 0x07; // enable, core clock, interrupts
}

void SysTick_Enable(void)
{
  SysTick_Clear();
  SysTickEnable();  
}

void WideTimer0_Init(void)
{
  (*((volatile uint32_t *)0x400FE65C)) |= 0b0011;
  wait_cycles(3);

  TimerDisable(WTIMER0_BASE, TIMER_BOTH);
  TimerConfigure(WTIMER0_BASE, TIMER_CFG_ONE_SHOT_UP);
  TimerClockSourceSet(WTIMER0_BASE, TIMER_CLOCK_SYSTEM);
  // TimerConfigure(WTIMER0_BASE, TIMER_CFG_RTC);
  TimerEnable(WTIMER0_BASE, TIMER_BOTH);
  // TimerRTCEnable(WTIMER0_BASE);
  TimerLoadSet64(WTIMER0_BASE, 1);
  // uint64_t oldval = TimerValueGet64(WTIMER0_BASE);
  // while(1)
  // {
  //   uint64_t newval = TimerValueGet64(WTIMER0_BASE);
  //   if(newval > oldval + 80000000)
  //   {
  //     oldval += 80000000;
  //     printf("%llu\n", oldval);
  //   }

  // }
  // wait_cycles(TIME_1MS);
  // uint64_t val1 = TimerValueGet64(WTIMER0_BASE);
  // wait_cycles(TIME_1MS);
  // uint64_t val2 = TimerValueGet64(WTIMER0_BASE);
  // printf("Timer read test: %llu %llu \n", val1, val2);
}

void OS_IdleIterator(queue_t* queue, void *data)
{
  TCB_t* tcb = (TCB_t*) data;
  if(tcb->status == ASLEEP && tcb->sleep < TimerValueGet64(WTIMER0_BASE))
  {
    queue_delete(queue, data);
    tcb->status = READY;
    queue_enqueue(&ready_queues[tcb->priority], data);
  }
  else if(tcb->status == EXITED)
  {
    queue_delete(queue, data);
    // if(tcb->parent)
    // {
    //   int i;
    //   for (i = 0; i<THREADSPERPROC; i++)
    //     if(tcb->parent->tcbs[i] == tcb)
    //       tcb->parent->tcbs[i] = NULL;
      
    //   bool threadPresent = false;
    //   for(int i = 0; i < THREADSPERPROC; i++)
    //     if(tcb->parent->tcbs[i])
    //     {
    //       threadPresent = true;
    //       break;
    //     }
    //   if(!threadPresent)
    //   {
    //     Heap_Free(tcb->parent->text);
    //     Heap_Free(tcb->parent->data);
    //     tcb->parent->text = NULL;
    //     tcb->parent->data = NULL;
    //   }
    // }
    tcb->sp = NULL;
    numThreads--;
    //TODO: what to do here
  }
}

void OS_IdleThread(void)
{
  while(1)
  {
    PD0 ^= 0x01;
    // PD2 ^= 0x04;       // heartbeat
    idleCountdown = IDLE_FORCE_PERIOD;
    //TODO: disable preempt
    DisableInterrupts();
    intDisableTimerStart();
    // uint32_t time = TimerValueGet64(WTIMER0_BASE);
    //iterate specialqueue
      //check sleeping
      //delete exited
    queue_iterate(&special_queue, &OS_IdleIterator);
    for (uint32_t i = 0; i < numThreadsCap; ++i)
    {
      if(tcbs[i]->status == ASLEEP)
        OS_IdleIterator(&special_queue, tcbs[i]);
    }
    //TODO: unblock threads
    // PD2 ^= 0x04;       // heartbeat
    OS_Suspend();
  }
}

/**
 * @details  Initialize operating system, disable interrupts until OS_Launch.
 * Initialize OS controlled I/O: serial, ADC, systick, LaunchPad I/O and timers.
 * Interrupts not yet enabled.
 * @param  none
 * @return none
 * @brief  Initialize OS
 */
void OS_Init(void){
  // put Lab 2 (and beyond) solution here
  DisableInterrupts();
  // intDisableTimerStart();
  PLL_Init(Bus80MHz);
  UART_Init();       // serial I/O for interpreter
  Heap_Init();
  ST7735_InitR(INITR_REDTAB); // LCD initialization
  printf("OS_init\n");

  // printf("Table adddr: %x\n", NVIC_VTABLE_R);

  // WideTimer0A_Init(increment_time, 80000, 5);
  for(int i = 0; i < MIN_PRIORITY; i++)
    queue_create(&ready_queues[i]);
  queue_create(&special_queue);

  //Initialize TCBs and PCBs
  pcbs = (PCB_t**)Heap_Calloc(numProcsCap * sizeof(PCB_t*));
  tcbs = (TCB_t**)Heap_Calloc(numThreadsCap * sizeof(TCB_t*));
  
  //Initialize empty PCBs
  // memset((void*) pcbs, 0, sizeof(PCB_t)*numProcsCap);

  //Initialize empty TCBs
  // memset((void*) tcbs, 0, sizeof(TCB_t)*numThreadsCap);
  // for (int i = 0; i < numThreadsCap; ++i)
  // {
  //   tcbs[i].status = EXITED;
  // }

  //setup global timer
  WideTimer0_Init();
  SysTick_Init(TIME_1MS*10);
  OS_SetupGPIOF();

  // IntRegister(14, &PendSV_Handler);
  IntPrioritySet(14, 7 << 5);
  // IntPendClear(14);
  // IntEnable(14);

  //TODO: systick init. preempt disabled

  OS_AddThread(&OS_IdleThread, 1024, MIN_PRIORITY-1);

  eFile_Mount();

  MPU_Init();
  OS_Status = 1;
  printf("OS_init\n");
  // intDisableTimerEnd();
  EnableInterrupts();
  
  //Init MPU
  //MPU_Init();
}; 

//******** MPU regions  *************** 
// TM4C hardware limits MPU to 8 regions (is this right?)
// Add a special user region of the MPU for each additional process (its threads inherit the region)
// Independent threads don't get MPU region, they just get kernel permission
// When you start a thread, enable its specific MPU region access
// When switching threads, disable a thread's MPU region and enable the next thread's MPU region

//******** OS_AddThread *************** 
// add a foregound thread to the scheduler
// Inputs: pointer to a void/void foreground task
//         number of bytes allocated for its stack
//         priority, 0 is highest, 5 is the lowest
// Outputs: 0 if successful, -1 if this thread can not be added
// stack size must be divisable by 8 (aligned to double word boundary)
// In Lab 2, you can ignore both the stackSize and priority fields
// In Lab 3, you can ignore the stackSize fields
int OS_AddThread(void(*task)(void), uint32_t stackSize, uint32_t priority){
  // put Lab 2 (and beyond) solution here
  DisableInterrupts();
  intDisableTimerStart();

  //Check if space to put new thread
  if(numThreads >= numThreadsCap){
    if(expandThreads()){
      printf("addthread expand threads failed\n");
      intDisableTimerEnd();
      EnableInterrupts();
      return 0;
    }
  }

		
  uint32_t i;
  //find first open TCB block
  for(i=0; i<numThreadsCap; i++)
  {
    if(tcbs[i] == NULL)
    {
      tcbs[i] = Heap_Calloc(sizeof(TCB_t));
      if(tcbs[i] == NULL)
      {
        printf("addthread TCB alloc failed\n");
        intDisableTimerEnd();
        EnableInterrupts();
        return 0;
      }
      tcbs[i]->status = EXITED;
      break;
    }
    if(tcbs[i]->status == EXITED && tcbs[i]->sp == NULL) break;
  }
  
  //Allocate stack
  tcbs[i]->stack_base = (uint8_t *) Heap_MallocAlignedPow2(STACK_SIZE);
  if(tcbs[i]->stack_base == NULL)
  {
    printf("addthread stack alloc failed\n");
    intDisableTimerEnd();
    EnableInterrupts(); 
    return 0;
  }
  uint8_t * newStack = tcbs[i]->stack_base;

  PD3 ^= 0x08;
  // int i = numThreads;
  numThreads++;
  tcbs[i]->id = i;
  tcbs[i]->priority = (priority < MIN_PRIORITY) ? priority : MIN_PRIORITY-1;
  tcbs[i]->sleep = 0;
  tcbs[i]->msTime = 0;
  tcbs[i]->status = READY;
  tcbs[i]->sp = &newStack[STACK_SIZE-(16*4)];
  tcbs[i]->access = USER; //For MPU
  if(newPCB)
  {
    tcbs[i]->parent = newPCB;
    newPCB = NULL;
  }
  else if(RunPt)
    tcbs[i]->parent = RunPt->parent;
  else
    tcbs[i]->parent = NULL;

  uint32_t task_uintptr = (uint32_t) task;
  uint32_t kill_uintptr = (uint32_t) &OS_Kill;
  uint32_t data_uintptr = 0x09090909;
  if(tcbs[i]->parent)
  {
    data_uintptr = (uint32_t) tcbs[i]->parent->data;
    int k;
    for(k=0; k<THREADSPERPROC; k++)
      if(tcbs[i]->parent->tcbs[k] == NULL) break;
    if (k ==THREADSPERPROC)
    {
      Heap_Free(newStack);
      tcbs[i]->sp = NULL;
      tcbs[i]->status = EXITED;
      numThreads--;
      printf("addthread too many threads in proc\n");
      intDisableTimerEnd();
      EnableInterrupts();
      return 0;
    }
    tcbs[i]->parent->tcbs[k] = tcbs[i];
  }

  for(int j = 0; j<4; j++)
  {
    stack_init[7-j] = (uint8_t) (task_uintptr >> (8*j));
    stack_init[11-j] = (uint8_t) (kill_uintptr >> (8*j));
    stack_init[43-j] = (uint8_t) (data_uintptr >> (8*j));
  }


  for(int j = 0; j<16*4; j++)
  {
    newStack[STACK_SIZE-1-j] = stack_init[j];
  }

  queue_enqueue(&ready_queues[tcbs[i]->priority], (void*)tcbs[i]);

  intDisableTimerEnd();
  EnableInterrupts();
     
  return 1;
};

//******** OS_AddThread_Process *************** 
// Add thread with user permissions, helper function for addprocess
/*
int OS_AddThread_Process(void(*task)(void), uint32_t stackSize, uint32_t priority){
  // put Lab 2 (and beyond) solution here
  
  //Check if space to put new thread
  if(numThreads >= numThreadsCap){
    if(expandThreads() == 0){
      return 0;
    }
  }

  DisableInterrupts();
  intDisableTimerStart();
  int i;
  //find first open TCB block
  for(i=0; i<numThreadsCap; i++)
    if(tcbs[i].status == EXITED && tcbs[i].sp == NULL) break;
  
  PD3 ^= 0x08;
  // int i = numThreads;
  numThreads++;
  tcbs[i].id = i;
  tcbs[i].priority = (priority < MIN_PRIORITY) ? priority : MIN_PRIORITY-1;
  tcbs[i].sleep = 0;
  tcbs[i].msTime = 0;
  tcbs[i].status = READY;
  tcbs[i].sp = &stacks[i][STACK_SIZE-(16*4)];
  tcbs[i].access = USER; //For MPU
  if(newPCB)
  {
    tcbs[i].parent = newPCB;
    newPCB = NULL;
  }
  else if(RunPt)
    tcbs[i].parent = RunPt->parent;
  else
    tcbs[i].parent = NULL;

  uint32_t task_uintptr = (uint32_t) task;
  uint32_t kill_uintptr = (uint32_t) &OS_Kill;
  uint32_t data_uintptr = 0x09090909;
  if(tcbs[i].parent)
  {
    data_uintptr = (uint32_t) tcbs[i].parent->data;
    int k;
    for(k=0; k<THREADSPERPROC; k++)
      if(tcbs[i].parent->tcbs[k] == NULL) break;
    if (k ==THREADSPERPROC)
    {
      tcbs[i].sp = NULL;
      tcbs[i].status = EXITED;
      numThreads--;
      return 0;
    }
    tcbs[i].parent->tcbs[k] = &tcbs[i];
  }

  for(int j = 0; j<4; j++)
  {
    stack_init[7-j] = (uint8_t) (task_uintptr >> (8*j));
    stack_init[11-j] = (uint8_t) (kill_uintptr >> (8*j));
    stack_init[43-j] = (uint8_t) (data_uintptr >> (8*j));
  }

  for(int j = 0; j<16*4; j++)
  {
    stacks[i][STACK_SIZE-1-j] = stack_init[j];
  }

  queue_enqueue(&ready_queues[tcbs[i].priority], (void*)&tcbs[i]);

  intDisableTimerEnd();
  EnableInterrupts();
     
  return 1;
};
*/
//******** OS_AddProcess *************** 
// add a process with foregound thread to the scheduler
// Inputs: pointer to a void/void entry point
//         pointer to process text (code) segment
//         pointer to process data segment
//         number of bytes allocated for its stack
//         priority (0 is highest)
// Outputs: 1 if successful, 0 if this process can not be added
// This function will be needed for Lab 5
// In Labs 2-4, this function can be ignored
// NOTE: Assume those are user processes
int OS_AddProcess(void(*entry)(void), void *text, void *data, 
  unsigned long stackSize, unsigned long priority){
  
  DisableInterrupts();
  intDisableTimerStart();

  //Check if space to put new process
  if(numProcs >= numProcsCap){
    if(expandProcesses() == 0){
      return 0;
    }
  }

  uint32_t i;
  //find first open PCB block
  for(i=0; i<numProcsCap; i++)
  {
    if(pcbs[i] == NULL)
    {
      pcbs[i] = Heap_Calloc(sizeof(PCB_t));
      if(pcbs[i] == NULL)
        return 0;
      break;
    }
    if(pcbs[i]->text == NULL) break;
  }

  printf("building proc %u\n", i);

  if(i >= numProcsCap)
  {
    printf("too many procs\n");

    Heap_Free(text);
    Heap_Free(data);
    return 0;
  }

  pcbs[i]->id = i;
  pcbs[i]->text = text;
  pcbs[i]->text_size = (flog2(Heap_GetAlloc(text)) -1) <<1;
  pcbs[i]->data = data;
  pcbs[i]->data_size = (flog2(Heap_GetAlloc(data)) -1) <<1;
  printf("addproc alloc size %d, %d\n", pcbs[i]->text_size, pcbs[i]->data_size);

  newPCB = pcbs[i];

  if(!OS_AddThread(entry, stackSize, priority))
  {
    printf("addproc issue with addthread\n");
    Heap_Free(text);
    Heap_Free(data);
    pcbs[i]->text = NULL;
    pcbs[i]->data = NULL;
    newPCB = NULL;
    pcbs[i]->tcbs[0] = NULL;
    return 0;
  }

  numProcs++;
  return 1;     
}

//******** OS_Id *************** 
// returns the thread ID for the currently running thread
// Inputs: none
// Outputs: Thread ID, number greater than zero 
uint32_t OS_Id(void){
  // put Lab 2 (and beyond) solution here
  
  return RunPt->id; // replace this line with solution
};

// ******** OS_Sleep ************
// place this thread into a dormant state
// input:  number of msec to sleep
// output: none
// You are free to select the time resolution for this function
// OS_Sleep(0) implements cooperative multitasking
void OS_Sleep(uint32_t sleepTime){
  // put Lab 2 (and beyond) solution here
  if(sleepTime == 0)
  {
    OS_Suspend();
    return;
  }
  DisableInterrupts();
  intDisableTimerStart();
  // calculate when to wake up
  RunPt->sleep = OS_getSleepTime(sleepTime);
  // set status and enqueue
  RunPt->status = ASLEEP;
  queue_enqueue(&special_queue, RunPt);
  // ctx switch with pendsv

  //****** FOR MPU ********
  //TODO: Do somthing different with user vs kernel thread (SVC handler or smth)
  if(RunPt->access == USER){
    //User thread
  } else {
    //Kernel thread
  }
  NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
  SysTick_Clear();
  intDisableTimerEnd();
  EnableInterrupts();
  wait_cycles(10);
  return;
};  

// ******** OS_Kill ************
// kill the currently running thread, release its TCB and stack
// input:  none
// output: none
void OS_Kill(void){
  // put Lab 2 (and beyond) solution here

  DisableInterrupts();
  intDisableTimerStart();
	//Kill RunPt stack
	Heap_Free(RunPt->stack_base);
	RunPt->sp = NULL;
	
  //enque current thread
  if(RunPt->parent)
  {
    int i;
    for (i = 0; i<THREADSPERPROC; i++)
      if(RunPt->parent->tcbs[i] == RunPt)
        RunPt->parent->tcbs[i] = NULL;
    
    bool threadPresent = false;
    for(int i = 0; i < THREADSPERPROC; i++)
      if(RunPt->parent->tcbs[i])
      {
        threadPresent = true;
        break;
      }
    if(!threadPresent)
    {
      printf("safe killing proc\n");
      Heap_Free(RunPt->parent->text);
      Heap_Free(RunPt->parent->data);
      RunPt->parent->text = NULL;
      RunPt->parent->data = NULL;
      numProcs--;
    }
  }
  RunPt->status = EXITED;
  // RunPt->sp = NULL;
  // numThreads--;

  //enqueue into special queue. idle thread will deal with it.
  queue_enqueue(&special_queue, RunPt);
  //trigger pendsv
  //  context will switch, execution will never come back

  //****** FOR MPU ********
  //TODO: Do somthing different with user vs kernel thread (SVC handler or smth)
  if(RunPt->access == USER){
    //User thread
  } else {
    //Kernel thread
  }
  NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
  SysTick_Clear();
  intDisableTimerEnd();
  EnableInterrupts();
  wait_cycles(10);
  return;
}; 

// ******** OS_Suspend ************
// suspend execution of currently running thread
// scheduler will choose another thread to execute
// Can be used to implement cooperative multitasking 
// Same function as OS_Sleep(0)
// input:  none
// output: none
void OS_Suspend(void){
  // put Lab 2 (and beyond) solution here
  // SysTickDisable();
  DisableInterrupts();
  intDisableTimerStart();
#ifdef OS_PROFILE
  PD0 ^= 0x01;       // heartbeat  
#endif
  //enque current thread
  RunPt->status = READY;
  queue_enqueue(&ready_queues[RunPt->priority], RunPt);
  // int count = 0;
  // while((NVIC_INT_CTRL_R >> 12 & 0xFF) == 0x0E)
  // {
  //   printf("%d: %x\n", count++, NVIC_INT_CTRL_R);
  // }

  //trigger pendsv
  //  context will switch, execution will come back here eventually

  //****** FOR MPU ********
  //TODO: Do somthing different with user vs kernel thread (SVC handler or smth)
  if(RunPt->access == USER){
    //User thread
  } else {
    //Kernel thread
  }
  NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
  SysTick_Clear();
  intDisableTimerEnd();
  EnableInterrupts();
  wait_cycles(10);
#ifdef OS_PROFILE
  PD0 ^= 0x01;       // heartbeat  
#endif
  return;
};

void OS_Block(void)
{
  DisableInterrupts();
  intDisableTimerStart();
  //enque current thread
  RunPt->status = BLOCKED;
  queue_enqueue(&special_queue, RunPt);

  //****** FOR MPU ********
  //TODO: Do somthing different with user vs kernel thread (SVC handler or smth)
  if(RunPt->access == USER){
    //User thread
  } else {
    //Kernel thread
  }
  NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
  SysTick_Clear();
  intDisableTimerEnd();
  EnableInterrupts();
  wait_cycles(10);
  return;
}

void OS_Unblock(TCB_t * thread)
{
  queue_delete(&special_queue, thread);
  // TCB_t * tcb = (TCB_t *) thread;
  thread->status = READY;
  queue_enqueue(&ready_queues[thread->priority], thread);
  // if(thread->priority < RunPt->priority)
  //   OS_Suspend();
}


void OS_SysTick_Hook(void)
{
#ifdef OS_PROFILE
  PD2 ^= 0x04;       // heartbeat  
#endif
  // DisableInterrupts();
  intDisableTimerStart();
  // PD1 ^= 0x02;       // heartbeat
  //enque current thread
  RunPt->status = READY;
  queue_enqueue(&ready_queues[RunPt->priority], RunPt);
  // dont need to enable interaupts because will return back to handler
  intDisableTimerEnd();
  // EnableInterrupts();
}

void OS_MemManage_Hook(uint8_t ctrl_reg, uint8_t faultstat, uint8_t* sp)
{
  printf("Mem fault: %x, %x, %x\n", (uint32_t) ctrl_reg, (uint32_t) faultstat, (uint32_t) sp);
  if(ctrl_reg == 0)
    while(1);
  OS_Kill();
}

//called by ctx switch to get next thread.
TCB_t* OS_getNext()
{
#ifdef OS_PROFILE
  PD1 ^= 0x02;
#endif
  PD2 ^= 0x04;

  intDisableTimerStart();
  OS_currTime = OS_Time();

  if(idleCountdown)
  {
    int queueNum = 0;
    for(queueNum = 0; queueNum < MIN_PRIORITY; queueNum++)
    {
      if(queue_length(&ready_queues[queueNum]))
        break;
    }
    int retval = queue_dequeue(&ready_queues[queueNum], (void**) &RunPt);
    if(retval)
    {
      printf("Critical failure in OS_getNext. queue_dequeue failed.");
    }
    idleCountdown--;
  }
  else
  {
    RunPt = tcbs[0];
    queue_delete(&ready_queues[MIN_PRIORITY-1], (void*) RunPt);
  }

  //Setup MPU
  if(RunPt->parent && RunPt->access == USER)
  {
    //user thread
    MPURegionSet(STACK_REGION, (uint32_t) RunPt->stack_base, STACK_FLAGS | MPU_RGN_ENABLE);
    MPURegionSet(TEXT_REGION, (uint32_t) RunPt->parent->text, TEXT_FLAGS | MPU_RGN_ENABLE | RunPt->parent->text_size);
    MPURegionSet(DATA_REGION, (uint32_t) RunPt->parent->data, DATA_FLAGS | MPU_RGN_ENABLE | RunPt->parent->data_size);
    MPUEnable(MPU_CONFIG_PRIV_DEFAULT);
  }
  else
  {
    MPUDisable();
    MPURegionDisable(STACK_REGION);
    MPURegionDisable(TEXT_REGION);
    MPURegionDisable(DATA_REGION);
  }

  RunPt->status = RUNNING;
#ifdef OS_PROFILE
  PD1 ^= 0x02;
#endif
  intDisableTimerEnd();
  return RunPt;
}


// ******** OS_Time ************
// return the system time 
// Inputs:  none
// Outputs: time in 12.5ns units, 0 to 4294967295
// The time resolution should be less than or equal to 1us, and the precision 32 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_TimeDifference have the same resolution and precision 
uint64_t OS_Time(void)
{
  return TimerValueGet64(WTIMER0_BASE); // replace this line with solution
};
// return  current time + certain milliseconds. set this way to abstract precision of OS_time and ms
// returns with precision of OS_time.
uint64_t OS_getSleepTime(int ms)
{
  return TimerValueGet64(WTIMER0_BASE) + ((uint64_t)ms * TIME_1MS);
}

// ******** OS_TimeDifference ************
// Calculates difference between two times
// Inputs:  two times measured with OS_Time
// Outputs: time difference in 12.5ns units 
// The time resolution should be less than or equal to 1us, and the precision at least 12 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_Time have the same resolution and precision 
uint64_t OS_TimeDifference(uint64_t start, uint64_t stop){
  // put Lab 2 (and beyond) solution here

  return stop - start; // replace this line with solution
};


// ******** OS_ClearMsTime ************
// sets the system time to zero (solve for Lab 1), and start a periodic interrupt
// Inputs:  none
// Outputs: none
// You are free to change how this works
// int OS_currTime;
void OS_ClearMsTime(void){
  RunPt->msTime = (uint32_t)(OS_Time() / TIME_1MS);
};

// ******** OS_MsTime ************
// reads the current time in msec (solve for Lab 1)
// Inputs:  none
// Outputs: time in ms units
// You are free to select the time resolution for this function
// For Labs 2 and beyond, it is ok to make the resolution to match the first call to OS_AddPeriodicThread
uint32_t OS_MsTime(void){
  return (uint32_t) (OS_Time() / TIME_1MS) - RunPt->msTime;
};


//******** OS_Launch *************** 
// start the scheduler, enable interrupts
// Inputs: number of 12.5ns clock cycles for each time slice
//         you may select the units of this parameter
// Outputs: none (does not return)
// In Lab 2, you can ignore the theTimeSlice field
// In Lab 3, you should implement the user-defined TimeSlice field
// It is ok to limit the range of theTimeSlice to match the 24-bit SysTick
// void StartOS();
void OS_Launch(uint32_t theTimeSlice){
  // put Lab 2 (and beyond) solution here
  // STCTRL = 0; // disable SysTick during setup
  // STCURRENT = 0; // any write to current clears it
  // SYSPRI3 =(SYSPRI3&0x00FFFFFF)|0xE0000000; // priority 7
  // STRELOAD = theTimeSlice - 1; // reload value
  // STCTRL = 0x00000007; // enable, core clock and interrupt arm
  SysTick_Init(theTimeSlice);
  SysTick_Enable();
  // MPUEnable(MPU_CONFIG_PRIV_DEFAULT);
  for(uint32_t i = 0; i < numThreadsCap; i++)
    if(tcbs[i])
      tcbs[i]->access = KERNEL;

  startTime = OS_Time();
  OS_getNext();
  OS_Status = 2;
  StartOS(); // start on the first task
    
};

//************** I/O Redirection *************** 
// redirect terminal I/O to UART or file (Lab 4)

int StreamToDevice=0;                // 0=UART, 1=stream to file (Lab 4)

int fputc (int ch, FILE *f) { 
  if(StreamToDevice==1){  // Lab 4
    if(eFile_Write(ch)){          // close file on error
       OS_EndRedirectToFile(); // cannot write to file
       return 1;                  // failure
    }
    return 0; // success writing
  }
  
  // default UART output
  UART_OutChar(ch);
  return ch; 
}

int fgetc (FILE *f){
  char ch = UART_InCharNonBlock();  // receive from keyboard

  while(ch ==0)
  {
    OS_Sleep(5);
    ch = UART_InCharNonBlock();
  }

  UART_OutChar(ch);         // echo
  return ch;
}

int OS_RedirectToFile(const char *name){  // Lab 4
  eFile_Create(name);              // ignore error if file already exists
  if(eFile_WOpen(name)) return 1;  // cannot open file
  StreamToDevice = 1;
  return 0;
}

int OS_EndRedirectToFile(void){  // Lab 4
  StreamToDevice = 0;
  if(eFile_WClose()) return 1;    // cannot close file
  return 0;
}

int OS_RedirectToUART(void){
  StreamToDevice = 0;
  return 0;
}

int OS_RedirectToST7735(void){
  
  return 1;
}

