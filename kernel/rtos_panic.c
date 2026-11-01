/**
 * @file rtos_panic.c
 * The kernel's one way to stop: rtos_panic records why, gives the application a chance
 * to report, then halts or resets. The record lives in memory the port keeps across a
 * reset, so that after a reset the application can read what happened and act on it
 * (log it, degrade, or just carry on).
 */

#include "rtos.h"
#include "rtos_kernel.h"


/*
 * Data definitions
 */

rtos_panicRecord_t rtos_panicRecord RTOS_PORT_NOINIT;

/** The record found at boot, taken out of rtos_panicRecord by rtos_init. */
static rtos_panicInfo_t _lastPanic;
static bool _haveLastPanic = false;


/*
 * Local functions
 */

static uint32_t checksum(const rtos_panicInfo_t *pInfo)
{
    const uint8_t *p = (const uint8_t *)pInfo;
    uint32_t sum = 0x5A5A5A5Aul;
    size_t i;
    for(i=0; i<sizeof(*pInfo); ++i)
        sum = sum * 31u + p[i];
    return sum;
}


/*
 * Kernel internal
 */

void rtos_kernel_panicInit(void)
{
    if(rtos_panicRecord.magic == RTOS_PANIC_MAGIC
       &&  rtos_panicRecord.check == checksum(&rtos_panicRecord.info)
      )
    {
        _lastPanic = rtos_panicRecord.info;
        _haveLastPanic = true;
    }
    rtos_panicRecord.magic = 0;
    rtos_panicRecord.check = 0;
}


/*
 * Public API
 */

void rtos_panic(uint8_t reason, uint32_t arg1, uint32_t arg2)
{
    rtos_panicInfo_t * const pInfo = &rtos_panicRecord.info;

    (void)rtos_port_enterCritical();
    pInfo->reason = reason;
    pInfo->task = rtos_pCurrent->prio;
    pInfo->arg1 = arg1;
    pInfo->arg2 = arg2;
    pInfo->tick = rtos_tickCount;
    if(reason != RTOS_PANIC_ASSERT)
    {
        pInfo->line = 0;
        pInfo->pFile = NULL;
    }
    rtos_panicRecord.magic = RTOS_PANIC_MAGIC;
    rtos_panicRecord.check = checksum(pInfo);

    RTOS_TRACE(RTOS_TRACE_PANIC, reason, arg1);
    rtos_panicHook(pInfo);
#if RTOS_CFG_PANIC_ACTION
    rtos_port_reset();
#else
    rtos_port_halt();
#endif
}


bool rtos_getLastPanic(rtos_panicInfo_t *pInfo)
{
    if(_haveLastPanic  &&  pInfo != NULL)
        *pInfo = _lastPanic;
    return _haveLastPanic;
}


uint8_t rtos_getResetCause(void)
{
    return rtos_port_getResetCause();
}


RTOS_PORT_WEAK void rtos_panicHook(const rtos_panicInfo_t *pInfo)
{
    (void)pInfo;
}


RTOS_PORT_WEAK void rtos_assertFailed(const char *file, uint16_t line)
{
    rtos_panicRecord.info.pFile = file;
    rtos_panicRecord.info.line = line;
    rtos_panic(RTOS_PANIC_ASSERT, line, 0);
}
