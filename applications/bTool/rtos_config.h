#ifndef RTOS_CONFIG_INCLUDED
#define RTOS_CONFIG_INCLUDED
/**
 * @file rtos_config.h
 * Kernel configuration of the bTool brewing controller.
 */

#define RTOS_CFG_NUM_TASKS      4
#define RTOS_CFG_TICK_HZ        1000ul
#define RTOS_CFG_TICK_BITS      32
#define RTOS_CFG_EVT_BITS       16
#define RTOS_CFG_STACK_CHECK    1
#define RTOS_CFG_IDLE_SLEEP     1
#define RTOS_CFG_ASSERT         1
#define RTOS_CFG_SW_TIMERS      1
#define RTOS_CFG_RUNTIME_STATS  1

#endif  /* RTOS_CONFIG_INCLUDED */
