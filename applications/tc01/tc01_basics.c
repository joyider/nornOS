/**
 * @file tc01_basics.c
 * Test case 1 for the v2 kernel: exercises every primitive of step one on real hardware
 * or in simavr.
 *
 *   task 3 "periodic":  runs every 10 ms (rtos_waitPeriodic), toggles the LED on PB7,
 *                       updates a shared counter under a ceiling mutex and once a second
 *                       posts an event to the worker.
 *   task 2 "worker":    waits for the event with a timeout, prints a status line over
 *                       UART0 and stops the test after TC01_STOP_AFTER_MS.
 *   task 1 "isrConsumer": blocks on a semaphore that an application interrupt (timer 4
 *                       compare at 500 Hz) gives from inside an RTOS_ISR.
 *   idle:               counts its loops and sleeps.
 *
 * Expected output once per second, e.g.
 *   t=1000 us=1000123 periodic=100 isr=500 semTimeouts=0 idle=... ovr=0 stack=...
 */

#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "testresult.h"

static const gpio_pin_t _pinLed = BOARD_PIN_LED;


/*
 * Defines
 */

#define TASK_ISR_CONSUMER   1u
#define TASK_WORKER         2u
#define TASK_PERIODIC       3u

#define EVT_SECOND          0x0001u

#define PERIOD_MS           10u
#define APPL_IRQ_HZ         500ul
#if !BOARD_APP_TIMER_HZ_IS_EXACT(APPL_IRQ_HZ)
# error APPL_IRQ_HZ is not an exact rate of timer 2
#endif

/** Stop the test (halt the CPU, which ends a simavr run) after this time. 0 runs forever,
    which is what you want on real hardware. */
#ifndef TC01_STOP_AFTER_MS
# define TC01_STOP_AFTER_MS  3000ul
#endif

#if RTOS_PORT_MIN_STACK > 192u
# define STACK_SIZE          (2u * RTOS_PORT_MIN_STACK)
#else
# define STACK_SIZE          192u
#endif


/*
 * Data definitions
 */

static uint8_t _stackIsrConsumer[STACK_SIZE];
static uint8_t _stackWorker[STACK_SIZE];
static uint8_t _stackPeriodic[STACK_SIZE];

static rtos_sem_t _semIsr = RTOS_SEM_INIT(0);
static rtos_mutex_t _mutexShared = RTOS_MUTEX_INIT(TASK_PERIODIC);

static struct
{
    uint32_t periodicCount;
    uint32_t isrCount;
    uint32_t semTimeouts;
} _shared;

static volatile uint32_t _idleLoops = 0;


/*
 * Function implementation
 */

/** Application interrupt: timer 2 compare A at APPL_IRQ_HZ. */
RTOS_ISR(BOARD_APP_TIMER_VECT)
{
    rtos_semGiveFromIsr(&_semIsr);
}


static void startApplInterrupt(void)
{
    board_appTimerStart(APPL_IRQ_HZ);
}


void tc01_idleHook(void)
{
    ++ _idleLoops;
}


static void taskPeriodic(void *arg)
{
    uint16_t n = 0;
    (void)arg;

    gpio_output(&_pinLed);
    for(;;)
    {
        (void)rtos_waitPeriodic(RTOS_MS(PERIOD_MS));

        gpio_toggle(&_pinLed);

        rtos_mutexLock(&_mutexShared);
        ++ _shared.periodicCount;
        rtos_mutexUnlock(&_mutexShared);

        if(++n >= 1000u / PERIOD_MS)
        {
            n = 0;
            rtos_postEvents(TASK_WORKER, EVT_SECOND);
        }
    }
}


static void taskIsrConsumer(void *arg)
{
    (void)arg;
    for(;;)
    {
        const bool got = rtos_semTake(&_semIsr, RTOS_MS(100));

        rtos_mutexLock(&_mutexShared);
        if(got)
            ++ _shared.isrCount;
        else
            ++ _shared.semTimeouts;
        rtos_mutexUnlock(&_mutexShared);
    }
}


static void printStatus(void)
{
    uint32_t periodic, isr, semTimeouts, idle;
    const uint32_t us = rtos_getTimeUs();

    rtos_mutexLock(&_mutexShared);
    periodic = _shared.periodicCount;
    isr = _shared.isrCount;
    semTimeouts = _shared.semTimeouts;
    rtos_mutexUnlock(&_mutexShared);
    idle = _idleLoops;

    uart_putsP(PSTR("t="));           uart_putU32(rtos_getTick());
    uart_putsP(PSTR(" us="));         uart_putU32(us);
    uart_putsP(PSTR(" periodic="));   uart_putU32(periodic);
    uart_putsP(PSTR(" isr="));        uart_putU32(isr);
    uart_putsP(PSTR(" semTimeouts=")); uart_putU32(semTimeouts);
    uart_putsP(PSTR(" idle="));       uart_putU32(idle);
    uart_putsP(PSTR(" ovr="));        uart_putU32(rtos_getOverrunCount(TASK_PERIODIC, false));
    uart_putsP(PSTR(" stack="));      uart_putU32(rtos_getStackFree(TASK_ISR_CONSUMER));
    uart_putc('/');                   uart_putU32(rtos_getStackFree(TASK_WORKER));
    uart_putc('/');                   uart_putU32(rtos_getStackFree(TASK_PERIODIC));
    uart_putsP(PSTR("\r\n"));
}


/** Expected after TC01_STOP_AFTER_MS: 100 periodic wakes and 500 ISR events per second,
    no semaphore timeouts, no overruns, time base within 0.5 percent, stacks intact. */
static void finish(void)
{
    const uint32_t ms = TC01_STOP_AFTER_MS;
    const uint32_t us = rtos_getTimeUs();

    rtos_mutexLock(&_mutexShared);
    tc_expectNear("periodic", _shared.periodicCount, ms / PERIOD_MS);
    tc_expectNear("isr", _shared.isrCount, ms * APPL_IRQ_HZ / 1000);
    tc_expectRange("semTimeouts", _shared.semTimeouts, 0, 0);
    rtos_mutexUnlock(&_mutexShared);
    tc_expectNear("us", us, ms * 1000);
    tc_expectRange("overruns", rtos_getOverrunCount(TASK_PERIODIC, false), 0, 0);
    tc_expectRange("idleLoops", _idleLoops, 1, 0xFFFFFFFFul);
    tc_expectRange("stackFree1", rtos_getStackFree(TASK_ISR_CONSUMER), 32, STACK_SIZE);
    tc_expectRange("stackFree2", rtos_getStackFree(TASK_WORKER), 32, STACK_SIZE);
    tc_expectRange("stackFree3", rtos_getStackFree(TASK_PERIODIC), 32, STACK_SIZE);
    tc_finish("tc01");
}


static void taskWorker(void *arg)
{
    (void)arg;
    for(;;)
    {
        const rtos_evt_t got = rtos_waitEvents(EVT_SECOND, RTOS_WAIT_ANY, RTOS_MS(1500));

        if(got == 0)
            uart_putsP(PSTR("worker: timeout, periodic task is not running!\r\n"));
        else
            printStatus();

#if TC01_STOP_AFTER_MS > 0
        if(rtos_getTick() >= RTOS_MS(TC01_STOP_AFTER_MS))
            finish();
#endif
    }
}


/** Report assertion failures over the UART before halting. */
void rtos_assertFailed(const char *file, uint16_t line)
{
    (void)rtos_port_enterCritical();
    uart_putsP(PSTR("\r\nASSERTION FAILED: "));
    uart_putsP(file);
    uart_putsP(PSTR(", line "));
    uart_putU32(line);
    uart_putsP(PSTR("\r\n"));
    rtos_port_halt();
}


int main(void)
{
    uart_init(115200);
    uart_putsP(PSTR("\r\nnornOS " RTOS_VERSION " tc01_basics\r\n"));

    rtos_init();
    rtos_createTask(TASK_ISR_CONSUMER, taskIsrConsumer, NULL, _stackIsrConsumer, sizeof(_stackIsrConsumer));
    rtos_createTask(TASK_WORKER,       taskWorker,      NULL, _stackWorker,      sizeof(_stackWorker));
    rtos_createTask(TASK_PERIODIC,     taskPeriodic,    NULL, _stackPeriodic,    sizeof(_stackPeriodic));

    startApplInterrupt();
    rtos_start();
    return 0;
}
