/**
 * @file rtos_wdt.c
 * Watchdog: the hardware watchdog of the port, kicked from the idle task, and a software
 * layer on top that requires monitored tasks to check in. The two cover each other: a
 * task that stops checking in is reported by name with RTOS_PANIC_WATCHDOG while the
 * system still runs (so the record and the hook work), and a system where the idle task
 * never runs any more is reset by the hardware, which the next boot sees as
 * RTOS_RESET_WATCHDOG without a panic record.
 */

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"

#if RTOS_CFG_WATCHDOG


/*
 * Data definitions
 */

static rtos_taskmask_t _monitored = 0;
static rtos_tick_t _interval[RTOS_NUM_TASKS];
static rtos_tick_t _deadline[RTOS_NUM_TASKS];
static bool _hwStarted = false;


/*
 * Kernel internal
 */

void rtos_kernel_wdtIdle(void)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();
    const rtos_tick_t now = rtos_tickCount;
    uint8_t i;

    for(i=1; i<RTOS_NUM_TASKS; ++i)
    {
        if((_monitored & rtos_tcbAry[i].bit) != 0
           &&  (rtos_tickdiff_t)(now - _deadline[i]) > 0
          )
            rtos_panic(RTOS_PANIC_WATCHDOG, i, (uint32_t)(now - _deadline[i]));
    }
    if(_hwStarted)
        rtos_port_wdtKick();
    rtos_port_exitCritical(cs);
}


/*
 * Public API
 */

void rtos_wdtInit(uint32_t timeoutMs)
{
    RTOS_ASSERT(timeoutMs > 0);
    rtos_port_wdtStart(timeoutMs);
    _hwStarted = true;
}


void rtos_wdtMonitor(uint8_t task, rtos_tick_t maxInterval)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(task >= 1  &&  task < RTOS_NUM_TASKS);
    if(maxInterval == 0)
        _monitored &= (rtos_taskmask_t)~rtos_tcbAry[task].bit;
    else
    {
        _interval[task] = maxInterval;
        _deadline[task] = rtos_tickCount + maxInterval;
        _monitored |= rtos_tcbAry[task].bit;
    }
    rtos_port_exitCritical(cs);
}


void rtos_wdtCheckIn(void)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();
    const uint8_t id = rtos_pCurrent->prio;

    _deadline[id] = rtos_tickCount + _interval[id];
    rtos_port_exitCritical(cs);
}

#endif  /* RTOS_CFG_WATCHDOG */
