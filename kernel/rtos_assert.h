#ifndef RTOS_ASSERT_INCLUDED
#define RTOS_ASSERT_INCLUDED
/**
 * @file rtos_assert.h
 * Kernel assertion macro. Enabled with RTOS_CFG_ASSERT. On failure rtos_assertFailed is
 * called; the application may override the weak default to print and halt.
 */

#include "rtos_config.h"
#include "port.h"

#if RTOS_CFG_ASSERT
# define RTOS_ASSERT(cond)                                                      \
    do {                                                                        \
        if(!(cond))                                                             \
            rtos_assertFailed(RTOS_PORT_STR(__FILE__), (uint16_t)__LINE__);     \
    } while(0)
#else
# define RTOS_ASSERT(cond)  do { } while(0)
#endif

#endif  /* RTOS_ASSERT_INCLUDED */
