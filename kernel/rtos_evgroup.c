/**
 * @file rtos_evgroup.c
 * Event groups: a set of flags that any number of tasks wait on, as opposed to the
 * per task events of rtos_waitEvents. A waiter asks for any or all of a mask and may
 * take its bits out of the group when it is woken (RTOS_WAIT_CLEAR); the setter wakes
 * every waiter whose condition is met, highest priority first, and clears for each such
 * waiter in that order, so a bit one waiter clears is not seen by a lower waiter.
 *
 * The waiter's mask lives in the TCB's evtWait field, the ALL and CLEAR options in its
 * flags, and the result is handed over through pXfer, like a queue item.
 */

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"

#if RTOS_CFG_EVGROUPS

static inline rtos_evt_t match(rtos_evt_t bits, rtos_evt_t mask, uint8_t flags)
{
    const rtos_evt_t got = bits & mask;
    if(flags & RTOS_TF_WAIT_ALL)
        return got == mask ? mask : 0;
    return got;
}

void rtos_evgroupInit(rtos_evgroup_t *pG, rtos_evt_t initialBits)
{
    pG->bits = initialBits;
    pG->waiters = 0;
}

void rtos_evgroupSetFromIsr(rtos_evgroup_t *pG, rtos_evt_t bits)
{
    uint8_t i;

    pG->bits |= bits;
    if(pG->waiters == 0)
        return;
    for(i=RTOS_NUM_TASKS-1; i>=1; --i)
    {
        rtos_tcb_t * const pT = &rtos_tcbAry[i];
        if(pG->waiters & pT->bit)
        {
            const rtos_evt_t got = match(pG->bits, pT->evtWait, pT->flags);
            if(got != 0)
            {
                *(rtos_evt_t *)pT->pXfer = got;
                if(pT->flags & RTOS_TF_EVG_CLEAR)
                    pG->bits &= (rtos_evt_t)~got;
                pT->evtWait = 0;
                rtos_kernel_makeReady(pT);          /* clears it from pG->waiters */
            }
        }
    }
}

void rtos_evgroupSet(rtos_evgroup_t *pG, rtos_evt_t bits)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();
    rtos_evgroupSetFromIsr(pG, bits);
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}

void rtos_evgroupClear(rtos_evgroup_t *pG, rtos_evt_t bits)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();
    pG->bits &= (rtos_evt_t)~bits;
    rtos_port_exitCritical(cs);
}

rtos_evt_t rtos_evgroupGet(const rtos_evgroup_t *pG)
{
    return pG->bits;
}

rtos_evt_t rtos_evgroupWait(rtos_evgroup_t *pG, rtos_evt_t mask, uint8_t options, rtos_tick_t timeout)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    rtos_evt_t got;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(mask != 0);
    if(options & RTOS_WAIT_ALL)
        pCur->flags |= RTOS_TF_WAIT_ALL;
    else
        pCur->flags &= (uint8_t)~RTOS_TF_WAIT_ALL;

    got = match(pG->bits, mask, pCur->flags);
    if(got != 0)
    {
        if(options & RTOS_WAIT_CLEAR)
            pG->bits &= (rtos_evt_t)~got;
    }
    else if(timeout != RTOS_NO_WAIT)
    {
        if(options & RTOS_WAIT_CLEAR)
            pCur->flags |= RTOS_TF_EVG_CLEAR;
        else
            pCur->flags &= (uint8_t)~RTOS_TF_EVG_CLEAR;
        pCur->evtWait = mask;
        pCur->pXfer = &got;
        pG->waiters |= pCur->bit;
        pCur->pWaiters = &pG->waiters;
        if(rtos_kernel_blockOn(RTOS_WAIT_EVGROUP, pG, timeout))
            got = 0;                                /* timed out: nothing handed over */
        pCur->evtWait = 0;
        pCur->flags &= (uint8_t)~RTOS_TF_EVG_CLEAR;
    }
    rtos_port_exitCritical(cs);
    return got;
}

#endif  /* RTOS_CFG_EVGROUPS */
