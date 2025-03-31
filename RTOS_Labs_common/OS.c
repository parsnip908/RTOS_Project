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
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/PLL.h"
#include "../inc/LaunchPad.h"
#include "../inc/Timer3A.h"
#include "../inc/Timer4A.h"
#include "../inc/Timer5A.h"
#include "../inc/WTimer0A.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/ST7735.h"
#include "../inc/ADCT0ATrigger.h"
#include "../RTOS_Labs_common/UART0int.h"
#include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Labs_common/heap.h"

// declare osasm functions
void ContextSwitch(void);
void StartOS(void);

// Performance Measurements 
int32_t MaxJitter;             // largest time jitter between interrupts in usec
uint32_t const JitterSize=JITTERSIZE;
uint32_t JitterHistogram[JITTERSIZE]={0,};
//When compiling check this: Program Size: Code=18892 RO-data=2544 RW-data=16 ZI-data=24816. How much ram used is RW-data + ZI-data. Don't exceed 32k

//Globals and stuff
uint32_t MSTime = 0; //Global MS time, incremented every MS using timer5A
volatile uint64_t tickCount = 0;  //Number of elapsed system ticks, used to test for waking up tasks. Incremented every systick handler
uint32_t timeSlice = 1; //Record this for OS_Sleep
int timerInit = 0; //If timer is initialized

//Thread globals for dynamic allocation
TCB* runPt; // points to TCB of currently running thread (used in osasm.s)
TCB* threadPool; //List of threads
uint32_t threadCount = 0; //Number of threads currently alive
uint32_t threadCap = 3; //Initial capacity of threads allocated
uint32_t threadID = 1; //Permanently incrementing thread ID

//Process Globals
PCB* processPool; //List of processes
uint32_t processCount = 0; //Number of processes currently alive
PCB* newProcess; //Determines if new process is bring created (new threads allocated to this process)
uint32_t processCap = 2; //Initial capacity of processes allocated
uint32_t processID = 1; //Permanently incrementing thread ID

//IDEA FOR DYNAMIC ALLOCATION: Set a small initial size, use pointers instead os static array, malloc initial list, and grow when needed via realloc

//FIFO and mailbox stuff
uint32_t fifo[FIFO_SIZE]; //Fifo array
Sema4Type fifoSemFilledSlots; //Fifo semaphore to signal filled slot (counting). This alerts producers when there are unfilled slots
Sema4Type fifoSemEmptySlots; //Fifo semaphore to signal empty slot. This alerts consumers when there are filled slots
Sema4Type fifoMutex; //Mutex to control access to Fifo data (one thread at a time)
uint32_t fifoStartPtr; //Start pointer of fifo, where the next element should be placed
uint32_t fifoEndPtr; //End pointer of fifo, where the next element should be removed from
uint32_t mailbox; //Whatever is stored in mailbox
Sema4Type boxFree; //Mailbox binary semaphore, alerts free box
Sema4Type dataValid; //Mailbox binary semaphore, alerts filled box

//SW1 task
uint32_t sw1_Init = 0;
void(*sw1Task)(void);
uint32_t sw1TaskPriority;

//SW2 task
uint32_t sw2_Init = 0;
void(*sw2Task)(void);
uint32_t sw2TaskPriority;
	
//Periodic Task
uint32_t numPeriodicTasks = 0;

//Dummy task
void Dummy(void){}

//DEBUG SEMAPHORE
uint32_t sigCalls = 0;
uint32_t waitCalls = 0;
//DEBUG: ARRAY DUMP: List of semaphore calls (number is thread ID, positive if signal, negative if wait)
//This tracks threads added and removed from semaphore blocked list (but only with one semaphore)
#define SEMA_DATA_SIZE 10
int8_t semaData[SEMA_DATA_SIZE];
uint32_t semaDataIndex = 0;
void Add_Sema_Data(int8_t data){
	semaData[semaDataIndex] = data;
	semaDataIndex++;
	if(semaDataIndex > SEMA_DATA_SIZE - 1){
		//Save last "x" ements of data, clear the rest
		int x = 300;
		for(int i = 0; i < x; i++){
			semaData[i] = semaData[SEMA_DATA_SIZE - x + i];
		}
		//Clear the rest
		for(int i = x; i < SEMA_DATA_SIZE; i++){
			semaData[i] = 0;
		}
		//Continue to add data from index "x"
		semaDataIndex = x; 
	}
}

//Fuctions to add and remove threads from sleeping, active, and semaphore blocked
//Add TCB to semaphore blocked, priority based
void Add_Sema_Blocked(Sema4Type *semaPt, TCB *tcbPt){ 
	//DEBUG: Test if runPt is not already in the blocked list
	TCB* checkBlockTCB = semaPt->blockedPt;
	for(int i = 0; i < semaPt->numBlocked; i++){
		//DEBUG CRITICAL ERROR: Already contained in block list, thread is blocked
		if(runPt == checkBlockTCB){
			DisableInterrupts();
			while(1){
				int a = 0;
			}
		}
		checkBlockTCB = checkBlockTCB->next_tcb_blocking;
	}
	
	//Index tracker
	uint32_t index = 0;
	
	//Add to semaphore blocked list
	if(semaPt->numBlocked == 0){
		//Set 1st blocked TCB
		semaPt->blockedPt = tcbPt;
		semaPt->blockedPt->next_tcb_blocking = NULL;
		semaPt->numBlocked = 1; //Set num blocked
	}
	//Check for insertion at head
	else if(tcbPt->priority < semaPt->blockedPt->priority){
		tcbPt->next_tcb_blocking = semaPt->blockedPt;
		semaPt->blockedPt = tcbPt;
		semaPt->numBlocked++; //Increment num blocked
	}
	//Insert into blocked linked list
	else {
		//Set other blocked TCBs
		TCB* setPointer = semaPt->blockedPt;
		//Traverse to list spot (highest to lowest priority). Move down the list if task is lower priority (Use <= to make it fair, adds to end of queue for its priority)
		while(setPointer->next_tcb_blocking != NULL && tcbPt->priority <= setPointer->next_tcb_blocking->priority && index < semaPt->numBlocked){
			setPointer = setPointer->next_tcb_blocking;
			index++;
		}
		//Insert TCB into the list, after setPointer
		tcbPt->next_tcb_blocking = setPointer->next_tcb_blocking;
		setPointer->next_tcb_blocking = tcbPt;
		semaPt->numBlocked++; //Increment num blocked
	}
}

//Returns TCB removed from semaphore, null if empty
TCB* Remove_Sema_Blocked(Sema4Type *semaPt){
	//Try to take the head of the list
	if(semaPt->numBlocked > 0){
		//Wake thread and move list 
		TCB* wakeThread = semaPt->blockedPt;
		semaPt->blockedPt = semaPt->blockedPt->next_tcb_blocking;
		semaPt->numBlocked--; //Decrement num blocked
		return wakeThread;
	}
	return NULL;
}

//Find next thread to run for the context switch (This should be called before a context switch)
void Find_Next_Thread(){
	//Move current thread to end of queue at its priority
	runPt->lastRunTickCount = tickCount;
	
	//Next thread to run
	TCB* nextThread;
	//Task to run is the one with the highest priority and was the longest ago
	uint32_t highestPriority = 99;
	uint64_t longestTicksAgo = 999999999;
	//Check sleeping thread
  for(int i = 0; i < threadCap; i++){
		//New highest priority active thread
		if(threadPool[i].priority < highestPriority && 
			(threadPool[i].state == ACTIVE || threadPool[i].state == RUNNING)){
			//Set thread and vars
			nextThread = &threadPool[i];
			highestPriority = nextThread->priority;
			longestTicksAgo = nextThread->lastRunTickCount;
		}
		//Active thread is same priority as highest but run longer ago (last run tick count is smaller)
		else if(threadPool[i].priority == highestPriority && 
			threadPool[i].lastRunTickCount < longestTicksAgo && 
			(threadPool[i].state == ACTIVE || threadPool[i].state == RUNNING)){
			//Set thread and vars
			nextThread = &threadPool[i];
			longestTicksAgo = nextThread->lastRunTickCount;
		}
	}
	//Assume nextThread will immediately be switched to
	nextThread->state = RUNNING;
	runPt->next_tcb = nextThread;
}

//Helper functions to increase thread and process cap (1 on success, 0 on failure)
int expandThreads(void){
	//Reallocate with new capacity
	uint32_t newCap = threadCap + 3;
	TCB* newThreadPool = (TCB*)Heap_Realloc(threadPool, newCap * sizeof(TCB));
	
	//Test if successful
	if(newThreadPool == NULL){
		//Fail
		return 0;
	}
	else{
		//Success
		threadPool = newThreadPool;
		//Init new slots
		for(int i = threadCap; i < newCap; i++){
			threadPool[i].state = DEAD;
		}
		//Set new cap
		threadCap = newCap;
		return 1;
	}
}
int expandProcesses(void){
	//Reallocate with new capacity
	uint32_t newCap = processCap + 2;
	PCB* newProcessPool = (PCB*)Heap_Realloc(processPool, newCap * sizeof(PCB));
	
	//Test if successful
	if(newProcessPool == NULL){
		//Fail
		return 0;
	}
	else{
		//Success
		processPool = newProcessPool;
		//Init new slots
		for(int i = processCap; i < newCap; i++){
			processPool[i].state = DEAD;
		}
		//Set new cap
		processCap = newCap;
		return 1;
	}
}

/*------------------------------------------------------------------------------
  Systick Interrupt Handler
  SysTick interrupt happens every 10 ms
  used for preemptive thread switch
 *------------------------------------------------------------------------------*/
void SysTick_Handler(void) {
  tickCount += 1;
	//Only set to active if it already active, sometimes this can interrupt OS_Wait context switch and make a block thread active
	//This can happen when thread is set to blocked but pendSV cannot activate yet
	if(runPt->state == RUNNING || runPt->state == ACTIVE){
		runPt->state = ACTIVE;
	}
	
  //Check sleeping thread
  for(int i = 0; i < threadCap; i++){
    //If sleep time is up, add thread back in
    if(threadPool[i].state == SLEEPING && tickCount >= threadPool[i].resume_tick){
      threadPool[i].state = ACTIVE;
    }
  }
	
	//Set next thread
	Find_Next_Thread();
	
	//Perform context switch
  ContextSwitch();
} // end SysTick_Handler

unsigned long OS_LockScheduler(void){
  // lab 4 might need this for disk formating
  return 0;// replace with solution
}
void OS_UnLockScheduler(unsigned long previous){
  // lab 4 might need this for disk formating
}

/*----------------------------------------------------------------------------
  PF1 Interrupt Init
 *----------------------------------------------------------------------------*/
// use for debugging profile
#define PF1       (*((volatile uint32_t *)0x40025008))
#define PF2       (*((volatile uint32_t *)0x40025010))
#define PF3       (*((volatile uint32_t *)0x40025020))
// global variable visible in Watch window of debugger
// increments at least once per button press
volatile uint32_t FallingEdges = 0;
void EdgeCounterPortF_Init(void){                          
  SYSCTL_RCGCGPIO_R |= 0x00000020; // (a) activate clock for port F
  FallingEdges = 0;             // (b) initialize counter
  GPIO_PORTF_LOCK_R = 0x4C4F434B;   // 2) unlock GPIO Port F
  GPIO_PORTF_CR_R = 0x1F;           // allow changes to PF4-0
  GPIO_PORTF_DIR_R |=  0x0E;    // output on PF3,2,1 
  GPIO_PORTF_DIR_R &= ~0x11;    // (c) make PF4,0 in (built-in button)
  GPIO_PORTF_AFSEL_R &= ~0x1F;  //     disable alt funct on PF4,0
  GPIO_PORTF_DEN_R |= 0x1F;     //     enable digital I/O on PF4   
  GPIO_PORTF_PCTL_R &= ~0x000FFFFF; // configure PF4 as GPIO
  GPIO_PORTF_AMSEL_R = 0;       //     disable analog functionality on PF
	
	//This allows SW1 (PF4) to interrupt, changed to allow SW2 (PF0) to interrupt as well
  GPIO_PORTF_PUR_R |= 0x11;     //     enable weak pull-up on PF4
  GPIO_PORTF_IS_R &= ~0x11;     // (d) PF4 is edge-sensitive
  GPIO_PORTF_IBE_R &= ~0x11;    //     PF4 is not both edges
  GPIO_PORTF_IEV_R &= ~0x11;    //     PF4 falling edge event
  GPIO_PORTF_ICR_R = 0x11;      // (e) clear flag4
  GPIO_PORTF_IM_R |= 0x11;      // (f) arm interrupt on PF4 *** No IME bit as mentioned in Book ***
  NVIC_PRI7_R = (NVIC_PRI7_R&0xFF00FFFF)|0x00A00000; // (g) priority 5
  NVIC_EN0_R = 0x40000000;      // (h) enable interrupt 30 in NVIC
}

// period in units of 12.5ns (assuming 80 MHz clock)
// maximum period is 2^24-1 (0.2097152 seconds)
void SysTick_Init(unsigned long period){
  //long primask = StartCriticalTime();
  NVIC_ST_CTRL_R = 0;         // disable SysTick during setup
  NVIC_ST_RELOAD_R = period-1;// reload value
  NVIC_ST_CURRENT_R = 0;      // any write to current clears it
  NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0x00FFFFFF)|0x40E00000; // This (4) sets systick to priority 2 and pendSV (E) to priority 7
  NVIC_ST_CTRL_R = 0x07;      // enable SysTick with core clock and interrupts
  //EndCriticalTime(primask);
}

// increment system time
void MS_Increment(void){
  MSTime = MSTime + 1;
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
  DisableInterruptTime();
  //PLL init and launchpad init (Lab1 has these)
  LaunchPad_Init(); 
  PLL_Init(Bus80MHz);
  
  //Initialize UART for interpreter
  UART_Init(); 

  //Initialize display
  ST7735_InitR(INITR_REDTAB); // LCD initialization

  //Initialize SW1 and SW2
  EdgeCounterPortF_Init();

  //Timer5A for MS time
  Timer5A_Init(&MS_Increment, 80e6/1000, 0);
	
	//Timer0A for jitter
	uint32_t jitter_period_lower = 0xFFFFFFFF; //32 lowest bits
	uint32_t jitter_period_upper = 0xFFFFFFFF; //32 upper bits
	WideTimer0A_Init(&Dummy, jitter_period_upper, jitter_period_lower, 0);
	timerInit = 1;

  // enable pendsv interrupt
  // set pendsv interrupt priorty to 7 (lowest)
  NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0xFF0FFFFF) | 0x7 << 21;
	
	//Initialize heap
	Heap_Init();
	
  //Initialize TCBs
	threadPool = (TCB*)Heap_Malloc(threadCap * sizeof(TCB));
  for (int i = 0; i < threadCap; i++) {
    threadPool[i].state = DEAD;
  }
	
	//Initialize PCBs
	processPool = (PCB*)Heap_Malloc(processCap * sizeof(PCB));
	for (int i = 0; i < processCap; i++) {
    processPool[i].state = DEAD;
  }
}; 

// ******** OS_InitSemaphore ************
// initialize semaphore 
// input:  pointer to a semaphore
// output: none
void OS_InitSemaphore(Sema4Type *semaPt, int32_t value){
  // put Lab 2 (and beyond) solution here
  semaPt->Value = value;
	semaPt->blockedPt = NULL;
	semaPt->numBlocked = 0;
}; 

// ******** OS_Wait ************
// decrement semaphore 
// Lab2 spinlock
// Lab3 block if less than zero
// input:  pointer to a counting semaphore
// output: none
void OS_Wait(Sema4Type *semaPt){	
	//Atomically modify semaphore
	long status = StartCriticalTime(); //Save the "I" bit and disable interrupts
	
	//DEBUG: WAIT CALL
	waitCalls = waitCalls + 1;
	
	//Decrement semaphore (This always happens)
	semaPt->Value = semaPt->Value - 1;
	
	//Test if available
	if(semaPt->Value < 0){
		//DEBUG: ARRAY DUMP
		Add_Sema_Data(-1 * runPt->id);
		
		//Not available, block the current thread
		runPt->state = BLOCKED;
		
		//Add to semaphore blocked list
		Add_Sema_Blocked(semaPt, runPt);
		
		//Find next thread
		Find_Next_Thread();
		
		//Make sure context switch happens. You can enable and disable interrupts here since interrupts don't call wait
		while(runPt->state == BLOCKED){ //Added this to force a context switch
			EnableInterruptTime();
			ContextSwitch(); //Context switch
			DisableInterruptTime();
		}
	}
	
	//Available, exit
	EndCriticalTime(status); //Restore I bit and enable interrupts
}; 

// ******** OS_Signal ************
// increment semaphore 
// Lab2 spinlock
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a counting semaphore
// output: none
void OS_Signal(Sema4Type *semaPt){	
  //Atomically modify semaphore
  long status = StartCriticalTime(); //Save the "I" bit and disable interrupts
	
	//DEBUG: SIGNAL CALL
	sigCalls = sigCalls + 1;
	
	//Increment semaphore
  semaPt->Value = semaPt->Value + 1;
	
	//Try to wake thread
	TCB* wakeThread = Remove_Sema_Blocked(semaPt);
	
	//If there is a thread to be woken up
	if(wakeThread != NULL){
		wakeThread->state = ACTIVE;
		wakeThread->lastRunTickCount = tickCount;
		
		//DEBUG: ARRAY DUMP
		Add_Sema_Data(wakeThread->id);
	}

  EndCriticalTime(status); //Restore I bit and enable interrupts
}; 

// ******** OS_bWait ************
// Lab2 spinlock, set to 0
// Lab3 block if less than zero
// input:  pointer to a binary semaphore
// output: none
void OS_bWait(Sema4Type *semaPt){
	//Atomically modify semaphore
	long status = StartCriticalTime(); //Save the "I" bit and disable interrupts
	
	//Available, decrement semaphore (only when full since it's binary) and enable interrupts
	semaPt->Value = 0;
	
	//Test if available
	if(semaPt->Value < 0){
		//Not available, block the current thread
		runPt->state = BLOCKED;
		
		//Add to semaphore blocked list
		Add_Sema_Blocked(semaPt, runPt);
		
		//Find next thread
		Find_Next_Thread();
		
		//Make sure context switch happens. You can enable and disable interrupts here since interrupts don't call wait
		while(runPt->state == BLOCKED){ //Added this to force a context switch
			EnableInterruptTime();
			ContextSwitch(); //Context switch
			DisableInterruptTime();
		}
	}
	
	EndCriticalTime(status); //Restore I bit and enable interrupts
}; 

// ******** OS_bSignal ************
// Lab2 spinlock, set to 1
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a binary semaphore
// output: none
void OS_bSignal(Sema4Type *semaPt){
  //Atomically modify semaphore
  long status = StartCriticalTime(); //Save the "I" bit and disable interrupts
	
	//Try to wake thread
	TCB* wakeThread = Remove_Sema_Blocked(semaPt);
	
	//Fill mailbox
	semaPt->Value = 1;
	
	//If there is a thread to be woken up
	if(wakeThread != NULL){
		wakeThread->state = ACTIVE;
		wakeThread->lastRunTickCount = tickCount;
	}
	
  EndCriticalTime(status); //Restore I bit and enable interrupts
}; 

//******** OS_AddThread *************** 
// add a foregound thread to the scheduler
// Inputs: pointer to a void/void foreground task
//         number of bytes allocated for its stack
//         priority, 0 is highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// stack size must be divisable by 8 (aligned to double word boundary)
// In Lab 2, you can ignore both the stackSize and priority fields
// In Lab 3, you can ignore the stackSize fields
int OS_AddThread(void(*task)(void), uint32_t stackSize, uint32_t priority){
	long status = StartCriticalTime(); //Save the "I" bit and disable interrupts
	
	//Check capacity
  if (threadCount >= threadCap) {
		//Try to increase size, return on fail
		if(expandThreads() == 0){
			EndCriticalTime(status); //Restore I bit and enable interrupts
			return 0;
		} 
  }

  //Find any free index to save thead to 
  int32_t index = -1;
  for(int i = 0; i < threadCap; i++){
    if(threadPool[i].state == DEAD){
      index = i;
			break;
    }
  }
	//Fail to find index
	if(index == -1){
		EndCriticalTime(status); //Restore I bit and enable interrupts
		return 0;
	}

  //Initialize thread
  TCB *newtcb = &threadPool[index];
  newtcb->id = threadID; // basic tid increments from 0
  newtcb->state = ACTIVE;
  newtcb->priority = priority; 
	newtcb->lastRunTickCount = tickCount;
	
	//Assign thread to process if thread is loaded from process
  if (newProcess != NULL) {
		//New thread called from AddProcess
    newtcb->pcb = newProcess;
    newtcb->pcb->numThreads++;
  } else if (runPt != NULL && runPt->pcb != NULL) {
		//New thread called from a running thread associated with a process
    newtcb->pcb = runPt->pcb;
    newtcb->pcb->numThreads++;
  } else {
		//Independent add thread call, or called from a thread not associated with a process
    newtcb->pcb = NULL;
  }
  
  //Update R9 for position-independent code
  uint32_t baseReg = 0x09090909;
  if (newtcb->pcb != NULL) {
    baseReg = (uint32_t)(newtcb->pcb->dataPt);
  }

  // push initial data onto stack
  newtcb->stack[STACKSIZE-1] = 0x01000000;       // PSR (set thumb bit)
  newtcb->stack[STACKSIZE-2] = (uint32_t)(task); // PC (function pointer)
  newtcb->stack[STACKSIZE-3] = 0x14141414;       // R14
  newtcb->stack[STACKSIZE-4] = 0x12121212;       // R12
  newtcb->stack[STACKSIZE-5] = 0x03030303;       // R3
  newtcb->stack[STACKSIZE-6] = 0x02020202;       // R2
  newtcb->stack[STACKSIZE-7] = 0x01010101;       // R1
  newtcb->stack[STACKSIZE-8] = 0x00000000;       // R0
  newtcb->stack[STACKSIZE-9] = 0x11111111;       // R11
  newtcb->stack[STACKSIZE-10] = 0x10101010;      // R10
  newtcb->stack[STACKSIZE-11] = baseReg;      	 // R9
  newtcb->stack[STACKSIZE-12] = 0x08080808;      // R8
  newtcb->stack[STACKSIZE-13] = 0x07070707;      // R7
  newtcb->stack[STACKSIZE-14] = 0x06060606;      // R6
  newtcb->stack[STACKSIZE-15] = 0x05050505;      // R5
  newtcb->stack[STACKSIZE-16] = 0x04040404;      // R4
  newtcb->sp = &newtcb->stack[STACKSIZE-16];     // Set thread SP

  // increment threadCount and threadID
  threadCount += 1;
  threadID += 1;

	EndCriticalTime(status); //Restore I bit and enable interrupts
  return 1;
};

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
int OS_AddProcess(void(*entry)(void), void *text, void *data, 
  unsigned long stackSize, unsigned long priority){
	long status = StartCriticalTime(); //Save the "I" bit and disable interrupts
		
  //Check capacity
  if (processCount >= processCap) {
		//Try to increase size, return on fail
		if(expandProcesses() == 0){
			EndCriticalTime(status); //Restore I bit and enable interrupts
			return 0;
		} 
  }

  //Find any free index to save process to 
  int32_t index = -1;
  for(int i = 0; i < processCap; i++){
    if(processPool[i].state == DEAD){
      index = i;
			break;
    }
  }
	//Fail to find index
	if(index == -1){
		EndCriticalTime(status); //Restore I bit and enable interrupts
		return 0;
	}

  //Initialize process
  PCB *newpcb = &processPool[index];
	newpcb->codePt = text;
  newpcb->dataPt = data;
	newpcb->state = ACTIVE;
	newpcb->priority = priority;
	newpcb->pid = processID;

  //Create new thread from process (thread should be tied to process)
  newProcess = newpcb; 
  if(!OS_AddThread(entry, stackSize, priority)){
		//Fail if cannot assign initial thread
		newpcb->state = DEAD;
		EndCriticalTime(status); //Restore I bit and enable interrupts
		newProcess = NULL;
		return 0; 
	}
  newProcess = NULL;

  //Increment threadCount and threadID
  processCount += 1;
  processID += 1;

	EndCriticalTime(status); //Restore I bit and enable interrupts
  return 1;
}


//******** OS_Id *************** 
// returns the thread ID for the currently running thread
// Inputs: none
// Outputs: Thread ID, number greater than zero 
uint32_t OS_Id(void){
  return runPt->id; 
};


//******** OS_AddPeriodicThread *************** 
// add a background periodic task
// typically this function receives the highest priority
// Inputs: pointer to a void/void background function
//         period given in system time units (12.5ns)
//         priority 0 is the highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// You are free to select the time resolution for this function
// It is assumed that the user task will run to completion and return
// This task can not spin, block, loop, sleep, or kill
// This task can call OS_Signal  OS_bSignal   OS_AddThread
// This task does not have a Thread ID
// In lab 1, this command will be called 1 time
// In lab 2, this command will be called 0 or 1 times
// In lab 2, the priority field can be ignored
// In lab 3, this command will be called 0 1 or 2 times
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddPeriodicThread(void(*task)(void), 
   uint32_t period, uint32_t priority){
  // put Lab 2 (and beyond) solution here
	// TODO: How to implement relative priority
	if(numPeriodicTasks == 0){
		//First periodic task
		Timer4A_Init(task,period,priority); //Run task periodically
		numPeriodicTasks = 1;
	} else if(numPeriodicTasks == 1){
		//Second periodic task
		Timer3A_Init(task, period,priority);
		numPeriodicTasks = 2;
	}
  return 0; // replace this line with solution
};
	 
/*----------------------------------------------------------------------------
  PF1 Interrupt Handler
 *----------------------------------------------------------------------------*/
void GPIOPortF_Handler(void){
	//Tasks are run as interrupt when switch is pressed
	//Check bit4 of PortF interrupt register (should be 1 if SW1 pressed)
	if(GPIO_PORTF_RIS_R & 0x10){
		GPIO_PORTF_ICR_R = 0x10; //Acknowledge flag4, clear the register
		
		//Debugging
		FallingEdges = FallingEdges + 1;
		PF1 ^= 0x02; // profile
		
		//Execute sw1 task
		if(sw1_Init == 1){
			(*sw1Task)(); 
		}
	}
	//Check bit0 of PortF interrupt register (should be 1 if SW2 pressed)
	if(GPIO_PORTF_RIS_R & 0x01){
		GPIO_PORTF_ICR_R = 0x01; //Acknowledge flag0, clear the register
		
		//Debugging
		FallingEdges = FallingEdges + 1;
		PF1 ^= 0x02; // profile
		
		//Execute sw2 task
		if(sw2_Init == 1){
			(*sw2Task)(); 
		}
	}
}

//******** OS_AddSW1Task *************** 
// add a background task to run whenever the SW1 (PF4) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is the highest, 5 is the lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed that the user task will run to completion and return
// This task can not spin, block, loop, sleep, or kill
// This task can call OS_Signal  OS_bSignal   OS_AddThread
// This task does not have a Thread ID
// In labs 2 and 3, this command will be called 0 or 1 times
// In lab 2, the priority field can be ignored
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW1Task(void(*task)(void), uint32_t priority){
  //Set SW1 task
  sw1Task = task;
  sw1TaskPriority = priority;
	sw1_Init = 1;
  return 0; // replace this line with solution
};

//******** OS_AddSW2Task *************** 
// add a background task to run whenever the SW2 (PF0) button is pushed
// Inputs: pointer to a void/void background function
//         priority 0 is highest, 5 is lowest
// Outputs: 1 if successful, 0 if this thread can not be added
// It is assumed user task will run to completion and return
// This task can not spin block loop sleep or kill
// This task can call issue OS_Signal, it can call OS_AddThread
// This task does not have a Thread ID
// In lab 2, this function can be ignored
// In lab 3, this command will be called will be called 0 or 1 times
// In lab 3, there will be up to four background threads, and this priority field 
//           determines the relative priority of these four threads
int OS_AddSW2Task(void(*task)(void), uint32_t priority){
  //Set SW2 task
  sw2Task = task;
  sw2TaskPriority = priority;
  sw2_Init = 1;
  return 0; // replace this line with solution
};

// ******** OS_Sleep ************
// place this thread into a dormant state
// input:  number of msec to sleep
// output: none
// You are free to select the time resolution for this function
// OS_Sleep(0) implements cooperative multitasking
void OS_Sleep(uint32_t sleepTime){
  DisableInterruptTime(); // start of atomic section
  // Sleep thread and assign its resume tick
  runPt->state = SLEEPING;
	//runPt->resume_tick = tickCount + (sleepTime * 80000 / timeSlice); //sleepTime is in units of MS, convert from MS to units of tickCount, which is period of Systick_Handler. 80,000 / timeSlice should be 1ms
	runPt->resume_tick = tickCount + 1; //sleepTime is in units of MS, convert from MS to units of tickCount, which is period of Systick_Handler. 80,000 / timeSlice should be 1ms
	runPt->lastRunTickCount = tickCount; //Move current thread to end of queue at its priority
	
  //Find next thread
	Find_Next_Thread();
	
	//Context switch
  EnableInterruptTime(); // end of atomic section
  ContextSwitch();
};  

// ******** OS_Kill ************
// kill the currently running thread, release its TCB and stack
// input:  none
// output: none
void OS_Kill(void){
  DisableInterruptTime(); // start of atomic section

  //Remove thread from linked list, mark as DEAD
  threadCount = threadCount - 1;
  runPt->state = DEAD;
	
	//Find next thread
	Find_Next_Thread();
	
	//Check process
	if (runPt->pcb != NULL){
    PCB *pcb = runPt->pcb;
    pcb->numThreads--;
		//If last thread killed
    if (pcb->numThreads == 0) {
			//Free code and data pointers, set PCB as dead so the next one created can replace it
      Heap_Free(pcb->codePt);
      Heap_Free(pcb->dataPt);
      pcb->state = DEAD;
    }
  }
	
	//Context switch
  EnableInterruptTime(); // end of atomic section
  ContextSwitch();    

  for(;;){}; // can not return
}; 

// ******** OS_Suspend ************
// suspend execution of currently running thread
// scheduler will choose another thread to execute
// Can be used to implement cooperative multitasking 
// Same function as OS_Sleep(0)
// input:  none
// output: none
void OS_Suspend(void){
	DisableInterruptTime(); // start of atomic section
	
	//Keep active
  if(runPt->state == RUNNING || runPt->state == ACTIVE){ //Check if thread is not blocked or anything for safety
		runPt->state = ACTIVE;
	}
	runPt->lastRunTickCount = tickCount; //Move current thread to end of queue at its priority
	
	//Find next thread
	Find_Next_Thread();
	
	//Context switch
	EnableInterruptTime(); // end of atomic section
  ContextSwitch();
};
  
// ******** OS_Fifo_Init ************
// Initialize the Fifo to be empty
// Inputs: size
// Outputs: none 
// In Lab 2, you can ignore the size field
// In Lab 3, you should implement the user-defined fifo size
// In Lab 3, you can put whatever restrictions you want on size
//    e.g., 4 to 64 elements
//    e.g., must be a power of 2,4,8,16,32,64,128
void OS_Fifo_Init(uint32_t size){
  // put Lab 2 (and beyond) solution here
  OS_InitSemaphore(&fifoSemEmptySlots, FIFO_SIZE); //Initialize semaphore of predetermined size (ignore size for lab 2)
  OS_InitSemaphore(&fifoSemFilledSlots, 0);
	OS_InitSemaphore(&fifoMutex, 1);
  fifoStartPtr = 0;
  fifoEndPtr = 0;
};

// ******** OS_Fifo_Put ************
// Enter one data sample into the Fifo
// Called from the background, so no waiting 
// Inputs:  data
// Outputs: true if data is properly saved,
//          false if data not saved, because it was full
// Since this is called by interrupt handlers 
//  this function can not disable or enable interrupts
int OS_Fifo_Put(uint32_t data){
  //This function should not used the previously created semaphores (those disable/enable interrupts)
  //Assume this won't be interrupted, since it is called by interrupt handlers
  if(fifoSemEmptySlots.Value > 0){
    //Success, slot found, gain critical access to add to FIFO
    fifo[fifoStartPtr] = data; //Store data
    fifoStartPtr++; //Increment start pointer
    if(fifoStartPtr >= FIFO_SIZE){fifoStartPtr = 0;} //Wrap around
		
    //Increment/decrement values
    fifoSemEmptySlots.Value = fifoSemEmptySlots.Value - 1;
    fifoSemFilledSlots.Value = fifoSemFilledSlots.Value + 1;
		
		//Wake up semaphore consumer task without interrupts
		//Test if thread can wake up
		if(fifoSemFilledSlots.Value > 0){
			//Try to wake thread
			TCB* wakeThread = Remove_Sema_Blocked(&fifoSemFilledSlots);
			
			//If there is a thread to be woken up
			if(wakeThread != NULL){
				wakeThread->state = ACTIVE;
				wakeThread->lastRunTickCount = tickCount;
			}
		}
		return 1; //Successful get
  } else {
    //failure, Fifo full
    return 0;
  }
  return 0;
};  

// ******** OS_Fifo_Get ************
// Remove one data sample from the Fifo
// Called in foreground, will spin/block if empty
// Inputs:  none
// Outputs: data 
uint32_t OS_Fifo_Get(void){
  //Try to find a filled slot to get data from
  OS_Wait(&fifoSemFilledSlots); //Wait for a filled slot
  OS_Wait(&fifoMutex); //Gain critical access to modify FIFO
  uint32_t data = fifo[fifoEndPtr]; //Get data
  fifoEndPtr++; //Increment end pointer
  if(fifoEndPtr >= FIFO_SIZE){fifoEndPtr = 0;} //Wrap around
  OS_Signal(&fifoMutex);
  OS_Signal(&fifoSemEmptySlots); //Notify producers of empty slot, however since interrupt based producers do not use semaphores to wait, they don't add threads to block list, so in that case, they will just increment semaphore
  return data; //Return data
};

// ******** OS_Fifo_Size ************
// Check the status of the Fifo
// Inputs: none
// Outputs: returns the number of elements in the Fifo
//          greater than zero if a call to OS_Fifo_Get will return right away
//          zero or less than zero if the Fifo is empty 
//          zero or less than zero if a call to OS_Fifo_Get will spin or block
int32_t OS_Fifo_Size(void){
  return fifoSemFilledSlots.Value; //Return FIFO size
};


// ******** OS_MailBox_Init ************
// Initialize communication channel
// Inputs:  none
// Outputs: none
void OS_MailBox_Init(void){
  //Init empty mailbox semaphore
  OS_InitSemaphore(&boxFree, 1);
  OS_InitSemaphore(&dataValid, 0);
};

// ******** OS_MailBox_Send ************
// enter mail into the MailBox
// Inputs:  data to be sent
// Outputs: none
// This function will be called from a foreground thread
// It will spin/block if the MailBox contains data not yet received 
void OS_MailBox_Send(uint32_t data){
  //Write to mailbox first before posting to prevent race condition where recieve access pefore posting signals
  OS_bWait(&boxFree);
  mailbox = data;
  OS_bSignal(&dataValid);
};

// ******** OS_MailBox_Recv ************
// remove mail from the MailBox
// Inputs:  none
// Outputs: data received
// This function will be called from a foreground thread
// It will spin/block if the MailBox is empty 
uint32_t OS_MailBox_Recv(void){
  OS_bWait(&dataValid);
  uint32_t data = mailbox; //Read mailbox
  OS_bSignal(&boxFree);
  return data; //Read and return mailbox data
};

// ******** OS_Time ************
// return the system time 
// Inputs:  none
// Outputs: time in 12.5ns units, 0 to 4294967295
// The time resolution should be less than or equal to 1us, and the precision 32 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_TimeDifference have the same resolution and precision 
uint64_t OS_Time(void){
	//If timer not initialized
	if(timerInit == 0){
		return 0;
	}
	
  //Measure straight into timer hardware
  return ((uint64_t)WTIMER0_TBV_R << 32) | WTIMER0_TAV_R; //This should be timer0A's hardware timer, with upper 32 (TBR) and lower 32 (TAR)
  //The way timer0A is initialized in OS_Init as a countdown timer activating every MS
  //Maximum jitter measurement depends on period, would having a timer with period 1ms be an issue? Have to used timer0 for periodic instead
};

// ******** OS_TimeDifference ************
// Calculates difference between two times
// Inputs:  two times measured with OS_Time
// Outputs: time difference in 12.5ns units 
// The time resolution should be less than or equal to 1us, and the precision at least 12 bits
// It is ok to change the resolution and precision of this function as long as 
//   this function and OS_Time have the same resolution and precision 
uint64_t OS_TimeDifference(uint64_t start, uint64_t stop){
	uint64_t time = start - stop; //Since its a countdown timer
  return time; //No overflow check since timer will likely never overflow
};

// ******** OS_ClearMsTime ************
// sets the system time to zero (solve for Lab 1), and start a periodic interrupt
// Inputs:  period - number of ms between periodic interupts
// Outputs: none
// You are free to change how this works
void OS_ClearMsTime(void){
  MSTime = 0;
};

// ******** OS_MsTime ************
// reads the current time in msec (solve for Lab 1)
// Inputs:  none
// Outputs: time in ms units
// You are free to select the time resolution for this function
// For Labs 2 and beyond, it is ok to make the resolution to match the first call to OS_AddPeriodicThread
uint32_t OS_MsTime(void){
  return MSTime;
};

//Wrapper functions for enabling and disabling interrupts while measuring time
//Unit: Microsecond (8 clock cycles). 
uint64_t longestInterruptDisabledOS = 0;
uint64_t interruptDisabledTimeOS = 0;
uint64_t interruptEnabledTimeOS = 0;
uint64_t startInterruptDisabled = 0;
uint64_t endInterruptDisabled = 0;
uint64_t startInterruptEnabled = 0;
uint64_t endInterruptEnabled = 0;
int interruptDisabled = 0; //Whether current state is interrupted

//Disable interrupt wrapper
void DisableInterruptTime(){
	DisableInterrupts();
	
	//Calculate time
	if(interruptDisabled == 0){
		//End non interruped segment and calculate the amount of time not interrupted
		endInterruptEnabled = OS_Time() / 8;
		interruptEnabledTimeOS = interruptEnabledTimeOS + OS_TimeDifference(startInterruptEnabled, endInterruptEnabled);
		
		//Start interrupted time segment
		startInterruptDisabled = OS_Time() / 8;
		
		interruptDisabled = 1;
	}
}

//Enable interrupt wrapper
void EnableInterruptTime(){
	//Calculate time
	if(interruptDisabled == 1){
		//End interrupt disabled segment and calculate the amount of time interrupted
		endInterruptDisabled = OS_Time() / 8; //Convert to microseconds
		interruptDisabledTimeOS = interruptDisabledTimeOS + OS_TimeDifference(startInterruptDisabled, endInterruptDisabled);
		
		//Calculate max interrupt time
		if(longestInterruptDisabledOS < OS_TimeDifference(startInterruptDisabled, endInterruptDisabled)){
			longestInterruptDisabledOS = OS_TimeDifference(startInterruptDisabled, endInterruptDisabled);
		}
		
		//Start interrupt enabled time segment
		startInterruptEnabled = OS_Time() / 8;
		
		interruptDisabled = 0;
	}
	EnableInterrupts();
}

//Disable interrupt wrapper
void EndCriticalTime(long status){
	//Calculate time
	if(interruptDisabled == 1){
		//End interrupt disabled segment and calculate the amount of time interrupted
		endInterruptDisabled = OS_Time() / 8; //Convert to microseconds
		interruptDisabledTimeOS = interruptDisabledTimeOS + OS_TimeDifference(startInterruptDisabled, endInterruptDisabled);
		
		//Calculate max interrupt time
		if(longestInterruptDisabledOS < OS_TimeDifference(startInterruptDisabled, endInterruptDisabled)){
			longestInterruptDisabledOS = OS_TimeDifference(startInterruptDisabled, endInterruptDisabled);
		}
		
		//Start interrupt enabled time segment
		startInterruptEnabled = OS_Time() / 8;
		
		interruptDisabled = 0;
	}
	EndCritical(status);
}

//Enable interrupt wrapper
long StartCriticalTime(){
	long status = StartCritical();
	
	//Calculate time
	if(interruptDisabled == 0){
		//End non interruped segment and calculate the amount of time not interrupted
		endInterruptEnabled = OS_Time() / 8;
		interruptEnabledTimeOS = interruptEnabledTimeOS + OS_TimeDifference(startInterruptEnabled, endInterruptEnabled);
		
		//Start interrupted time segment
		startInterruptDisabled = OS_Time() / 8;
		
		interruptDisabled = 1;
	}
	return status;
}

//Getters for info
uint64_t percentInterruptDisabled(){
	return (interruptDisabledTimeOS * 100) / (interruptDisabledTimeOS + interruptEnabledTimeOS);
}

uint64_t longestInterruptDisabled(){
	return longestInterruptDisabledOS;
}

//******** OS_Launch *************** 
// start the scheduler, enable interrupts
// Inputs: number of 12.5ns clock cycles for each time slice
//         you may select the units of this parameter
// Outputs: none (does not return)
// In Lab 2, you can ignore the theTimeSlice field
// In Lab 3, you should implement the user-defined TimeSlice field
// It is ok to limit the range of theTimeSlice to match the 24-bit SysTick
void OS_Launch(uint32_t theTimeSlice){
  //Init scheduler here
	//Set timeslice and Initialize systick (Context switching)
  SysTick_Init(theTimeSlice); // period determined by theTimeSlice
	timeSlice = theTimeSlice;
	
  //Just select the first thread in the pool for now
  runPt = &threadPool[0];
  runPt->state = RUNNING;
  EnableInterruptTime();
  StartOS();
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
  char ch = UART_InChar();  // receive from keyboard
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

