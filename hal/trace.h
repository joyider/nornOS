#ifndef TRACE_INCLUDED
#define TRACE_INCLUDED
/**
 * @file trace.h
 * Ring buffer recorder for the kernel trace hook (RTOS_CFG_TRACE). The application
 * implements rtos_traceHook by calling trace_record, and prints the buffer with
 * trace_dump when something went wrong, for example from rtos_panicHook. Entries carry
 * the run time counter (RTOS_RUNTIME_HZ) as time stamp. TRACE_DEPTH entries are kept;
 * the oldest are overwritten.
 */

#include <stdint.h>
#include "rtos.h"

#ifndef TRACE_DEPTH
# define TRACE_DEPTH    32u
#endif

typedef struct trace_entry
{
    uint32_t time;
    uint32_t a;
    uint32_t b;
    uint8_t event;
} trace_entry_t;

/** Store one entry. Interrupts must be disabled (true inside rtos_traceHook). */
void trace_record(uint8_t event, uint32_t a, uint32_t b);

/** Print the buffer in order on the console UART, oldest first. */
void trace_dump(void);

/** Number of entries recorded so far (may exceed TRACE_DEPTH). */
uint32_t trace_count(void);

#endif  /* TRACE_INCLUDED */
