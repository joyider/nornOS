#ifndef RTOS_CONFIG_INCLUDED
#define RTOS_CONFIG_INCLUDED
/**
 * @file rtos_config.template.h
 * Copy this file to your application folder as rtos_config.h and adjust it.
 */

/** Number of application tasks (the idle task is not counted). Each task gets a unique
    priority 1..RTOS_CFG_NUM_TASKS; higher number means higher priority. Max 31. */
#define RTOS_CFG_NUM_TASKS      3

/** Tick frequency in Hz. Must divide F_CPU/64 exactly (1000 and 500 work at 16 MHz). */
#define RTOS_CFG_TICK_HZ        1000ul

/** Width of the tick counter: 16 or 32. 32 avoids false overrun detections. */
#define RTOS_CFG_TICK_BITS      32

/** Width of the per-task event flag vector: 8, 16 or 32. */
#define RTOS_CFG_EVT_BITS       16

/** AVR: which 16 bit timer drives the tick. Mega 2560: 1, 3, 4 or 5; UNO: only 1. Leave
    it undefined to get 3 on the Mega and 1 on the UNO. Timer 0 and 2 are never used by
    the kernel; hal/board.h offers timer 2 to the application. */
/* #define RTOS_CFG_TIMER       3 */

/** Fill stacks with a pattern at creation and support rtos_getStackFree. */
#define RTOS_CFG_STACK_CHECK    1

/** Let the idle task sleep (SLEEP_MODE_IDLE) between interrupts. */
#define RTOS_CFG_IDLE_SLEEP     1

/** Compile software timers (rtos_timer*). Costs one list check per tick. */
#define RTOS_CFG_SW_TIMERS      1

/** Measure run time per task at every context switch (rtos_getTaskRunTime,
    rtos_getCpuLoad). Costs about 60 cycles per switch and 4 bytes per task. */
#define RTOS_CFG_RUNTIME_STATS  1

/** Compile kernel assertions. */
#define RTOS_CFG_ASSERT         1

/** Optional: a function void f(void) called in every idle loop iteration. */
/* #define RTOS_CFG_IDLE_HOOK   myIdleHook */

/** 0: a panic halts the CPU (development). 1: it resets the board; the reason survives
    the reset and is read with rtos_getLastPanic. */
#define RTOS_CFG_PANIC_ACTION   0

/** RTOS_CFG_STACK_CHECK 2 adds a canary at the bottom of every stack, checked before
    each context switch and at each tick: an overflow becomes RTOS_PANIC_STACK_OVERFLOW. */

/** Hardware watchdog kicked by the idle task plus per task check-ins (rtos_wdtInit,
    rtos_wdtMonitor, rtos_wdtCheckIn). */
#define RTOS_CFG_WATCHDOG       0

/** AVR only: 1 makes rtos_port_reset use the watchdog, which needs a bootloader that
    disables the watchdog at start; 0 jumps to the reset vector instead. */
/* #define RTOS_CFG_AVR_RESET_VIA_WDT 0 */

/** Run software timer callbacks in a kernel task of this priority instead of in the tick
    interrupt; 0 keeps them in the interrupt. The slot must be left free by the
    application. RTOS_CFG_TIMER_TASK_STACK sets that task's stack size in bytes. */
#define RTOS_CFG_TIMER_TASK_PRIO    0

/** Call rtos_traceHook at context switches, blocking, wake-ups, timers and panics. The
    application provides the hook; hal/trace.h is a ring buffer recorder for it. */
#define RTOS_CFG_TRACE          0

/** Event groups: flags shared by several tasks (rtos_evgroup*). */
#define RTOS_CFG_EVGROUPS       1

/** rtos_suspend / rtos_resume. */
#define RTOS_CFG_SUSPEND        1

/** First fit heap with coalescing (rtos_heapInit, rtos_malloc, rtos_free). */
#define RTOS_CFG_HEAP           0

/** Tickless idle: the idle task sleeps until the next deadline instead of every tick.
    ESP32 only; the AVR port ignores it. */
#define RTOS_CFG_TICKLESS       0

#endif  /* RTOS_CONFIG_INCLUDED */
