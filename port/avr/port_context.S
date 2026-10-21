/**
 * @file port_context.S
 * AVR context switch. Called from C with interrupts disabled:
 *
 *     rtos_pNext = &rtos_tcbAry[n];
 *     rtos_port_switchContext();
 *
 * Saves the call-saved registers r2-r17, r28, r29 and SREG of the current task on its own
 * stack, stores the stack pointer in rtos_pCurrent->ctx (offset 0 of the TCB), makes
 * rtos_pNext the current task, loads its stack pointer, restores its registers and
 * returns into it. The call-clobbered registers do not need saving here: for a voluntary
 * switch the C caller has already given them up, and for a switch at the end of an ISR the
 * RTOS_ISR wrapper has pushed them before calling rtos_isrTail.
 *
 * A freshly created task has a stack prepared by rtos_port_initStack with the same layout,
 * so the first switch into it is not a special case: the "return" lands in
 * rtos_taskTrampoline.
 */

#include <avr/io.h>

        .section .text
        .global  rtos_port_switchContext
        .type    rtos_port_switchContext, @function

rtos_port_switchContext:
        push    r2
        push    r3
        push    r4
        push    r5
        push    r6
        push    r7
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
        push    r16
        push    r17
        push    r28
        push    r29
        in      r0, _SFR_IO_ADDR(SREG)
        push    r0

        /* rtos_pCurrent->ctx = SP */
        lds     r30, rtos_pCurrent
        lds     r31, rtos_pCurrent+1
        in      r0, _SFR_IO_ADDR(SPL)
        st      Z, r0
        in      r0, _SFR_IO_ADDR(SPH)
        std     Z+1, r0

        /* rtos_pCurrent = rtos_pNext */
        lds     r30, rtos_pNext
        lds     r31, rtos_pNext+1
        sts     rtos_pCurrent, r30
        sts     rtos_pCurrent+1, r31

        /* SP = rtos_pCurrent->ctx */
        ld      r0, Z
        out     _SFR_IO_ADDR(SPL), r0
        ldd     r0, Z+1
        out     _SFR_IO_ADDR(SPH), r0

        pop     r0
        out     _SFR_IO_ADDR(SREG), r0
        pop     r29
        pop     r28
        pop     r17
        pop     r16
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     r7
        pop     r6
        pop     r5
        pop     r4
        pop     r3
        pop     r2
        ret

        .size   rtos_port_switchContext, .-rtos_port_switchContext
