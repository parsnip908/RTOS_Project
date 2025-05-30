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

        EXTERN  RunPt            ; currently running thread
        IMPORT OS_getNext
        IMPORT OS_SysTick_Hook

        EXPORT  StartOS
        EXPORT  ContextSwitch
        EXPORT  PendSV_Handler
        EXPORT  SysTick_Handler
        EXPORT  SVC_Handler
        EXPORT  wait_cycles
        EXPORT  flog2

        IMPORT GPIOPortF_Hook
        IMPORT OS_Timer_Hook

        EXPORT GPIOPortF_Handler
        ; EXPORT Timer0A_Handler
        ; EXPORT Timer0B_Handler
        EXPORT Timer1A_Handler
        EXPORT Timer1B_Handler
        EXPORT Timer2A_Handler
        EXPORT Timer2B_Handler
        EXPORT Timer3A_Handler
        EXPORT Timer3B_Handler
        EXPORT Timer4A_Handler
        EXPORT Timer4B_Handler
        EXPORT Timer5A_Handler
        EXPORT Timer5B_Handler


; .def USER_CLAIM_TIMER0

NVIC_INT_CTRL   EQU     0xE000ED04                              ; Interrupt control state register.
NVIC_SYSPRI14   EQU     0xE000ED22                              ; PendSV priority register (position 14).
NVIC_SYSPRI15   EQU     0xE000ED23                              ; Systick priority register (position 15).
NVIC_LEVEL14    EQU           0xEF                              ; Systick priority value (second lowest).
NVIC_LEVEL15    EQU           0xFF                              ; PendSV priority value (lowest).
NVIC_PENDSVSET  EQU     0x10000000                              ; Value to trigger PendSV exception.

wait_cycles
    cmp     r0, #0
    bxeq    lr
waitloop
    subs    r0, r0, #1
    bne     waitloop
    bx      lr

flog2
    CMP     R0, #0            ; Check if input is zero
    BEQ     flog2_error       ; If zero, jump to error handler
    CLZ     R1, R0            ; R1 = count leading zeros
    RSBS    R0, R1, #31       ; R0 = 31 - R1 (RSBS = Reverse Subtract and Set Flags)
    BX      LR                ; Return
flog2_error
    MOV     R0, #-1
    ; LSL     R0, R0, #24       ; Move 0xFF to top byte (0xFF000000)
    ; ORR     R0, R0, #0xFFFFFF ; Combine to 0xFFFFFFFF
    BX      LR

StartOS
; put your code here
    MRS R0, CONTROL
    ORR R0, R0, #2 ; Set bit 1 of control register to 1 (SPSEL = 1, use PSP)
    BIC R0, R0, #1 ; Clear bit 0 of control register (nPRIV = 0, privileged)
    MSR CONTROL, R0
    ISB
    LDR R0, =RunPt ; currently running thread
    LDR R1, [R0] ; R1 = value of RunPt
    LDR SP, [R1] ; new thread SP; SP = RunPt->sp;
    POP {R4-R11} ; restore regs r4-11
    POP {R0-R3} ; restore regs r0-3
    POP {R12}
    ADD SP, SP, #4 ; discard LR from initial stack
    POP {LR} ; start location
    ADD SP, SP, #4 ; discard PSR
    CPSIE I ; Enable interrupts at processor level
    BX LR ; start first thread

OSStartHang
    B       OSStartHang        ; Should never get here


;********************************************************************************************************
;                               PERFORM A CONTEXT SWITCH (From task level)
;                                           void ContextSwitch(void)
;
; Note(s) : 1) ContextSwitch() is called when OS wants to perform a task context switch.  This function
;              triggers the PendSV exception which is where the real work is done.
;********************************************************************************************************
SysTick_Handler
    CPSID I ; 2) Prevent interrupt during switch
    PUSH {LR}
    BL OS_SysTick_Hook
    POP {LR}
    ; B ContextSwitch

PendSV_Handler
ContextSwitch
; edit this code
    ; PUSH  {R0-R12,LR}
    ; assuming came from interuppt (systick or pendsv)
    
    CPSID I ; 2) Prevent interrupt during switch
    TST LR, #4 ; Test bit 2 of EXC_RETURN
    ITE EQ
    MRSEQ R2, MSP ; if 0, stacking used MSP, copy to R2
    MRSNE R2, PSP ; if 1, stacking used PSP, copy to R2
    ; MRS R2, PSP ; Get current process stack pointer value
    STMFD R2!,{R4-R11} ; Save R4 to R11 in task stack (8 regs)
    ; PUSH {R4-R11} ; 3) Save remaining regs r4-11
    LDR R0, =RunPt ; 4) R0=pointer to RunPt, old thread
    LDR R1, [R0] ; R1 = RunPt
    STR R2, [R1] ; 5) Save SP into TCB
    PUSH {LR}
    BL OS_getNext
    POP {LR}

    MRS R1, CONTROL         ; Read CONTROL register
    LDRB R2, [R0, #28] ; load access byte
    CMP R2, #1
    ITE EQ
    ORREQ R1, R1, #1 ; Set bit 0 of control register to 1 (nPRIV = 1, unprivileged)
    BICNE R1, R1, #1 ; Clear bit 0 of control register (nPRIV = 0, privileged)
    MSR CONTROL, R1  ; Write value back to CONTROL register
    ISB              ; Instruction Sync Barrier. Make sure this is done before returning

    ;LDR R1, [R1,#4] ; 6) R1 = RunPt->next
    ;STR R1, [R0] ; RunPt = R1
    ;LDR SP, [R1] ; 7) new thread SP; SP = RunPt->sp;
    ; LDR SP, [R0] ; function returned runpt in R0;
    ; POP {R4-R11} ; 8) restore regs r4-11

    LDR R2, [R0] ; function returned runpt in R0;
    LDMFD R2!, {R4-R11} ; 8) restore regs r4-11
    TST LR, #4 ; Test bit 2 of EXC_RETURN
    ITE EQ
    MSREQ MSP, R2 ; if 0, stacking used MSP, copy from R2
    MSRNE PSP, R2 ; if 1, stacking used PSP, copy from R2
    ; MSR PSP, R2
    CPSIE I ; 9) tasks run with interrupts enabled
    BX LR ; 10) restore R0-R3,R12,LR,PC,PSR
    
    
    ;BX      LR
    

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
; SysTick_Handler
;     PUSH {LR}
;     BL OS_SysTick_Hook
;     POP {LR}
;     B ContextSwitch

; PendSV_Handler
; ; put your code here
;     ; goto context switch without doing anything. 
;     ; we are here for the auto register saving feature only
;     B ContextSwitch
;     ; not linked so will not return from context switch. will go directly back to original
;     BX      LR                 ; Exception return will restore remaining context   
    

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
        IMPORT    ST7735_Message

SVC_Handler
    TST LR, #4 ; Test bit 2 of EXC_RETURN
    ITE EQ
    MRSEQ R0, MSP ; if 0, stacking used MSP, copy to R0
    MRSNE R0, PSP ; if 1, stacking used PSP, copy to R0
    ; MRS R2, PSP ; Get current process stack pointer value

    LDR IP,[R0, #24] ; Return address
    LDRH IP,[IP, #-2] ; SVC instruction is 2 bytes
    BIC IP, #0xFF00 ; Extract ID in R12
    LDM R0,{R0-R3} ; Get any parameters

    PUSH {LR}
    LDR LR, =SVC_Return

    ; BL OS_xxx ; Call OS routine by ID…
    CMP IP, #0
    BEQ OS_Id
    CMP IP, #1
    BEQ OS_Kill
    CMP IP, #2
    BEQ OS_Sleep
    CMP IP, #3
    BEQ OS_Time
    CMP IP, #4
    BEQ OS_AddThread
    CMP IP, #5
    BEQ ST7735_Message

SVC_Return
    POP {LR}

    TST LR, #4 ; Test bit 2 of EXC_RETURN
    ITE EQ
    MRSEQ R1, MSP ; if 0, stacking used MSP, copy to R0
    MRSNE R1, PSP ; if 1, stacking used PSP, copy to R0

    STR R0,[R1] ; Store return value
    BX LR ; Return from exception

        IMPORT  OS_MemManage_Hook
        EXPORT  MemManage_Handler

MemManage_Handler
    CPSID I ; 2) Prevent interrupt during switch
    MRS R0, CONTROL
    MOV R1, #0xE000E000
    LDRB R1, [R1, #0xD28]
    TST LR, #4 ; Test bit 2 of EXC_RETURN
    ITTEE EQ
    MRSEQ R2, MSP ; if 0, stacking used MSP, copy to R0
    BICEQ R1, R1, #2 ; Clear bit 1 of control register (SPSEL = 0, MSP)
    MRSNE R2, PSP ; if 1, stacking used PSP, copy to R0
    ORRNE R1, R1, #2 ; Set bit 1 of control register (SPSEL = 1, PSP)
    PUSH {LR}
    B OS_MemManage_Hook
    POP {LR}
    CPSIE I ; 9) tasks run with interrupts enabled
    BX LR

GPIOPortF_Handler
    ; CPSID I
    B GPIOPortF_Hook
    ; PUSH {LR}
    ; BL GPIOPortF_Hook
    ; POP {LR}
    ; B ContextSwitch

; Timer0A_Handler
; Timer0B_Handler
;     CPSID I
;     MOV R0, #0
;     B OS_Timer_Hook
    ; PUSH {LR}
    ; BL OS_Timer_Hook
    ; POP {LR}
    ; B ContextSwitch

Timer1A_Handler
Timer1B_Handler
    ; CPSID I
    MOV R0, #1
    B OS_Timer_Hook
    ; PUSH {LR}
    ; BL OS_Timer_Hook
    ; POP {LR}
    ; B ContextSwitch

Timer2A_Handler
Timer2B_Handler
    ; CPSID I
    MOV R0, #2
    B OS_Timer_Hook
    ; PUSH {LR}
    ; BL OS_Timer_Hook
    ; POP {LR}
    ; B ContextSwitch

Timer3A_Handler
Timer3B_Handler
    CPSID I
    MOV R0, #3
    B OS_Timer_Hook
    ; PUSH {LR}
    ; BL OS_Timer_Hook
    ; POP {LR}
    ; B ContextSwitch

Timer4A_Handler
Timer4B_Handler
    CPSID I
    MOV R0, #4
    B OS_Timer_Hook
    ; PUSH {LR}
    ; BL OS_Timer_Hook
    ; POP {LR}
    ; B ContextSwitch

Timer5A_Handler
Timer5B_Handler
    CPSID I
    MOV R0, #5
    B OS_Timer_Hook
    ; PUSH {LR}
    ; BL OS_Timer_Hook
    ; POP {LR}
    ; B ContextSwitch

    ALIGN
    END

;********************************************************************************************************
;                                         SET MPU PRIVILEGE
;                                     void MPU_SetPrivilegeASM(void)
;                                     void MPU_SetUnPrivilegeASM(void)
;
; Note(s) : Set MPU to privileged and unprivileged modes
; MSR and MRS are used to write and read to special CPU registers
; Do we need to save register states?
;********************************************************************************************************
MPU_SetUnPrivilegeASM
        MRS     R0, CONTROL         ; Read CONTROL register
        ORR     R0, R0, #1          ; Set bit 0 of control register to 1 (nPRIV = 1, unprivileged)
        MSR     CONTROL, R0         ; Write value back to CONTROL register
        ISB                         ; Instruction Sync Barrier. Make sure this is done before returning
        BX      LR                  ; Return from function

MPU_SetPrivilegeASM
        MRS     R0, CONTROL         ; Read CONTROL register
        BIC     R0, R0, #1          ; Clear bit 0 of control register (nPRIV = 0, privileged)
        MSR     CONTROL, R0         ; Write value back to CONTROL register
        ISB                         ; Instruction Sync Barrier. Make sure this is done before returning
        BX      LR                  ; Return from function