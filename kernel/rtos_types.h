#ifndef RTOS_TYPES_INCLUDED
#define RTOS_TYPES_INCLUDED
/**
 * @file rtos_types.h
 * Kernel scalar types derived from the application configuration. Included by rtos.h and
 * by the port layer, so it must not depend on either.
 */

#include <stdint.h>
#include "rtos_config.h"

#ifndef RTOS_CFG_NUM_TASKS
# error RTOS_CFG_NUM_TASKS must be defined in rtos_config.h
#endif
#ifndef RTOS_CFG_TICK_HZ
# define RTOS_CFG_TICK_HZ 1000ul
#endif
#ifndef RTOS_CFG_TICK_BITS
# define RTOS_CFG_TICK_BITS 32
#endif
#ifndef RTOS_CFG_EVT_BITS
# define RTOS_CFG_EVT_BITS 16
#endif
#ifndef RTOS_CFG_STACK_CHECK
# define RTOS_CFG_STACK_CHECK 1
#endif
#ifndef RTOS_CFG_IDLE_SLEEP
# define RTOS_CFG_IDLE_SLEEP 1
#endif
#ifndef RTOS_CFG_ASSERT
# define RTOS_CFG_ASSERT 1
#endif
#ifndef RTOS_CFG_SW_TIMERS
# define RTOS_CFG_SW_TIMERS 1
#endif
#ifndef RTOS_CFG_RUNTIME_STATS
# define RTOS_CFG_RUNTIME_STATS 1
#endif

/** Bitmask with one bit per task. Sized to the smallest integer that fits. */
#if RTOS_CFG_NUM_TASKS + 1 <= 8
typedef uint8_t rtos_taskmask_t;
#elif RTOS_CFG_NUM_TASKS + 1 <= 16
typedef uint16_t rtos_taskmask_t;
#elif RTOS_CFG_NUM_TASKS + 1 <= 32
typedef uint32_t rtos_taskmask_t;
#else
# error At most 31 application tasks are supported
#endif

/** The system time in ticks. Wraps around; the kernel only ever compares differences. */
#if RTOS_CFG_TICK_BITS == 16
typedef uint16_t rtos_tick_t;
typedef int16_t  rtos_tickdiff_t;
#elif RTOS_CFG_TICK_BITS == 32
typedef uint32_t rtos_tick_t;
typedef int32_t  rtos_tickdiff_t;
#else
# error RTOS_CFG_TICK_BITS must be 16 or 32
#endif

/** Event flag vector, one bit per event, private to each task. */
#if RTOS_CFG_EVT_BITS == 8
typedef uint8_t rtos_evt_t;
#elif RTOS_CFG_EVT_BITS == 16
typedef uint16_t rtos_evt_t;
#elif RTOS_CFG_EVT_BITS == 32
typedef uint32_t rtos_evt_t;
#else
# error RTOS_CFG_EVT_BITS must be 8, 16 or 32
#endif

#endif  /* RTOS_TYPES_INCLUDED */
