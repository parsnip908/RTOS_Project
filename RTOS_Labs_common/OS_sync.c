#include <stdint.h>
#include <stdio.h>
// #include <string.h>
// #include <stdlib.h>
#include <stdbool.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/PLL.h"
#include "../inc/LaunchPad.h"
// #include "../inc/Timer4A.h"
// #include "../inc/WTimer0A.h"
// #include "../inc/ADCT0ATrigger.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/osasm.h"
#include "../RTOS_Labs_common/ST7735.h"
// #include "../RTOS_Labs_common/UART0int.h"
// #include "../RTOS_Labs_common/eFile.h"
#include "../RTOS_Labs_common/queue.h"
// #include "../RTOS_Labs_common/ST7735.h"
// #include "../inc/driverlib/timer.h"
// #include "../inc/driverlib/interrupt.h"
// #include "../inc/driverlib/systick.h"

extern TCB_t *RunPt;
// ******** OS_InitSemaphore ************
// initialize semaphore 
// input:  pointer to a semaphore
// output: none
void OS_InitSemaphore(sem_t *new_sem, int32_t value){
  // put Lab 2 (and beyond) solution here
  // sem_t new_sem = malloc(sizeof(struct semaphore));
  // if(new_sem == NULL) return NULL;

  queue_create(&(new_sem->blocked_queue));
  // if(new_sem->blocked_queue == NULL) {
  //   free(new_sem); //need to free the struct if the queue alloc failed.
  //   return NULL;
  // }

  new_sem->lock = value;
  // return new_sem;

}

// ******** OS_Wait ************
// decrement semaphore 
// Lab2 spinlock
// Lab3 block if less than zero
// input:  pointer to a counting semaphore
// output: none
int OS_Wait(sem_t *sem)
{
  // put Lab 2 (and beyond) solution here
  if(sem == NULL) return -1;

  while(1) {
    DisableInterrupts();
    intDisableTimerStart();
    //attempt lock
    if(sem->lock > 0) {
      //take lock
      sem->lock--;
      intDisableTimerEnd();
      EnableInterrupts();
      return 0;
    }
    //block self
    queue_enqueue(&sem->blocked_queue, RunPt);
    OS_Block();
    //loop and try to take the lock again
  }
}

// ******** OS_Signal ************
// increment semaphore 
// Lab2 spinlock
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a counting semaphore
// output: none
int OS_Signal(sem_t *sem)
{
  // put Lab 2 (and beyond) solution here
  if(sem == NULL) return -1;
  // DisableInterrupts();
  // intDisableTimerStart();


  sem->lock++;
  //unblock one thread if there is one
  if(queue_length(&sem->blocked_queue)) {
    void *tcb_to_unblock;
    queue_dequeue(&sem->blocked_queue, (void**) &tcb_to_unblock);
    OS_Unblock(tcb_to_unblock);
  }
  // intDisableTimerEnd();
  // EnableInterrupts();
  return 0;
}

// ******** OS_bWait ************
// Lab2 spinlock, set to 0
// Lab3 block if less than zero
// input:  pointer to a binary semaphore
// output: none
int OS_bWait(sem_t *sem){
  // put Lab 2 (and beyond) solution here
  if(sem == NULL) return -1;

  while(1) {
    DisableInterrupts();
    intDisableTimerStart();


    //attempt lock
    if(sem->lock > 0) {
      //take lock
      sem->lock = 0;
      intDisableTimerEnd();
      EnableInterrupts();
      return 0;
    }
    //block self
    queue_enqueue(&sem->blocked_queue, RunPt);
    OS_Block();
    //loop and try to take the lock again
  }
}; 

// ******** OS_bSignal ************
// Lab2 spinlock, set to 1
// Lab3 wakeup blocked thread if appropriate 
// input:  pointer to a binary semaphore
// output: none
int OS_bSignal(sem_t *sem){
  if(sem == NULL) return -1;
  // DisableInterrupts();
  // intDisableTimerStart();


  sem->lock = 1;
  //unblock one thread if there is one
  if(queue_length(&sem->blocked_queue)) {
    void *tcb_to_unblock;
    queue_dequeue(&sem->blocked_queue, (void**) &tcb_to_unblock);
    OS_Unblock(tcb_to_unblock);
  }

  // intDisableTimerEnd();
  // EnableInterrupts();
  return 0;
};


#define FIFO_MAX_SIZE 16
uint32_t fifo[FIFO_MAX_SIZE];
sem_t fifoSem;
volatile int fifoHead;

// ******** OS_Fifo_Init ************
// Initialize the Fifo to be empty
// Inputs: size
// Outputs: none 
// In Lab 2, you can ignore the size field
// In Lab 3, you should implement the user-defined fifo size
// In Lab 3, you can put whatever restrictions you want on size
//    e.g., 4 to 64 elements
//    e.g., must be a power of 2,4,8,16,32,64,128
void OS_Fifo_Init(uint32_t size)
{
  fifoHead = 0;
  OS_InitSemaphore(&fifoSem,0); 
};

// ******** OS_Fifo_Put ************
// Enter one data sample into the Fifo
// Called from the background, so no waiting 
// Inputs:  data
// Outputs: true if data is properly saved,
//          false if data not saved, because it was full
// Since this is called by interrupt handlers 
//  this function can not disable or enable interrupts
int OS_Fifo_Put(uint32_t data)
{
  if(fifoSem.lock >= FIFO_MAX_SIZE)
    return false;
  int fifoSize = fifoSem.lock;
  fifo[(fifoHead+(fifoSize++)) % FIFO_MAX_SIZE] = data;
  OS_Signal(&fifoSem);
  return true;
};  

// ******** OS_Fifo_Get ************
// Remove one data sample from the Fifo
// Called in foreground, will spin/block if empty
// Inputs:  none
// Outputs: data 
uint32_t OS_Fifo_Get(void)
{
  OS_Wait(&fifoSem);
  fifoHead = (fifoHead +1) % FIFO_MAX_SIZE;
  uint32_t data = fifo[fifoHead];
  return data;
};

// ******** OS_Fifo_Size ************
// Check the status of the Fifo
// Inputs: none
// Outputs: returns the number of elements in the Fifo
//          greater than zero if a call to OS_Fifo_Get will return right away
//          zero or less than zero if the Fifo is empty 
//          zero or less than zero if a call to OS_Fifo_Get will spin or block
int32_t OS_Fifo_Size(void)
{
  return fifoSem.lock;
};


uint32_t mailboxVal;
sem_t mailboxFlagFull;
sem_t mailboxFlagEmpty;
// ******** OS_MailBox_Init ************
// Initialize communication channel
// Inputs:  none
// Outputs: none
void OS_MailBox_Init(void)
{
  OS_InitSemaphore(&mailboxFlagEmpty, 1);
  OS_InitSemaphore(&mailboxFlagFull, 0);
  // mailboxFlag = false;
};

// ******** OS_MailBox_Send ************
// enter mail into the MailBox
// Inputs:  data to be sent
// Outputs: none
// This function will be called from a foreground thread
// It will spin/block if the MailBox contains data not yet received 
void OS_MailBox_Send(uint32_t data)
{
  OS_bWait(&mailboxFlagEmpty);
  mailboxVal = data;
  OS_bSignal(&mailboxFlagFull);
};

// ******** OS_MailBox_Recv ************
// remove mail from the MailBox
// Inputs:  none
// Outputs: data received
// This function will be called from a foreground thread
// It will spin/block if the MailBox is empty 
uint32_t OS_MailBox_Recv(void)
{
  OS_bWait(&mailboxFlagFull);
  uint32_t data = mailboxVal;
  OS_bSignal(&mailboxFlagEmpty);
  return data;
};
