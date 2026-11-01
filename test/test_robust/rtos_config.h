#ifndef RTOS_CONFIG_INCLUDED
#define RTOS_CONFIG_INCLUDED
/* Kernel configuration of the host robustness test: every safety feature on. */

#define RTOS_CFG_NUM_TASKS      4
#define RTOS_CFG_TICK_HZ        1000ul
#define RTOS_CFG_TICK_BITS      32
#define RTOS_CFG_EVT_BITS       16
#define RTOS_CFG_STACK_CHECK    2
#define RTOS_CFG_IDLE_SLEEP     1
#define RTOS_CFG_ASSERT         1
#define RTOS_CFG_SW_TIMERS      1
#define RTOS_CFG_RUNTIME_STATS  1
#define RTOS_CFG_PANIC_ACTION   0
#define RTOS_CFG_WATCHDOG       1
#define RTOS_CFG_TRACE          1
#define RTOS_CFG_TIMER_TASK_PRIO    4
#define RTOS_CFG_TIMER_TASK_STACK   16
#define RTOS_CFG_IDLE_HOOK      test_idleHook

void test_idleHook(void);

#endif  /* RTOS_CONFIG_INCLUDED */
