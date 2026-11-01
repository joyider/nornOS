/**
 * @file port_context.S
 * Xtensa LX6 windowed ABI: context switch, task start and the level 1 interrupt entry.
 *
 * Register windows make this different from the AVR port. Live registers of the callers
 * of the current function are still in the register file, not on the stack, and a0..a3
 * (a4..a11 for call8/call12 frames) of a frame are written to fixed places relative to
 * the stack pointers of its neighbours only when a window overflow exception spills them.
 * Before a context can be left, every live window except the current one is therefore
 * spilled with SPILL_ALL_WINDOWS: rotating the window base and touching a12 forces the
 * overflow exceptions for every live window in turn (the technique of xthal_window_spill
 * and of the Zephyr port). After that WINDOWSTART holds only the current window, the
 * register file is free, and a return from the current function refills the caller from
 * the stack through a window underflow exception. The macro needs PS.WOE = 1 and
 * PS.EXCM = 0 (window exceptions must be possible) and interrupts masked by PS.INTLEVEL.
 */

#include "port_regs.h"

    .macro SPILL_ALL_WINDOWS
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    4
    .endm

    .section .text
    .literal_position

/*
 * void rtos_port_switchContext(void)
 *
 * Called from the kernel with interrupts masked. Saves a0 and PS of this frame on the
 * stack, spills all other windows, stores the stack pointer in rtos_pCurrent->ctx (the
 * first TCB member), makes rtos_pNext current, loads its stack pointer and returns into
 * it. The retw underflows into the caller frame of the next task's own switchContext
 * call, or into the fake caller frame prepared by rtos_port_initStack for a new task.
 */
    .align  4
    .global rtos_port_switchContext
    .type   rtos_port_switchContext, @function
rtos_port_switchContext:
    entry   a1, 32
    rsr     a2, ps
    s32i    a0, a1, 0
    s32i    a2, a1, 4
    SPILL_ALL_WINDOWS
    movi    a3, 0
    wsr     a3, cpenable                /* lazy FPU: the next FPU instruction traps */
    movi    a3, rtos_pCurrent
    movi    a5, rtos_pNext
    l32i    a4, a3, 0
    s32i    a1, a4, 0                   /* pCurrent->ctx = sp */
    l32i    a4, a5, 0
    s32i    a4, a3, 0                   /* pCurrent = pNext */
    l32i    a1, a4, 0                   /* sp = pNext->ctx */
    l32i    a2, a1, 4
    l32i    a0, a1, 0
    wsr     a2, ps
    rsync
    retw
    .size   rtos_port_switchContext, . - rtos_port_switchContext


/*
 * First code of every task. Entered through the retw of rtos_port_switchContext with the
 * stack pointer at the top of the task's stack (reloaded by the window underflow from the
 * fake caller frame), interrupts masked. The trampoline enables them.
 */
    .align  4
    .global _rtos_port_taskStart
    .type   _rtos_port_taskStart, @function
_rtos_port_taskStart:
    call4   rtos_taskTrampoline
1:  j       1b
    .size   _rtos_port_taskStart, . - _rtos_port_taskStart


/*
 * Exception entry for level 1 interrupts and the FPU trap, jumped to from the user
 * exception vector with the interrupted a0 in EXCSAVE1, PC in EPC1 and PS.EXCM set. The
 * C side, rtos_port_exception(cause, frame), dispatches on the cause.
 *
 * The frame is built on the interrupted stack (port_regs.h). The interrupted window is
 * reused for the handler: its registers are all saved first, then exception mode is
 * left with interrupts masked, and all other windows are spilled while a1 still holds
 * the interrupted stack pointer, so the spilled callers land where the interrupted code
 * will look for them. The handler's own window can be spilled too once C code is called:
 * the overflow handler reads the "caller's a1" from 12 bytes below the handler's stack
 * pointer and writes a4..a11 at 48..20 bytes below that value, so the entry stores a
 * pointer there that aims the write into reserved space inside the frame.
 */
    .align  4
    .global _exceptionEntry
    .type   _exceptionEntry, @function
_exceptionEntry:
    addi    a0, a1, -PORT_FP_SIZE
    s32i    a2, a0, PORT_FP_A2
    s32i    a3, a0, PORT_FP_A3
    rsr     a2, excsave1
    s32i    a2, a0, PORT_FP_A0
    rsr     a2, epc1
    s32i    a2, a0, PORT_FP_PC
    rsr     a2, ps
    s32i    a2, a0, PORT_FP_PS
    s32i    a1, a0, PORT_FP_A1
    s32i    a4, a0, PORT_FP_AR(4)
    s32i    a5, a0, PORT_FP_AR(5)
    s32i    a6, a0, PORT_FP_AR(6)
    s32i    a7, a0, PORT_FP_AR(7)
    s32i    a8, a0, PORT_FP_AR(8)
    s32i    a9, a0, PORT_FP_AR(9)
    s32i    a10, a0, PORT_FP_AR(10)
    s32i    a11, a0, PORT_FP_AR(11)
    s32i    a12, a0, PORT_FP_AR(12)
    s32i    a13, a0, PORT_FP_AR(13)
    s32i    a14, a0, PORT_FP_AR(14)
    s32i    a15, a0, PORT_FP_AR(15)
    rsr     a2, sar
    s32i    a2, a0, PORT_FP_SAR
    rsr     a2, lbeg
    s32i    a2, a0, PORT_FP_LBEG
    rsr     a2, lend
    s32i    a2, a0, PORT_FP_LEND
    rsr     a2, lcount
    s32i    a2, a0, PORT_FP_LCOUNT
    rsr     a2, exccause
    s32i    a2, a0, PORT_FP_CAUSE
    rsr     a2, cpenable
    s32i    a2, a0, PORT_FP_CPENABLE
    movi    a2, 0
    wsr     a2, cpenable                /* no FPU in handlers; a use traps to a panic */

    /* Leave exception mode, keep interrupts masked, then spill the other windows. */
    movi    a2, PS_WOE | PS_UM | PS_INTLEVEL(PORT_INTLEVEL_CRITICAL)
    wsr     a2, ps
    rsync
    SPILL_ALL_WINDOWS

    /* Handler stack below the frame; pseudo base save area for the handler window. */
    addi    a1, a0, -PORT_ISR_FP        /* a1 = HSP */
    addi    a3, a1, -16
    s32i    a0, a3, 4                   /* [HSP - 12] = FP: a4..a11 spill to [HSP+16..48) */
    l32i    a10, a0, PORT_FP_CAUSE      /* arguments of a call8 go in a10, a11 */
    mov     a11, a0
    movi    a0, 0
    call8   rtos_port_exception

    /* Back in exception mode: no interrupts, no window exceptions, then restore. */
    addi    a0, a1, PORT_ISR_FP         /* a0 = FP */
    l32i    a2, a0, PORT_FP_PS
    wsr     a2, ps
    rsync
    l32i    a2, a0, PORT_FP_PC
    wsr     a2, epc1
    l32i    a2, a0, PORT_FP_SAR
    wsr     a2, sar
    l32i    a2, a0, PORT_FP_LBEG
    wsr     a2, lbeg
    l32i    a2, a0, PORT_FP_LEND
    wsr     a2, lend
    l32i    a2, a0, PORT_FP_LCOUNT
    wsr     a2, lcount
    l32i    a2, a0, PORT_FP_CPENABLE
    wsr     a2, cpenable
    l32i    a3, a0, PORT_FP_A3
    l32i    a4, a0, PORT_FP_AR(4)
    l32i    a5, a0, PORT_FP_AR(5)
    l32i    a6, a0, PORT_FP_AR(6)
    l32i    a7, a0, PORT_FP_AR(7)
    l32i    a8, a0, PORT_FP_AR(8)
    l32i    a9, a0, PORT_FP_AR(9)
    l32i    a10, a0, PORT_FP_AR(10)
    l32i    a11, a0, PORT_FP_AR(11)
    l32i    a12, a0, PORT_FP_AR(12)
    l32i    a13, a0, PORT_FP_AR(13)
    l32i    a14, a0, PORT_FP_AR(14)
    l32i    a15, a0, PORT_FP_AR(15)
    l32i    a2, a0, PORT_FP_A2
    l32i    a1, a0, PORT_FP_A1
    l32i    a0, a0, PORT_FP_A0
    rsync
    rfe
    .size   _exceptionEntry, . - _exceptionEntry
