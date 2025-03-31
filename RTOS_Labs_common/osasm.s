;/*****************************************************************************/
;/* OSasm.s: low-level OS commands, written in assembly                       */
;/* derived from uCOS-II                                                      */
;/*****************************************************************************/
;Jonathan Valvano, OS Lab2/3/4/5, 1/12/20
;Students will implement these functions as part of EE445M/EE380L.12 Lab

        AREA |.text|, CODE, READONLY, ALIGN=2
        THUMB
        REQUIRE8
        PRESERVE8

        EXTERN  runPt            ; currently running thread
        EXTERN  NextRunPt        ; next thread selected by scheduler

        EXPORT  StartOS
        EXPORT  ContextSwitch
        EXPORT  PendSV_Handler
        EXPORT  SVC_Handler

NVIC_INT_CTRL   EQU     0xE000ED04                              ; Interrupt control state register.
NVIC_SYSPRI14   EQU     0xE000ED22                              ; PendSV priority register (position 14).
NVIC_SYSPRI15   EQU     0xE000ED23                              ; Systick priority register (position 15).
NVIC_LEVEL14    EQU           0xEF                              ; Systick priority value (second lowest).
NVIC_LEVEL15    EQU           0xFF                              ; PendSV priority value (lowest).
NVIC_PENDSVSET  EQU     0x10000000                              ; Value to trigger PendSV exception.


StartOS
; put your code here
; this operates on the main processor's stack/regs (instead of those of TCB thread objects)
; OS_Launch() puts first TCB pointer in R0
    LDR     R0, =runPt  ; =RunPt is the addr of the RunPt
    LDR     R1, [R0]    ; R1 = value of RunPt
    LDR     SP, [R1]    ; SP is first element in TCB (RunPt->SP)

    POP     {R4-R11}    ; restore regs r4-11
    POP     {R0-R3,R12} ; restore regs r0-3
    ADD     SP,SP,#4    ; discard LR from initial stack
    POP     {LR}        ; load initial function pointer into LR (from PC idx)
    ADD     SP,SP,#4    ; discard PSR
    
    CPSIE   I           ; enable interrupts
    BX      LR          ; start first thread

OSStartHang
    B       OSStartHang        ; Should never get here


;********************************************************************************************************
;                               PERFORM A CONTEXT SWITCH (From task level)
;                                           void ContextSwitch(void)
;
; Note(s) : 1) ContextSwitch() is called when OS wants to perform a task context switch.  This function
;              triggers the PendSV exception which is where the real work is done.
;********************************************************************************************************

ContextSwitch
; This should perform this action:  NVIC_INT_CTRL_R = NVIC_INT_CTRL_PEND_SV;
; This sets the ICSR register (Address defined by NVIC_INT_CTRL_R) with defined constant for PendSV bit (NVIC_INT_CTRL_PEND_SV)
; Activating this should cause pendSV interrupt to trigger, which will do the REAL context switch
    PUSH    {R0,R1}
    LDR     R0, =NVIC_INT_CTRL          ; Load address of ICSR
    LDR     R1, =NVIC_PENDSVSET         ; Load PendSV value
    STR     R1, [R0]                    ; Set PendSV value to ICSR address
    POP     {R0,R1}                     ; Save R0, R1, same as before, not sure if needed though 
    BX      LR                          ; Return

    

;********************************************************************************************************
;                                         HANDLE PendSV EXCEPTION
;                                     void OS_CPU_PendSVHandler(void)
;
; Note(s) : 1) PendSV is used to cause a context switch.  This is a recommended method for performing
;              context switches with Cortex-M.  This is because the Cortex-M3 auto-saves half of the
;              processor context on any exception, and restores same on return from exception.  So only
;              saving of R4-R11 is required and fixing up the stack pointers.  Using the PendSV exception
;              this way means that context saving and restoring is identical whether it is initiated from
;              a thread or occurs due to an interrupt or exception.
;
;           2) Pseudo-code is:
;              a) Get the process SP, if 0 then skip (goto d) the saving part (first context switch);
;              b) Save remaining regs r4-r11 on process stack;
;              c) Save the process SP in its TCB, OSTCBCur->OSTCBStkPtr = SP;
;              d) Call OSTaskSwHook();
;              e) Get current high priority, OSPrioCur = OSPrioHighRdy;
;              f) Get current ready thread TCB, OSTCBCur = OSTCBHighRdy;
;              g) Get new process SP from TCB, SP = OSTCBHighRdy->OSTCBStkPtr;
;              h) Restore R4-R11 from new process stack;
;              i) Perform exception return which will restore remaining context.
;
;           3) On entry into PendSV handler:
;              a) The following have been saved on the process stack (by processor):
;                 xPSR, PC, LR, R12, R0-R3
;              b) Processor mode is switched to Handler mode (from Thread mode)
;              c) Stack is Main stack (switched from Process stack)
;              d) OSTCBCur      points to the OS_TCB of the task to suspend
;                 OSTCBHighRdy  points to the OS_TCB of the task to resume
;
;           4) Since PendSV is set to lowest priority in the system (by OSStartHighRdy() above), we
;              know that it will only be run when no other exception or interrupt is active, and
;              therefore safe to assume that context being switched out was using the process stack (PSP).
;********************************************************************************************************

PendSV_Handler
; put your code here
    CPSID   I               ; Make atomic

    PUSH    {R4-R11}       ; Push registers (Everything else (including LR) automatically pushed)
    LDR     R0, =runPt         ; get address of RunPt (Pointer to current running thread's TCB)            
    LDR     R1, [R0]           ; get value of RunPt (pointer that points to the TCB)
    STR     SP, [R1]           ; save current thread SP back to the TCB via RunPt (0 offset since SP is the first param of TCB)
    ; implement round robin scheduling
    LDR     R1, [R1, #4]       ; point R1 to the next TCB (offset by 4 since next TCB is the second param)
    LDR     SP, [R1]           ; load stack pointer from next TCB
    STR     R1, [R0]           ; store new TCB's pointer to RunPtr (R0 is RunPt variable, R1 is new TCB)
    ; start next thread
    POP     {R4-R11}        ; Pop registers (Everything else automatically popped)
    CPSIE   I               ; enable interrupts
    BX      LR              ; Exception return will restore remaining context    
    

;********************************************************************************************************
;                                         HANDLE SVC EXCEPTION
;                                     void OS_CPU_SVCHandler(void)
;
; Note(s) : SVC is a software-triggered exception to make OS kernel calls from user land. 
;           The function ID to call is encoded in the instruction itself, the location of which can be
;           found relative to the return address saved on the stack on exception entry.
;           Function-call paramters in R0..R3 are also auto-saved on stack on exception entry.
;********************************************************************************************************

        IMPORT    OS_Id
        IMPORT    OS_Kill
        IMPORT    OS_Sleep
        IMPORT    OS_Time
        IMPORT    OS_AddThread

SVC_Handler
    LDR     R12, [SP,#24]      ; Return address
    LDRH    R12, [R12,#-2]     ; SVC instruction is 2 bytes
    BIC     R12, R12, #0xFF00  ; Extract ID in R12. This is the opcode passed during SVC call, look below
    LDM     SP, {R0-R3}        ; Get any parameters

    PUSH    {R4,LR}
    CMP     R12, #4 			; Checks the SVC opcode obtained earlier
    BHI     MERGE 				; DEFAULT, triggered if opcode is greater than 4, invalid
    ADR     R4, SWITCH			; If opcode is valid, load the address of switch table into R4
    LDR     PC, [R4, R12, LSL #2] ;Load PC depending on switch table (R4) and (R12) as offset

	ALIGN 4 ; 4 byte align (p2align 2: 2^2 align)
	
SWITCH
    DCD   OS_ID+1         ; #0 convert .word to DCD, both define 32 bit value
    DCD   OS_KILL+1       ; #1
    DCD   OS_SLEEP+1      ; #2
    DCD   OS_TIME+1       ; #3
    DCD   OS_ADDTHREAD+1  ; #4
    ALIGN

OS_ID
    BL  OS_Id
    B   MERGE
OS_KILL
    BL  OS_Kill
    B   MERGE
OS_SLEEP
    BL  OS_Sleep
    B   MERGE
OS_TIME
    BL  OS_Time
    B   MERGE
OS_ADDTHREAD
    BL  OS_AddThread
    B   MERGE
DEFAULT
    ; TODO

MERGE
    POP     {R4,LR}
    STR     R0,[SP]      ; Store return value
    BX      LR           ; Return from exception



;*****************************************************************************
;*                         OS System Calls                                   *
;*****************************************************************************

SVC_OS_Id
	SVC		#0 ; Calls SVC with #0 as opcode (Trigger the handler). Opcode is then passed into R12 in handler
	BX		LR

SVC_OS_Kill
	SVC		#1
	BX		LR

SVC_OS_Sleep
	SVC		#2
	BX		LR

SVC_OS_Time
	SVC		#3
	BX		LR

SVC_OS_AddThread
	SVC		#4
	BX		LR



    ALIGN
    END
