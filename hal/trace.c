/**
 * @file trace.c
 * Ring buffer recorder for the kernel trace hook.
 */

#include "trace.h"
#include "uart.h"

static trace_entry_t _buf[TRACE_DEPTH];
static uint32_t _count = 0;

void trace_record(uint8_t event, uint32_t a, uint32_t b)
{
    trace_entry_t * const e = &_buf[_count % TRACE_DEPTH];
    e->time = (uint32_t)rtos_port_getRunTimeCounter();
    e->event = event;
    e->a = a;
    e->b = b;
    ++ _count;
}

uint32_t trace_count(void)
{
    return _count;
}

static void putName(uint8_t event)
{
    switch(event)
    {
    case RTOS_TRACE_SWITCH: uart_puts("SWITCH"); break;
    case RTOS_TRACE_BLOCK:  uart_puts("BLOCK "); break;
    case RTOS_TRACE_READY:  uart_puts("READY "); break;
    case RTOS_TRACE_TIMER:  uart_puts("TIMER "); break;
    case RTOS_TRACE_PANIC:  uart_puts("PANIC "); break;
    default:                uart_puts("?     "); break;
    }
}

void trace_dump(void)
{
    const uint32_t n = _count < TRACE_DEPTH ? _count : TRACE_DEPTH;
    uint32_t i;

    uart_puts("trace (");
    uart_putU32(_count);
    uart_puts(" events, last ");
    uart_putU32(n);
    uart_puts("):\r\n");
    for(i=0; i<n; ++i)
    {
        const trace_entry_t * const e = &_buf[(_count - n + i) % TRACE_DEPTH];
        uart_puts("  ");
        uart_putU32(RTOS_RUNTIME_TO_MS(e->time));
        uart_puts(" ms ");
        putName(e->event);
        uart_putc(' ');
        uart_putU32(e->a);
        uart_putc(' ');
        uart_putU32(e->b);
        uart_puts("\r\n");
    }
}
