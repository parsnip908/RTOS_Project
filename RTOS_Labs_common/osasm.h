#ifndef __OSASM_H
#define __OSASM_H

void StartOS(void);

void ContextSwitch(void);

void SysTick_Handler(void);

void PendSV_Handler(void);

void SVC_Handler(void);

void wait_cycles(uint32_t cycles);

#endif