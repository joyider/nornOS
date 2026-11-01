/**
 * @file rtos_timer.c
 * Software timers. A timer is a statically allocated object with a callback. Active
 * timers sit in one list sorted by expiry tick, so the tick ISR only inspects the head.
 * Periodic timers are re-armed from the previous expiry, not from "now", so they do not
 * drift.
 *
 * Where the callback runs depends on RTOS_CFG_TIMER_TASK_PRIO:
 *   0:  inside the tick interrupt with interrupts disabled. Keep it short and only use
 *       kernel functions ending in FromIsr, or rtos_timerStart/Stop, inside it.
 *   >0: in the timer task of that priority, created by rtos_start. The tick only moves
 *       expired timers to a pending list and notifies the task; callbacks may then use
 *       the whole API, at the cost of latency equal to the timer task's scheduling
 *       latency. Periodic timers are re-armed in the tick either way, so they stay
 *       drift free; a timer that expires again before the task got to it runs once.
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

#if RTOS_CFG_TIMER_TASK_PRIO > 0
/** Expired timers waiting for the timer task, in expiry order. */
static rtos_timer_t *_pPendingHead = NULL;
static rtos_timer_t *_pPendingTail = NULL;
static uint8_t _timerStack[RTOS_CFG_TIMER_TASK_STACK];
#endif


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


#if RTOS_CFG_TIMER_TASK_PRIO > 0
/** Remove from the pending list if present. Interrupts disabled. */
static void removePending(rtos_timer_t *pT)
{
    rtos_timer_t **ppLink = &_pPendingHead;
    rtos_timer_t *pPrev = NULL;

    while(*ppLink != NULL  &&  *ppLink != pT)
    {
        pPrev = *ppLink;
        ppLink = &(*ppLink)->pNextPending;
    }
    if(*ppLink == pT)
    {
        *ppLink = pT->pNextPending;
        if(_pPendingTail == pT)
            _pPendingTail = pPrev;
    }
    pT->pending = 0;
}


/** The timer task: runs the callbacks of expired timers with interrupts enabled. */
static void timerTask(void *arg)
{
    (void)arg;
    for(;;)
    {
        rtos_timer_t *pT;

        (void)rtos_notifyTake(RTOS_FOREVER);
        for(;;)
        {
            rtos_port_cs_t cs = rtos_port_enterCritical();
            pT = _pPendingHead;
            if(pT != NULL)
            {
                _pPendingHead = pT->pNextPending;
                if(_pPendingHead == NULL)
                    _pPendingTail = NULL;
                pT->pending = 0;
            }
            rtos_port_exitCritical(cs);
            if(pT == NULL)
                break;
            RTOS_TRACE(RTOS_TRACE_TIMER, (uintptr_t)pT, 1);
            pT->callback(pT->arg);
        }
    }
}


void rtos_kernel_timerTaskCreate(void)
{
    rtos_createTask(RTOS_CFG_TIMER_TASK_PRIO, timerTask, NULL, _timerStack, sizeof(_timerStack));
}
#endif


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

#if RTOS_CFG_TIMER_TASK_PRIO > 0
        if(!pT->pending)
        {
            pT->pending = 1;
            pT->pNextPending = NULL;
            if(_pPendingTail != NULL)
                _pPendingTail->pNextPending = pT;
            else
                _pPendingHead = pT;
            _pPendingTail = pT;
        }
        rtos_notifyFromIsr(RTOS_CFG_TIMER_TASK_PRIO, 1, RTOS_NOTIFY_INC);
#else
        RTOS_TRACE(RTOS_TRACE_TIMER, (uintptr_t)pT, 0);
        pT->callback(pT->arg);
#endif
    }
}


rtos_tick_t rtos_kernel_timerTicksToNext(void)
{
    if(_pHead == NULL)
        return RTOS_FOREVER;
    {
        const rtos_tickdiff_t d = (rtos_tickdiff_t)(_pHead->expiresAt - rtos_tickCount);
        return d <= 0 ? 0u : (rtos_tick_t)d;
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
    pT->pending = 0;
    pT->pNextPending = NULL;
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
#if RTOS_CFG_TIMER_TASK_PRIO > 0
    if(pT->pending)
        removePending(pT);
#endif
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
