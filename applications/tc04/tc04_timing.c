/**
 * @file tc04_timing.c
 * Test case 4: timing figures of the kernel on the current board.
 *
 *   switch      a low priority task gives a semaphore a high priority task waits on, the
 *               high task takes it again at once: one round trip is two context switches
 *               plus the give and the take. Min and average per switch, in ns.
 *   isrLatency  the application timer interrupt reads its own counter on entry: time
 *               from the hardware event to the first line of the RTOS_ISR body, through
 *               the port's interrupt entry and dispatch. Average and max in us.
 *   jitter      a task with rtos_waitPeriodic(10 ms) at the highest priority measures
 *               how late it wakes relative to the ideal grid. Average and max in us.
 *
 * All three run at the same time, so the max values include the interference between
 * them, which is what matters. Printed once after TC04_STOP_AFTER_MS, with generous
 * limits as PASS criteria; the numbers themselves go into the README per board.
 */

#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "testresult.h"


/*
 * Defines
 */

#define TASK_JITTER     3
#define TASK_HIGH       2
#define TASK_LOW        1

#define APPL_IRQ_HZ     500ul
#if !BOARD_APP_TIMER_HZ_IS_EXACT(APPL_IRQ_HZ)
# error APPL_IRQ_HZ is not an exact rate of the application timer
#endif

#define SWITCH_ROUNDS   1000u
#define PERIOD_MS       10u

#ifndef TC04_STOP_AFTER_MS
# define TC04_STOP_AFTER_MS  3000ul
#endif

#if RTOS_PORT_MIN_STACK > 192u
# define STACK_SIZE     (2u * RTOS_PORT_MIN_STACK)
#else
# define STACK_SIZE     192u
#endif

/* Limits for PASS; far above the expected values, they catch gross regressions only. An
   emulator (TC_TOLERANCE_PERMILLE raised for it) stalls for a millisecond now and then
   when its host does, so its limits are wider still. */
#define LIMIT_SWITCH_NS         100000ul
#if TC_TOLERANCE_PERMILLE > 10u
# define LIMIT_ISR_LATENCY_US   3000ul
# define LIMIT_JITTER_US        3000ul
#else
# define LIMIT_ISR_LATENCY_US   300ul
# define LIMIT_JITTER_US        300ul
#endif


/*
 * Data definitions
 */

static uint8_t _stackJitter[STACK_SIZE];
static uint8_t _stackHigh[STACK_SIZE];
static uint8_t _stackLow[STACK_SIZE];

static rtos_sem_t _sem = RTOS_SEM_INIT(0);

static volatile uint32_t _isrCount = 0;
static volatile uint32_t _isrLatencySum = 0;    /* timer counts */
static volatile uint32_t _isrLatencyMax = 0;

static volatile uint32_t _jitterCount = 0;
static volatile uint32_t _jitterSum = 0;        /* run time counts */
static volatile uint32_t _jitterMax = 0;

static uint32_t _switchMin = 0;                 /* run time counts per switch */
static uint32_t _switchAvg = 0;


/*
 * Function implementation
 */

static uint32_t runTimeNow(void)
{
    uint32_t t;
    const rtos_port_cs_t cs = rtos_port_enterCritical();
    t = (uint32_t)rtos_port_getRunTimeCounter();
    rtos_port_exitCritical(cs);
    return t;
}

static uint32_t countsToNs(uint32_t counts, uint32_t hz)
{
    return (uint32_t)(((uint64_t)counts * 1000000000ull) / hz);
}

static uint32_t countsToUs(uint32_t counts, uint32_t hz)
{
    return (uint32_t)(((uint64_t)counts * 1000000ull) / hz);
}


RTOS_ISR(BOARD_APP_TIMER_VECT)
{
    const uint32_t e = board_appTimerElapsed();
    ++ _isrCount;
    _isrLatencySum += e;
    if(e > _isrLatencyMax)
        _isrLatencyMax = e;
}


static void taskJitter(void *arg)
{
    const uint32_t period = (uint32_t)(RTOS_RUNTIME_HZ / (1000ul / PERIOD_MS));
    uint32_t ideal;
    (void)arg;

    (void)rtos_waitPeriodic(RTOS_MS(PERIOD_MS));
    ideal = runTimeNow();
    for(;;)
    {
        uint32_t now, late;

        (void)rtos_waitPeriodic(RTOS_MS(PERIOD_MS));
        now = runTimeNow();
        ideal += period;
        late = (int32_t)(now - ideal) > 0 ? now - ideal : 0;
        ++ _jitterCount;
        _jitterSum += late;
        if(late > _jitterMax)
            _jitterMax = late;
    }
}


static void taskHigh(void *arg)
{
    (void)arg;
    for(;;)
        (void)rtos_semTake(&_sem, RTOS_FOREVER);
}


static void report(void)
{
    /* Snapshot first: printing takes tens of milliseconds and the counters keep going. */
    const uint32_t ms = rtos_getTick() * (1000ul / RTOS_CFG_TICK_HZ);
    const uint32_t isrCount = _isrCount;
    const uint32_t jitterCount = _jitterCount;
    const uint32_t isrAvg = isrCount ? _isrLatencySum / isrCount : 0;
    const uint32_t jitAvg = jitterCount ? _jitterSum / jitterCount : 0;
    const uint32_t switchMinNs = countsToNs(_switchMin, (uint32_t)RTOS_RUNTIME_HZ);
    const uint32_t switchAvgNs = countsToNs(_switchAvg, (uint32_t)RTOS_RUNTIME_HZ);
    const uint32_t isrAvgUs = countsToUs(isrAvg, (uint32_t)BOARD_APP_TIMER_COUNT_HZ);
    const uint32_t isrMaxUs = countsToUs(_isrLatencyMax, (uint32_t)BOARD_APP_TIMER_COUNT_HZ);
    const uint32_t jitAvgUs = countsToUs(jitAvg, (uint32_t)RTOS_RUNTIME_HZ);
    const uint32_t jitMaxUs = countsToUs(_jitterMax, (uint32_t)RTOS_RUNTIME_HZ);

    uart_puts("tc04 " BOARD_NAME " F_CPU=");  uart_putU32((uint32_t)F_CPU);
    uart_puts("\r\n  switch      min=");       uart_putU32(switchMinNs);
    uart_puts(" ns avg=");                     uart_putU32(switchAvgNs);
    uart_puts(" ns (");                        uart_putU32(SWITCH_ROUNDS);
    uart_puts(" round trips)\r\n  isrLatency  avg="); uart_putU32(isrAvgUs);
    uart_puts(" us max=");                     uart_putU32(isrMaxUs);
    uart_puts(" us (n=");                      uart_putU32(isrCount);
    uart_puts(")\r\n  jitter      avg=");      uart_putU32(jitAvgUs);
    uart_puts(" us max=");                     uart_putU32(jitMaxUs);
    uart_puts(" us (n=");                      uart_putU32(jitterCount);
    uart_puts(")\r\n  cpuLoad     ");          uart_putU32(rtos_getCpuLoad());
    uart_puts(" %\r\n");

    tc_expectRange("switchAvgNs", switchAvgNs, 1, LIMIT_SWITCH_NS);
    tc_expectNear("isrCount", isrCount, ms * APPL_IRQ_HZ / 1000);
    tc_expectRange("isrMaxUs", isrMaxUs, 0, LIMIT_ISR_LATENCY_US);
    tc_expectRange("jitterCount", jitterCount, ms / PERIOD_MS - 3, ms / PERIOD_MS);
    tc_expectRange("jitterMaxUs", jitMaxUs, 0, LIMIT_JITTER_US);
    tc_expectRange("overruns", rtos_getOverrunCount(TASK_JITTER, false), 0, 0);
    tc_expectRange("stackFree", rtos_getStackFree(TASK_JITTER), 32, STACK_SIZE);
    tc_finish("tc04");
}


static void taskLow(void *arg)
{
    uint32_t i, sum = 0, min = 0xFFFFFFFFul;
    (void)arg;

    rtos_delay(RTOS_MS(50));            /* let the others settle */
    for(i=0; i<SWITCH_ROUNDS; ++i)
    {
        const uint32_t t0 = runTimeNow();
        rtos_semGive(&_sem);            /* taskHigh runs, takes again, blocks: back here */
        {
            const uint32_t dt = runTimeNow() - t0;
            sum += dt;
            if(dt < min)
                min = dt;
        }
    }
    _switchMin = min / 2u;
    _switchAvg = sum / (2u * SWITCH_ROUNDS);

    for(;;)
    {
        rtos_delay(RTOS_MS(100));
#if TC04_STOP_AFTER_MS > 0
        if(rtos_getTick() >= RTOS_MS(TC04_STOP_AFTER_MS))
            report();
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
    uart_puts("\r\nnornOS " RTOS_VERSION " tc04_timing\r\n");

    rtos_init();
    rtos_createTask(TASK_JITTER, taskJitter, NULL, _stackJitter, sizeof(_stackJitter));
    rtos_createTask(TASK_HIGH,   taskHigh,   NULL, _stackHigh,   sizeof(_stackHigh));
    rtos_createTask(TASK_LOW,    taskLow,    NULL, _stackLow,    sizeof(_stackLow));

    board_appTimerStart(APPL_IRQ_HZ);
    rtos_start();
    return 0;
}
