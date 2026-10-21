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

#endif  /* RTOS_CONFIG_INCLUDED */
