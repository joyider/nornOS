/**
 * @file tc06_float.c
 * Test case 6: floating point across context switches. Three tasks keep float values in
 * registers across preemption points and check them against exact results:
 *   3  every 2 ms: y = ((y + 0.25) * 2 - 0.5) / 2 stays at 1.0 exactly
 *   2  every 5 ms: a short series with a known closed form
 *   1  continuously: x = 2x + 1 twenty times, then back down, must end at 0.0
 * Task 1 is preempted by the others in the middle of its arithmetic all the time. On the
 * ESP32 the FPU is switched lazily between the tasks; on the AVR float is software and
 * the test only proves the context switch keeps every register. The application timer
 * interrupt counts but does not touch floating point.
 */

#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "testresult.h"
#if defined(BOARD_IS_ESP32)
# include "port.h"
#endif


/*
 * Defines
 */

#define TASK_FAST       3
#define TASK_SERIES     2
#define TASK_LOOP       1

#define APPL_IRQ_HZ     500ul
#if !BOARD_APP_TIMER_HZ_IS_EXACT(APPL_IRQ_HZ)
# error APPL_IRQ_HZ is not an exact rate of the application timer
#endif

#ifndef TC06_STOP_AFTER_MS
# define TC06_STOP_AFTER_MS  3000ul
#endif

#if RTOS_PORT_MIN_STACK > 192u
# define STACK_SIZE     (2u * RTOS_PORT_MIN_STACK)
#else
# define STACK_SIZE     192u
#endif


/*
 * Data definitions
 */

static uint8_t _stackFast[STACK_SIZE];
static uint8_t _stackSeries[STACK_SIZE];
static uint8_t _stackLoop[STACK_SIZE];

static volatile uint32_t _isrCount = 0;
static volatile uint32_t _fastRounds = 0, _fastErrors = 0;
static volatile uint32_t _seriesRounds = 0, _seriesErrors = 0;
static volatile uint32_t _loopRounds = 0, _loopErrors = 0;


/*
 * Function implementation
 */

RTOS_ISR(BOARD_APP_TIMER_VECT)
{
    ++ _isrCount;
}


static void taskFast(void *arg)
{
    float y = 1.0f;
    (void)arg;
    for(;;)
    {
        (void)rtos_waitPeriodic(RTOS_MS(2));
        y = y + 0.25f;
        y = y * 2.0f;
        y = y - 0.5f;
        y = y / 2.0f;
        ++ _fastRounds;
        if(y != 1.0f)
        {
            ++ _fastErrors;
            y = 1.0f;
        }
    }
}


static void taskSeries(void *arg)
{
    (void)arg;
    for(;;)
    {
        float s = 0.0f, k = 1.0f;
        uint8_t i;

        (void)rtos_waitPeriodic(RTOS_MS(5));
        for(i=0; i<16; ++i)             /* 1 + 2 + ... + 16 = 136, exact in float */
        {
            s += k;
            k += 1.0f;
            if(i == 8)
                rtos_delay(1);          /* switch out with s and k live in registers */
        }
        ++ _seriesRounds;
        if(s != 136.0f  ||  k != 17.0f)
            ++ _seriesErrors;
    }
}


static void taskLoop(void *arg)
{
    (void)arg;
    for(;;)
    {
        float x = 0.0f;
        uint8_t i;
        volatile uint16_t spin;

        for(i=0; i<20; ++i)
        {
            x = x * 2.0f + 1.0f;
            for(spin=0; spin<50; ++spin)
                ;
        }
        for(i=0; i<20; ++i)
        {
            x = (x - 1.0f) / 2.0f;
            for(spin=0; spin<50; ++spin)
                ;
        }
        ++ _loopRounds;
        if(x != 0.0f)
            ++ _loopErrors;

#if TC06_STOP_AFTER_MS > 0
        if(rtos_getTick() >= RTOS_MS(TC06_STOP_AFTER_MS))
        {
            uart_puts("tc06 fast=");       uart_putU32(_fastRounds);
            uart_puts(" series=");         uart_putU32(_seriesRounds);
            uart_puts(" loop=");           uart_putU32(_loopRounds);
            uart_puts(" errors=");         uart_putU32(_fastErrors);
            uart_putc('/');                uart_putU32(_seriesErrors);
            uart_putc('/');                uart_putU32(_loopErrors);
            uart_puts(" isr=");            uart_putU32(_isrCount);
#if defined(BOARD_IS_ESP32)
            uart_puts(" fpuSwitches=");    uart_putU32(rtos_port_fpuSwitchCount());
            tc_expectRange("fpuSwitches", rtos_port_fpuSwitchCount(), 100, 0xFFFFFFFFul);
#endif
            uart_puts("\r\n");
            tc_expectRange("fastRounds", _fastRounds, 1000, 0xFFFFFFFFul);
            tc_expectRange("seriesRounds", _seriesRounds, 300, 0xFFFFFFFFul);
            tc_expectRange("loopRounds", _loopRounds, 10, 0xFFFFFFFFul);
            tc_expectRange("fastErrors", _fastErrors, 0, 0);
            tc_expectRange("seriesErrors", _seriesErrors, 0, 0);
            tc_expectRange("loopErrors", _loopErrors, 0, 0);
            tc_expectNear("isr", _isrCount, TC06_STOP_AFTER_MS * APPL_IRQ_HZ / 1000);
            tc_finish("tc06");
        }
#endif
    }
}


void rtos_assertFailed(const char *file, uint16_t line)
{
    (void)rtos_port_enterCritical();
    uart_puts("\r\nASSERTION FAILED: ");
    uart_putsP(file);
    uart_puts(", line ");
    uart_putU32(line);
    uart_puts("\r\n");
    rtos_port_halt();
}


int main(void)
{
    uart_init(115200);
    uart_puts("\r\nnornOS " RTOS_VERSION " tc06_float\r\n");

    rtos_init();
    rtos_createTask(TASK_FAST,   taskFast,   NULL, _stackFast,   sizeof(_stackFast));
    rtos_createTask(TASK_SERIES, taskSeries, NULL, _stackSeries, sizeof(_stackSeries));
    rtos_createTask(TASK_LOOP,   taskLoop,   NULL, _stackLoop,   sizeof(_stackLoop));

    board_appTimerStart(APPL_IRQ_HZ);
    rtos_start();
    return 0;
}
