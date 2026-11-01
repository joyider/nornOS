#ifndef RTOS_PORT_INCLUDED
#define RTOS_PORT_INCLUDED
/**
 * @file port.h
 * Xtensa LX6 (ESP32) port of the kernel: critical sections, types and constants the
 * kernel needs. Startup is in port_start.S, the vector table in port_vectors.S, hardware
 * setup and the port functions in port_esp32.c.
 *

 * FPU: the single precision FPU (coprocessor 0) is handled lazily. CPENABLE is cleared at
 * every context switch; the first FPU instruction of a task traps, and the trap saves the
 * registers of the task that used the FPU last and loads the current task's. Tasks that
 * never touch floating point cost nothing. Interrupt handlers and timer callbacks in the
 * tick must not use the FPU: that traps to a panic.
 *
 * Interrupts: the kernel sees level 1 interrupts only. A peripheral source is routed by
 * the interrupt matrix to a level 1 CPU interrupt with rtos_port_attachInterrupt, and the
 * dispatcher in port_esp32.c calls the attached handler; the handler must clear the source
 * at the peripheral. RTOS_ISR(name) defines such a handler as name_rtosBody. The tick is
 * the CCOMPARE0 timer on CPU interrupt 6.
 *
 * Critical sections raise PS.INTLEVEL to PORT_INTLEVEL_CRITICAL with rsil, which masks
 * every interrupt the kernel knows about, and restore the saved PS afterwards.
 */

#include <stdint.h>
#include <stddef.h>
#include "rtos_config.h"
#include "rtos_types.h"
#include "port_regs.h"


/*
 * Defines
 */

/** Smallest stack: an interrupt frame, the window spill area of a few nested calls and
    some room for the task itself. */
#define RTOS_PORT_MIN_STACK     1024u

/** The windowed ABI needs 16 byte aligned stack frames. */
#define RTOS_PORT_STACK_ALIGN   16u

#define RTOS_PORT_WEAK          __attribute__((weak))

/** Data that keeps its value across a reset: the .noinit section of esp32.ld, which
    _start does not clear. DRAM is retained through a software or watchdog reset. */
#define RTOS_PORT_NOINIT        __attribute__((section(".noinit")))

/** Strings live in DRAM; nothing to do. */
#define RTOS_PORT_STR(s)        (s)

/** Run time counter: the CPU cycle counter CCOUNT, extended to 64 bits by the port. */
#define RTOS_PORT_RUNTIME_HZ    F_CPU

/** nsau gives the leading zero count in one instruction. */
#define RTOS_PORT_HIGHEST_BIT(m)    (31 - __builtin_clz((uint32_t)(m)))

/** Interrupt body definition; the port's dispatcher calls it. */
#define RTOS_ISR(vector)        RTOS_ISR_(vector)
#define RTOS_ISR_(vector)       void vector##_rtosBody(void)

/** No peripheral source: the CPU interrupt is internal (timer, software). */
#define RTOS_PORT_SRC_NONE      0xFFu


/*
 * Global type definitions
 */

/** Saved context of a task: its stack pointer. Must be the first TCB member. */
typedef uint32_t *rtos_port_ctx_t;

/** Saved PS of a critical section. */
typedef uint32_t rtos_port_cs_t;

typedef uint64_t rtos_port_runtime_t;

/** Interrupt handler as stored by rtos_port_attachInterrupt. */
typedef void (*rtos_port_isr_t)(void);


/*
 * Inline functions
 */

static inline rtos_port_cs_t rtos_port_enterCritical(void)
{
    uint32_t ps;
    __asm__ __volatile__ ("rsil %0, " RTOS_PORT_STRINGIFY(PORT_INTLEVEL_CRITICAL)
                          : "=a"(ps) : : "memory");
    return ps;
}

static inline void rtos_port_exitCritical(rtos_port_cs_t ps)
{
    __asm__ __volatile__ ("wsr %0, ps\n\trsync" : : "a"(ps) : "memory");
}

static inline void rtos_port_enableInterrupts(void)
{
    uint32_t ps;
    __asm__ __volatile__ ("rsil %0, 0" : "=a"(ps) : : "memory");
    (void)ps;
}

/** Wait for an interrupt with everything unmasked. */
static inline void rtos_port_idleSleep(void)
{
    __asm__ __volatile__ ("waiti 0" : : : "memory");
}

/** Tickless idle: sleep until the next interrupt, with the tick timer moved out to
    \a ticks from now (RTOS_FOREVER: as far as allowed) so that the CPU does not wake
    for ticks nobody waits for. The missed ticks are counted on wake up. */
void rtos_port_idleSleepTicks(rtos_tick_t ticks);

/** Number of tick interrupts taken; with tickless idle fewer than the tick count. */
uint32_t rtos_port_tickInterrupts(void);

static inline void rtos_port_halt(void) __attribute__((noreturn));
static inline void rtos_port_halt(void)
{
    for(;;)
        __asm__ __volatile__ ("waiti 15" : : : "memory");
}

/** 0 on the PRO CPU, 1 on the APP CPU (PRID special register). */
static inline uint8_t rtos_port_coreId(void)
{
    uint32_t prid;
    __asm__ __volatile__ ("rsr %0, prid" : "=a"(prid));
    return (prid & 0xFFFFu) == 0xABABu ? 1u : 0u;
}

/** Read the CPU cycle counter. */
static inline uint32_t rtos_port_ccount(void)
{
    uint32_t c;
    __asm__ __volatile__ ("rsr %0, ccount" : "=a"(c));
    return c;
}


/*
 * Global prototypes
 */

void rtos_port_switchContext(void);
rtos_port_ctx_t rtos_port_initStack(uint8_t *stackArea, size_t stackSize, void (*entry)(void));
void rtos_port_startTimer(void);
rtos_port_runtime_t rtos_port_getRunTimeCounter(void);

/** Called from _start before main: watchdogs off, clocks known. */
void rtos_port_earlyInit(void);

/** Switch the CPU to F_CPU (port_clock.c); called by rtos_port_earlyInit. */
void rtos_port_clockInit(void);

/** Reset the chip (digital system reset through RTC_CNTL). Never returns. */
void rtos_port_reset(void) __attribute__((noreturn));

/** RTOS_RESET_* cause of the last reset of the PRO CPU. */
uint8_t rtos_port_getResetCause(void);

/** Start the timer group 0 watchdog with a system reset after timeoutMs. */
void rtos_port_wdtStart(uint32_t timeoutMs);

/** Feed the watchdog. */
void rtos_port_wdtKick(void);

/** Route peripheral interrupt \a source (ESP32 TRM table, or RTOS_PORT_SRC_NONE for an
    internal CPU interrupt) to level 1 CPU interrupt \a cpuInt and enable it. */
void rtos_port_attachInterrupt(uint8_t cpuInt, uint8_t source, rtos_port_isr_t handler);

/** Called from the interrupt entry with interrupts masked: runs every pending handler. */
void rtos_port_dispatchLevel1(void);

/** Called from the exception entry: level 1 interrupts and the FPU trap. */
void rtos_port_exception(uint32_t cause, uint32_t *pFrame);

/** How often the FPU registers were switched to another task (lazy FPU context). */
uint32_t rtos_port_fpuSwitchCount(void);

/** AMP: copy the APP CPU's image (an esptool image: header and segments for its own
    IRAM/DRAM, see mem_core1.ld) into place and start the APP CPU at its entry. Called on
    the PRO CPU before the kernel starts. */
void rtos_port_startAppCpu(const uint8_t *image, uint32_t size);

/** Raise the FROM_CPU interrupt of the other core (source 24 for the PRO CPU, 25 for the
    APP CPU); the receiver clears it with rtos_port_ackCrossCore. */
void rtos_port_signalOtherCore(void);
void rtos_port_ackCrossCore(void);
#define RTOS_PORT_SRC_FROM_CPU0     24u
#define RTOS_PORT_SRC_FROM_CPU1     25u

/** Unhandled exception or interrupt; prints the cause on UART0 and halts. */
void rtos_port_fatal(uint32_t cause, uint32_t epc, uint32_t vaddr) __attribute__((noreturn));

#endif  /* RTOS_PORT_INCLUDED */
