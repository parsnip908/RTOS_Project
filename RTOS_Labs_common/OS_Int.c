

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../RTOS_Labs_common/OS.h"
#include "../RTOS_Labs_common/osasm.h"
#include "../inc/driverlib/gpio.h"
#include "../inc/driverlib/timer.h"
#include "../inc/driverlib/interrupt.h"

// timer defines
#define TIMER0_BASE 0x40030000
#define GET_TIMER_BASE(x) (TIMER0_BASE | (x << 12))

typedef void(*task_t)(void);

task_t PeriodicTasks[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
int numPeriodicTasks = 1;

// Jitter Measurements 
// jitterStat_t jitterStats[6];
uint32_t periods[6];
uint32_t maxJitter[6];
// uint32_t const JitterSize=JITTERSIZE;
uint32_t jitterHistogram[6][JITTERSIZE] = {0};
uint32_t totalJitter[6] = {0};
uint32_t badJitterCount[6] = {0};

uint64_t lastTime[6] = {0};


void OS_SetupTimer(int timerNum, uint32_t period, uint32_t priority)
{
    SYSCTL_RCGCTIMER_R |=  1 << timerNum;
    uint32_t baseaddr = GET_TIMER_BASE(timerNum);
    wait_cycles(3);

    TimerDisable(baseaddr, TIMER_BOTH);
    TimerClockSourceSet(baseaddr, TIMER_CLOCK_SYSTEM);
    TimerConfigure(baseaddr, TIMER_CFG_PERIODIC);
    TimerLoadSet(baseaddr, TIMER_BOTH, period);
    TimerIntEnable(baseaddr, TIMER_TIMA_TIMEOUT);

    int intNum = 35;
    if(timerNum == 1)
        intNum = 37;
    else if(timerNum == 2)
        intNum = 39;
    else if(timerNum == 3)
        intNum = 51;
    else if(timerNum == 4)
        intNum = 86;
    else if(timerNum == 5)
        intNum = 108;
    IntPrioritySet(intNum, (priority & 7) << 5);
    IntEnable(intNum);
    TimerEnable(baseaddr, TIMER_BOTH);
}

void OS_Timer_Hook(int timerNum)
{
    uint64_t thisTime = OS_Time(); // get time immediately
    if(timerNum > 5)
    {
        printf("ERROR: OS_Timer_Hook bad timer number %d\n", timerNum);
        return;
    }

    //measure jitter. skip first iteration
    // jitterStat_t *curr_jitterStat = &jitterStats[timerNum];
    if(lastTime[timerNum])
    {
        uint64_t diff = OS_TimeDifference(lastTime[timerNum],thisTime);
        uint32_t jitter;
        if(diff > periods[timerNum])
          jitter = (diff-periods[timerNum]+4)/8;  // in 0.1 usec
        else
          jitter = (periods[timerNum]-diff+4)/8;  // in 0.1 usec

        if(jitter > maxJitter[timerNum]){
          maxJitter[timerNum] = jitter; // in usec
        }

        totalJitter[timerNum] += jitter;
        if(jitter >= JITTERSIZE){
          jitter = JITTERSIZE-1;
          badJitterCount[timerNum]++;
        }
        jitterHistogram[timerNum][jitter]++; 
    }
    lastTime[timerNum] = thisTime;

    //clear interupt
    uint32_t baseaddr = GET_TIMER_BASE(timerNum);
    TimerIntClear(baseaddr, TIMER_TIMA_TIMEOUT);

    if(PeriodicTasks[timerNum] == NULL)
    {
        printf("ERROR: OS_Timer_Hook no task for timer %d\n", timerNum);
        return;
    }
    PeriodicTasks[timerNum](); // run user task
    // EnableInterrupts();
}

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
int OS_AddPeriodicThread(task_t task, uint32_t period, uint32_t priority)
{
    DisableInterrupts();
    intDisableTimerStart();

    if(numPeriodicTasks > 5)
        return 0;
    PeriodicTasks[numPeriodicTasks] = task;
    periods[numPeriodicTasks] = period;
    OS_SetupTimer(numPeriodicTasks++, period, priority);

    intDisableTimerEnd();
    EnableInterrupts();

    return 1; // replace this line with solution
};


#define GPIOPORTF_BASE 0x40025000

task_t SW1Task = NULL; //s[4] = {NULL, NULL, NULL, NULL};
task_t SW2Task = NULL; //s[4] = {NULL, NULL, NULL, NULL};
bool SW1Init = false;
bool SW2Init = false;
uint64_t SW1Time = 0;
uint64_t SW2Time = 0;
bool SW1Pressed = false;
bool SW2Pressed = false;


void OS_SetupGPIOF(void)
{
    DisableInterrupts();
    intDisableTimerStart();

    // printf("in gpio setup\n");
    SYSCTL_RCGCGPIO_R |= 0x00000020; // (a) activate clock for port F
    while((SYSCTL_PRGPIO_R & 0x00000020) == 0){};

    GPIOUnlockPin(GPIOPORTF_BASE, 0x11);
    GPIOADCTriggerDisable(GPIOPORTF_BASE, 0x11);
    GPIODMATriggerDisable(GPIOPORTF_BASE, 0x11);
    GPIODirModeSet(GPIOPORTF_BASE, 0x11, GPIO_DIR_MODE_IN);
    GPIOPadConfigSet(GPIOPORTF_BASE, 0x11, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    // GPIOPinTypeGPIOInput(GPIOPORTF_BASE, 0x11);
    GPIOIntTypeSet(GPIOPORTF_BASE, 0x11, GPIO_BOTH_EDGES);
    GPIOIntDisable(GPIOPORTF_BASE, 0x1ff);
    GPIO_PORTF_AFSEL_R &= ~0x11;  //     disable alt funct on PF4

    GPIO_PORTF_AMSEL_R = 0;       //     disable analog functionality on PF

    GPIOIntEnable(GPIOPORTF_BASE, GPIO_INT_PIN_0 |  GPIO_INT_PIN_4);
    IntPrioritySet(46, 7 << 5);
    IntDisable(46);

    intDisableTimerEnd();
    EnableInterrupts();
}

/*----------------------------------------------------------------------------
  PF1 Interrupt Handler
 *----------------------------------------------------------------------------*/
void GPIOPortF_Hook(void)
{
    // PD2 ^= 0x04;       // heartbeat
    // printf("in gpio %x\n", GPIOPinRead(GPIOPORTF_BASE, 0xff));
    GPIOIntClear(GPIOPORTF_BASE, GPIO_INT_PIN_0 |  GPIO_INT_PIN_4);
    uint32_t pinValue = GPIOPinRead(GPIOPORTF_BASE, 0x11);
    bool SW1Val = (pinValue & 0x10) ? true : false;
    bool SW2Val = (pinValue & 0x01) ? true : false;
    uint64_t currTime = OS_Time();
    // Switch 1 tree
    if(currTime > SW1Time + (TIME_1MS))
    {
        if(SW1Val && SW1Pressed) //Switch released
        {
            SW1Time = currTime;
            SW1Pressed = false;
            // printf("sw1 released\n");
        }
        else if(!SW1Val && !SW1Pressed) //switch pressed
        {
            SW1Time = currTime;
            SW1Pressed = true;
            // printf("sw1 pressed\n");
            if(SW1Task)
                SW1Task();
        }
        else // state is the same
        {
            //TODO: antthing here?
        }

    }

    if(currTime > SW2Time + (TIME_1MS))
    {
        if(SW2Val && SW2Pressed) //Switch released
        {
            SW2Time = currTime;
            SW2Pressed = false;
            // printf("sw2 released\n");
        }
        else if(!SW2Val && !SW2Pressed) //switch pressed
        {
            SW2Time = currTime;
            SW2Pressed = true;
            // printf("sw2 pressed\n");
            if(SW2Task)
                SW2Task();
        }
        else // state is the same
        {
            //TODO: antthing here?
        }

    }


    // task_t *taskList = NULL;
    // if(sw1)
    //     taskList = SW1Tasks;
    // if(sw2)
    //     taskList = SW2Tasks;

    // //TODO: disable systick
    // for (int i = 0; i < 4; ++i)
    // {
    //     if(taskList[i] == NULL) 
    //         continue;

    // }
    // PD2 ^= 0x04;       // heartbeat
    // EnableInterrupts();
}

//******** OS_AddSW1Task *************** 
//******** OS_AddSW2Task *************** 
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
int OS_AddSW1Task(void(*task)(void), uint32_t priority)
{
    DisableInterrupts();
    intDisableTimerStart();

    SW1Task = task;
    IntPrioritySet(46, (priority & 7) << 5);
    IntEnable(46);

    intDisableTimerEnd();
    EnableInterrupts();
    return 1;
};

int OS_AddSW2Task(void(*task)(void), uint32_t priority)
{
    DisableInterrupts();
    intDisableTimerStart();

    SW2Task = task;
    IntPrioritySet(46, (priority & 7) << 5);
    IntEnable(46);

    intDisableTimerEnd();
    EnableInterrupts();
    return 1;
};


