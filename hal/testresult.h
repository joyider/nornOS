#ifndef TESTRESULT_INCLUDED
#define TESTRESULT_INCLUDED
/**
 * @file testresult.h
 * Self-checking test applications: collect expectations, then end with a line
 *     RESULT tcNN PASS
 * or FAIL, which tools/test_all.sh looks for. The halt that follows ends a simavr run.
 */

#include <stdint.h>
#include <stdbool.h>
#include "rtos.h"
#include "uart.h"

static uint8_t tc_failures = 0;

/** Tolerance of rate and time checks in parts per thousand, at least a few counts.
    Cycle accurate simulators and real boards pass with the default; QEMU's timers drift
    about a percent against the CPU clock, so its builds get -DTC_TOLERANCE_PERMILLE=30. */
#ifndef TC_TOLERANCE_PERMILLE
# define TC_TOLERANCE_PERMILLE  5u
#endif

static inline void tc_expect(const char *what, bool ok)
{
    if(!ok)
    {
        ++ tc_failures;
        uart_puts("CHECK FAILED: ");
        uart_puts(what);
        uart_puts("\r\n");
    }
}

static inline void tc_expectRange(const char *what, uint32_t value, uint32_t lo, uint32_t hi)
{
    if(value < lo  ||  value > hi)
    {
        ++ tc_failures;
        uart_puts("CHECK FAILED: ");
        uart_puts(what);
        uart_putc('=');
        uart_putU32(value);
        uart_puts(" expected ");
        uart_putU32(lo);
        uart_puts("..");
        uart_putU32(hi);
        uart_puts("\r\n");
    }
}

/** Expect value within the tolerance of expected (and never off by less than 5). */
static inline void tc_expectNear(const char *what, uint32_t value, uint32_t expected)
{
    uint32_t tol = expected / 1000u * TC_TOLERANCE_PERMILLE;
    if(tol < 5u)
        tol = 5u;
    tc_expectRange(what, value, expected > tol ? expected - tol : 0u, expected + tol);
}

static inline void tc_finish(const char *name) __attribute__((noreturn));
static inline void tc_finish(const char *name)
{
    uart_puts("RESULT ");
    uart_puts(name);
    uart_puts(tc_failures == 0 ? " PASS\r\n" : " FAIL\r\n");
    uart_flush();
    rtos_port_halt();
}

#endif  /* TESTRESULT_INCLUDED */
