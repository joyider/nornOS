/**
 * @file port_start.S
 * Entry point of a nornOS image on the ESP32. The ROM loader has copied the segments into
 * IRAM/DRAM and jumps here on the PRO CPU with the APP CPU held in reset.
 *
 * Steps: no interrupts, fresh register window state, PS for windowed user code, VECBASE
 * to our vector table, our own stack, zeroed .bss, early hardware setup in C, then main.
 * .data needs no copying: the ROM loader placed it in DRAM already.
 */

#include "port_regs.h"

    .section .entry, "ax"
    .literal_position
    .global _start
    .align  4
_start:
    movi    a0, 0
    wsr     a0, INTENABLE
    wsr     a0, INTCLEAR              /* no-op on sources that are not software */

    /* Window base 0, only window 0 in use. Registers are undefined after this. */
    movi    a0, 1
    wsr     a0, WINDOWSTART
    movi    a0, 0
    wsr     a0, WINDOWBASE
    rsync

    /* Windowed ABI, user vector, interrupts masked until the kernel enables them. */
    movi    a0, PS_WOE | PS_UM | PS_INTLEVEL(PORT_INTLEVEL_CRITICAL)
    wsr     a0, PS
    rsync

    movi    a0, _vector_table
    wsr     a0, VECBASE

    movi    a1, _stack_top
    movi    a0, 0                     /* return address 0: main must not return */

    /* Clear .bss. */
    movi    a2, _bss_start
    movi    a3, _bss_end
    movi    a4, 0
1:  bgeu    a2, a3, 2f
    s32i    a4, a2, 0
    addi    a2, a2, 4
    j       1b
2:
    call4   rtos_port_earlyInit
    call4   main
3:  j       3b
