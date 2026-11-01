/**
 * @file tc08_features.c
 * Test case 8: heap, event groups, suspend/resume and tickless idle together.
 *   3  supervisor: every 100 ms suspends task 1 for 50 ms and checks that it froze;
 *      allocates and frees heap blocks with patterns; reports and finishes.
 *   2  waits on an event group for the application interrupt's flag (ALL, clear) and
 *      counts; the interrupt sets it at 500 Hz.
 *   1  counts in a 10 ms loop with a heap block it writes and verifies.
 * With RTOS_CFG_TICKLESS the idle task programs the tick timer only for the next
 * deadline; on the ESP32 the number of tick interrupts taken is well below the number
 * of ticks counted, and the time base must still be exact.
 */

#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "testresult.h"
#if defined(BOARD_IS_ESP32)
# include "port.h"
#endif

#define TASK_SUPER      3
#define TASK_WAITER     2
#define TASK_COUNTER    1

#define APPL_IRQ_HZ     500ul
#if !BOARD_APP_TIMER_HZ_IS_EXACT(APPL_IRQ_HZ)
# error APPL_IRQ_HZ is not an exact rate of the application timer
#endif

#ifndef TC08_STOP_AFTER_MS
# define TC08_STOP_AFTER_MS  3000ul
#endif

#if RTOS_PORT_MIN_STACK > 192u
# define STACK_SIZE     (2u * RTOS_PORT_MIN_STACK)
# define HEAP_SIZE      2048u
#else
# define STACK_SIZE     192u
# define HEAP_SIZE      256u
#endif

#define EV_TICK         0x01u

static uint8_t _stackSuper[STACK_SIZE];
static uint8_t _stackWaiter[STACK_SIZE];
static uint8_t _stackCounter[STACK_SIZE];
static uint8_t _heap[HEAP_SIZE];

static rtos_evgroup_t _group = RTOS_EVGROUP_INIT(0);
static volatile uint32_t _isrCount = 0, _waiterCount = 0, _counter = 0;
static volatile uint32_t _frozenOk = 0, _frozenBad = 0, _heapErrors = 0, _heapRounds = 0;


RTOS_ISR(BOARD_APP_TIMER_VECT)
{
    ++ _isrCount;
    rtos_evgroupSetFromIsr(&_group, EV_TICK);
}


static void taskWaiter(void *arg)
{
    (void)arg;
    for(;;)
    {
        if(rtos_evgroupWait(&_group, EV_TICK, RTOS_WAIT_ALL | RTOS_WAIT_CLEAR, RTOS_MS(100)) == EV_TICK)
            ++ _waiterCount;
    }
}


static void taskCounter(void *arg)
{
    (void)arg;
    for(;;)
    {
        uint8_t *p = rtos_malloc(24);
        (void)rtos_waitPeriodic(RTOS_MS(10));
        ++ _counter;
        if(p != NULL)
        {
            uint8_t i;
            for(i=0; i<24; ++i)
                p[i] = (uint8_t)(i ^ _counter);
            for(i=0; i<24; ++i)
                if(p[i] != (uint8_t)(i ^ _counter))
                    ++ _heapErrors;
            rtos_free(p);
        }
        else
            ++ _heapErrors;
    }
}


static void taskSuper(void *arg)
{
    void *blocks[4];
    (void)arg;
    for(;;)
    {
        uint32_t before, after;
        uint8_t i;

        (void)rtos_waitPeriodic(RTOS_MS(100));

        /* Heap: hold a few blocks across a period, check the stats afterwards. */
        for(i=0; i<4; ++i)
            blocks[i] = rtos_malloc(8u + 8u * i);
        for(i=0; i<4; ++i)
            if(blocks[i] == NULL)
                ++ _heapErrors;

        /* Suspend task 1 for 50 ms: its counter must not move. */
        rtos_suspend(TASK_COUNTER);
        before = _counter;
        rtos_delay(RTOS_MS(50));
        after = _counter;
        rtos_resume(TASK_COUNTER);
        if(before == after)
            ++ _frozenOk;
        else
            ++ _frozenBad;

        for(i=0; i<4; ++i)
            rtos_free(blocks[i]);
        ++ _heapRounds;

#if TC08_STOP_AFTER_MS > 0
        if(rtos_getTick() >= RTOS_MS(TC08_STOP_AFTER_MS))
        {
            rtos_heapStats_t st;
            const uint32_t ms = rtos_getTick() * (1000ul / RTOS_CFG_TICK_HZ);
            const uint32_t us = rtos_getTimeUs();
            const uint32_t isr = _isrCount, waiter = _waiterCount, counter = _counter;
            rtos_heapStats(&st);
            uart_puts("tc08 isr=");           uart_putU32(isr);
            uart_puts(" waiter=");            uart_putU32(waiter);
            uart_puts(" counter=");           uart_putU32(counter);
            uart_puts(" frozen=");            uart_putU32(_frozenOk);
            uart_putc('/');                   uart_putU32(_frozenBad);
            uart_puts(" heapErrors=");        uart_putU32(_heapErrors);
            uart_puts(" heap free=");         uart_putU32((uint32_t)st.free);
            uart_putc('/');                   uart_putU32((uint32_t)st.total);
            uart_puts(" minFree=");           uart_putU32((uint32_t)st.minFree);
            uart_puts(" allocs=");            uart_putU32(st.allocs);
            uart_puts(" us=");                uart_putU32(us);
#if defined(BOARD_IS_ESP32)
            uart_puts(" tickIrqs=");          uart_putU32(rtos_port_tickInterrupts());
            uart_puts("/");                   uart_putU32(ms);
            tc_expectRange("tickIrqs below ticks", rtos_port_tickInterrupts(), 1, ms * 3 / 4);
#endif
            uart_puts("\r\n");
            tc_expectNear("isr", isr, ms * APPL_IRQ_HZ / 1000);
            /* The waiter sees every set, or several merged when it was preempted. */
            tc_expectRange("waiter", waiter, isr / 2, isr);
            /* 10 ms period, suspended 50 of every 100 ms, plus one overrun run after
               each resume (rtos_waitPeriodic re-bases instead of replaying periods). */
            tc_expectNear("counter", counter, ms / 20 + ms / 100);
            tc_expectRange("frozenBad", _frozenBad, 0, 0);
            tc_expectRange("frozenOk", _frozenOk, ms / 100 - 1, ms / 100);
            tc_expectRange("heapErrors", _heapErrors, 0, 0);
            /* Task 1 holds one block across its period; nothing else may be left. */
            tc_expect("heap free but one block", st.total - st.free <= 64u);
            tc_expectNear("us", us, ms * 1000);
            tc_finish("tc08");
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
    uart_puts("\r\nnornOS " RTOS_VERSION " tc08_features\r\n");

    rtos_init();
    rtos_heapInit(_heap, sizeof(_heap));
    rtos_createTask(TASK_SUPER,   taskSuper,   NULL, _stackSuper,   sizeof(_stackSuper));
    rtos_createTask(TASK_WAITER,  taskWaiter,  NULL, _stackWaiter,  sizeof(_stackWaiter));
    rtos_createTask(TASK_COUNTER, taskCounter, NULL, _stackCounter, sizeof(_stackCounter));

    board_appTimerStart(APPL_IRQ_HZ);
    rtos_start();
    return 0;
}
