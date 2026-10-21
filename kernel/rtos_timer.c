/**
 * @file rtos_timer.c
 * Software timers. A timer is a statically allocated object with a callback. Active
 * timers sit in one list sorted by expiry tick, so the tick ISR only inspects the head.
 * Periodic timers are re-armed from the previous expiry, not from "now", so they do not
 * drift.
 *
 * The callback runs inside the tick interrupt with interrupts disabled. Keep it short and
 * only use kernel functions ending in FromIsr, or rtos_timerStart/Stop, inside it. The
 * usual pattern is to post an event, give a semaphore or notify a task and let that task
 * do the work.
 */

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"

#if RTOS_CFG_SW_TIMERS


/*
 * Data definitions
 */

/** Head of the sorted list of active timers. */
static rtos_timer_t *_pHead = NULL;


/*
 * Local functions
 */

/** Insert into the sorted list. Interrupts disabled. */
static void insert(rtos_timer_t *pT)
{
    rtos_timer_t **ppLink = &_pHead;

    while(*ppLink != NULL  &&  (rtos_tickdiff_t)((*ppLink)->expiresAt - pT->expiresAt) <= 0)
        ppLink = &(*ppLink)->pNext;
    pT->pNext = *ppLink;
    *ppLink = pT;
    pT->active = 1;
}


/** Remove from the list if present. Interrupts disabled. */
static void remove(rtos_timer_t *pT)
{
    rtos_timer_t **ppLink = &_pHead;

    while(*ppLink != NULL  &&  *ppLink != pT)
        ppLink = &(*ppLink)->pNext;
    if(*ppLink == pT)
        *ppLink = pT->pNext;
    pT->active = 0;
}


/*
 * Kernel internal
 */

/**
 * Serve expired timers. Called from the tick with interrupts disabled.
 */
void rtos_kernel_timerTick(rtos_tick_t now)
{
    while(_pHead != NULL  &&  (rtos_tickdiff_t)(_pHead->expiresAt - now) <= 0)
    {
        rtos_timer_t * const pT = _pHead;

        _pHead = pT->pNext;
        if(pT->period != 0)
        {
            pT->expiresAt += pT->period;
            insert(pT);
        }
        else
            pT->active = 0;

        pT->callback(pT->arg);
    }
}


/*
 * Public API
 */

void rtos_timerInit(rtos_timer_t *pT, rtos_timerFunc_t callback, void *arg)
{
    RTOS_ASSERT(callback != NULL);
    pT->pNext = NULL;
    pT->expiresAt = 0;
    pT->period = 0;
    pT->callback = callback;
    pT->arg = arg;
    pT->active = 0;
}


void rtos_timerStart(rtos_timer_t *pT, rtos_tick_t delay, rtos_tick_t period)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(pT->callback != NULL);
    if(pT->active)
        remove(pT);
    pT->period = period;
    pT->expiresAt = rtos_tickCount + delay;
    insert(pT);
    rtos_port_exitCritical(cs);
}


void rtos_timerStop(rtos_timer_t *pT)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    if(pT->active)
        remove(pT);
    rtos_port_exitCritical(cs);
}


bool rtos_timerIsActive(const rtos_timer_t *pT)
{
    return pT->active != 0;
}


rtos_tick_t rtos_timerRemaining(const rtos_timer_t *pT)
{
    rtos_tick_t r = 0;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    if(pT->active)
    {
        const rtos_tickdiff_t d = (rtos_tickdiff_t)(pT->expiresAt - rtos_tickCount);
        r = d > 0 ? (rtos_tick_t)d : 0;
    }
    rtos_port_exitCritical(cs);
    return r;
}

#endif /* RTOS_CFG_SW_TIMERS */
