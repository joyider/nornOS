#ifndef RTOS_CONFIG_INCLUDED
#define RTOS_CONFIG_INCLUDED
/**
 * @file rtos_config.h
 * Kernel configuration of test case tc05: panic resets the board, stack canaries and
 * the watchdog are on.
 */

#define RTOS_CFG_NUM_TASKS      2
#define RTOS_CFG_TICK_HZ        1000ul
#define RTOS_CFG_TICK_BITS      32
#define RTOS_CFG_EVT_BITS       16
#define RTOS_CFG_STACK_CHECK    2
#define RTOS_CFG_IDLE_SLEEP     1
#define RTOS_CFG_ASSERT         1
#define RTOS_CFG_PANIC_ACTION   1
#define RTOS_CFG_WATCHDOG       1

#endif  /* RTOS_CONFIG_INCLUDED */
