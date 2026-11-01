/**
 * @file port_vectors.S
 * Exception and interrupt vector table of the Xtensa LX6 in the ESP32. VECBASE points
 * here (set in port_start.S). Each vector has 64 bytes.
 *
 * The window overflow and underflow handlers are the standard ones of the windowed ABI
 * (Xtensa ISA, section 4.7.1): they spill or refill the 4, 8 or 12 registers of a caller
 * frame when the physical register file runs out. They are entered with PS.EXCM set and
 * use the s32e/l32e instructions that address the caller's save area.
 *
 * Level 1 interrupts (EXCCAUSE 4) and the FPU trap (EXCCAUSE 32, coprocessor 0 disabled)
 * arrive at the user exception vector and continue in _exceptionEntry (port_context.S),
 * which hands them to rtos_port_exception. Everything else lands in rtos_port_fatal.
 */

#include "port_regs.h"

    .section .vectors, "ax"
    .global _vector_table
_vector_table:

/* ---- window overflow / underflow, offsets 0x000 - 0x17F ----------------------------- */

    .org 0x000
_WindowOverflow4:
    s32e    a0, a5, -16
    s32e    a1, a5, -12
    s32e    a2, a5,  -8
    s32e    a3, a5,  -4
    rfwo

    .org 0x040
_WindowUnderflow4:
    l32e    a0, a5, -16
    l32e    a1, a5, -12
    l32e    a2, a5,  -8
    l32e    a3, a5,  -4
    rfwu

    .org 0x080
_WindowOverflow8:
    s32e    a0, a9, -16
    l32e    a0, a1, -12
    s32e    a1, a9, -12
    s32e    a2, a9,  -8
    s32e    a3, a9,  -4
    s32e    a4, a0, -32
    s32e    a5, a0, -28
    s32e    a6, a0, -24
    s32e    a7, a0, -20
    rfwo

    .org 0x0C0
_WindowUnderflow8:
    l32e    a0, a9, -16
    l32e    a1, a9, -12
    l32e    a2, a9,  -8
    l32e    a7, a1, -12
    l32e    a3, a9,  -4
    l32e    a4, a7, -32
    l32e    a5, a7, -28
    l32e    a6, a7, -24
    l32e    a7, a7, -20
    rfwu

    .org 0x100
_WindowOverflow12:
    s32e    a0,  a13, -16
    l32e    a0,  a1,  -12
    s32e    a1,  a13, -12
    s32e    a2,  a13,  -8
    s32e    a3,  a13,  -4
    s32e    a4,  a0,  -48
    s32e    a5,  a0,  -44
    s32e    a6,  a0,  -40
    s32e    a7,  a0,  -36
    s32e    a8,  a0,  -32
    s32e    a9,  a0,  -28
    s32e    a10, a0,  -24
    s32e    a11, a0,  -20
    rfwo

    .org 0x140
_WindowUnderflow12:
    l32e    a0,  a13, -16
    l32e    a1,  a13, -12
    l32e    a2,  a13,  -8
    l32e    a11, a1,  -12
    l32e    a3,  a13,  -4
    l32e    a4,  a11, -48
    l32e    a5,  a11, -44
    l32e    a6,  a11, -40
    l32e    a7,  a11, -36
    l32e    a8,  a11, -32
    l32e    a9,  a11, -28
    l32e    a10, a11, -24
    l32e    a11, a11, -20
    rfwu

/* ---- interrupt levels 2..5, debug, NMI --------------------------------------------- */

    .org 0x180
_Level2InterruptVector:
    j       _fatalEntry
    .org 0x1C0
_Level3InterruptVector:
    j       _fatalEntry
    .org 0x200
_Level4InterruptVector:
    j       _fatalEntry
    .org 0x240
_Level5InterruptVector:
    j       _fatalEntry
    .org 0x280
_DebugExceptionVector:
    j       _fatalEntry
    .org 0x2C0
_NMIExceptionVector:
    j       _fatalEntry

/* ---- exceptions ---------------------------------------------------------------------- */

    .org 0x300
_KernelExceptionVector:
    j       _fatalEntry

    .org 0x340
_UserExceptionVector:
    wsr     a0, excsave1
    rsr     a0, exccause
    beqi    a0, EXCCAUSE_LEVEL1_INTERRUPT, 1f
    beqi    a0, EXCCAUSE_CP0_DISABLED, 1f
    j       _fatalEntry
1:  j       _exceptionEntry

    .org 0x3C0
_DoubleExceptionVector:
    j       _fatalEntry

    .org 0x400
_vector_table_end:


/*
 * Unhandled exception or interrupt: leave exception mode, keep the stack we have, and
 * report EXCCAUSE and EPC1 through rtos_port_fatal(cause, epc, excvaddr), which never
 * returns. Arguments of a call4 go in a6..a8 of the caller.
 */
    .section .text
    .literal_position
    .align  4
_fatalEntry:
    rsr     a2, EXCCAUSE
    rsr     a3, EPC1
    rsr     a4, EXCVADDR
    movi    a0, PS_WOE | PS_UM          /* exception mode off, interrupts stay masked */
    wsr     a0, PS
    rsync
    mov     a6, a2
    mov     a7, a3
    mov     a8, a4
    call4   rtos_port_fatal
1:  j       1b
