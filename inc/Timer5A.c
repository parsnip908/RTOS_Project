// Timer5A.c
// Runs on LM4F120/TM4C123
// Use Timer5 in 32-bit periodic mode to request interrupts at a periodic rate
// Daniel Valvano
// Jan 1, 2020

/* This example accompanies the book
   "Embedded Systems: Real Time Interfacing to Arm Cortex M Microcontrollers",
   ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2020
  Program 7.5, example 7.6

 Copyright 2020 by Jonathan W. Valvano, valvano@mail.utexas.edu
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
#include "../inc/tm4c123gh6pm.h"

void (*PeriodicTask5)(void);   // user function

// ***************** Timer5A_Init ****************
// Activate Timer5 interrupts to run user task periodically
// Inputs:  task is a pointer to a user function
//          period in units (1/clockfreq)
//          priority 0 (highest) to 7 (lowest)
// Outputs: none
void Timer5A_Init(void(*task)(void), uint32_t period, uint32_t priority){
  SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R5;   // 0) activate TIMER5
  PeriodicTask5 = task;         // user function

  // Wait for the Timer 0 to be ready
  while((SYSCTL_PRTIMER_R & SYSCTL_PRTIMER_R5) == 0) {}

  TIMER5_CTL_R = ~TIMER_CTL_TAEN;    // 1) disable TIMER5A during setup
  TIMER5_CFG_R = TIMER_CFG_32_BIT_TIMER;    // 2) configure for 32-bit mode
  TIMER5_TAMR_R = TIMER_TAMR_TAMR_PERIOD;   // 3) configure for periodic mode, default down-count settings
  TIMER5_TAILR_R = period-1;          // 4) reload value
  TIMER5_TAPR_R = 0;                  // 5) bus clock resolution (no prescale)
  TIMER5_ICR_R = TIMER_ICR_TATOCINT;  // 6) clear TIMER3A timeout flag // is this necessary?
  TIMER5_IMR_R = TIMER_IMR_TATOIM;    // 7) enable interrupt on timer5a
  
  // page 105 and 136
  NVIC_PRI23_R = (NVIC_PRI23_R&0xFFFFFF00)|(priority<<5); // priority

  // vector number 108, interrupt number 92
  NVIC_EN2_R = 1U<<(92%32);      // 9) enable interrupt 70 in NVIC
  TIMER5_CTL_R = TIMER_CTL_TAEN;    // 10) enable TIMER5A
}

void Timer5A_Handler(void){
  TIMER5_ICR_R = TIMER_ICR_TATOCINT;// acknowledge TIMER5A timeout
  (*PeriodicTask5)();               // execute user task
}

void Timer5A_Stop(void){
  NVIC_DIS2_R = 0x00000040;        // 9) disable interrupt 70 in NVIC
  TIMER5_CTL_R = 0x00000000;       // 10) disable timer5A
}
