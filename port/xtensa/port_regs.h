#ifndef PORT_REGS_INCLUDED
#define PORT_REGS_INCLUDED
/**
 * @file port_regs.h
 * Xtensa LX6 special register bits and ESP32 peripheral addresses used by the port and
 * the HAL. Usable from C and assembler.
 */

/* PS register fields. */
#define PS_INTLEVEL_MASK    0x0000000F
#define PS_INTLEVEL(n)      ((n) & PS_INTLEVEL_MASK)
#define PS_EXCM             0x00000010
#define PS_UM               0x00000020
#define PS_RING(n)          (((n) & 3) << 6)
#define PS_OWB_MASK         0x00000F00
#define PS_CALLINC(n)       (((n) & 3) << 16)
#define PS_WOE              0x00040000

/* Interrupt level the kernel masks in a critical section: all level 1..3 sources. */
#define PORT_INTLEVEL_CRITICAL  3

/* EXCCAUSE value of a level 1 interrupt. */
#define EXCCAUSE_LEVEL1_INTERRUPT   4

/* CPU interrupt numbers that are level 1 on the ESP32 (0-10, 12, 13, 17, 18). 6 is the
   CCOMPARE0 timer, 7 is software; the rest are external, mapped by the interrupt matrix. */
#define PORT_LEVEL1_INT_MASK    0x000637FF
#define PORT_INT_TIMER0         6

/*
 * Interrupt frame on the stack of the interrupted task, built by _level1Interrupt in
 * port_context.S. HSP is the stack pointer while C handlers run; the saved registers sit
 * at HSP + PORT_ISR_FP. The 64 bytes between are the window save areas of the handler's
 * own register window (see port_context.S), and the 16 bytes at the very top are the base
 * save area of the interrupted frame's caller, which the spill at entry fills in.
 */
#define PORT_ISR_FRMSZ      176
#define PORT_ISR_FP         64
#define PORT_FP_PS          0
#define PORT_FP_PC          4
#define PORT_FP_A0          8
#define PORT_FP_A1          12
#define PORT_FP_A2          16      /* a2..a15 follow, 4 bytes each */
#define PORT_FP_A3          20
#define PORT_FP_AR(n)       (PORT_FP_A2 + 4 * ((n) - 2))
#define PORT_FP_SAR         72
#define PORT_FP_LBEG        76
#define PORT_FP_LEND        80
#define PORT_FP_LCOUNT      84
#define PORT_FP_CAUSE       88
#define PORT_FP_CPENABLE    92
#define PORT_FP_SIZE        112     /* incl. the 16 byte base save area at the top */

/* EXCCAUSE of a coprocessor 0 (FPU) instruction while CPENABLE bit 0 is clear. */
#define EXCCAUSE_CP0_DISABLED   32

/* Initial frame of a task, built by rtos_port_initStack: a0 and PS for the retw that
   starts it, with the fake caller's a0..a3 in the 16 bytes below. */
#define PORT_INIT_FRMSZ     32

/* Peripheral register base addresses (ESP32 TRM). */
#define DR_REG_DPORT_BASE   0x3FF00000
#define DR_REG_UART_BASE    0x3FF40000
#define DR_REG_GPIO_BASE    0x3FF44000
#define DR_REG_RTCCNTL_BASE 0x3FF48000
#define DR_REG_IO_MUX_BASE  0x3FF49000
#define DR_REG_TIMG0_BASE   0x3FF5F000
#define DR_REG_TIMG1_BASE   0x3FF60000

/* Watchdog write protection key. */
#define WDT_WKEY            0x50D83AA1

#ifndef __ASSEMBLER__
# include <stdint.h>
# define PORT_REG(addr)     (*(volatile uint32_t *)(addr))
# define RTOS_PORT_STRINGIFY_(x) #x
# define RTOS_PORT_STRINGIFY(x)  RTOS_PORT_STRINGIFY_(x)
#endif

#endif  /* PORT_REGS_INCLUDED */
