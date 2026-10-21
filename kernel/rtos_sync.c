/**
 * @file rtos_sync.c
 * Counting semaphores and immediate priority ceiling mutexes.
 *
 * Semaphore: a token count plus a bitmask of waiting tasks. A give hands the token straight
 * to the highest priority waiter, if any, otherwise increments the count.
 *
 * Mutex: while a task holds the mutex its effective priority is the mutex ceiling. Tasks
 * with a priority up to the ceiling cannot preempt it, so with a correctly chosen ceiling
 * (>= the priority of every task that uses the mutex) no task ever has to block on the
 * mutex and deadlocks are impossible. If the ceiling is too low, or if the owner blocks
 * while holding the mutex, a contender falls back to waiting; the unlock then hands the
 * mutex over to the highest priority waiter.
 */

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"


/**
 * Pick the highest priority waiter in a mask, remove it and make it ready. Interrupts
 * disabled. The mask must not be 0.
 *   @return The task id of the released waiter.
 */
uint8_t rtos_kernel_releaseHighestWaiter(rtos_taskmask_t *pWaiters)
{
    uint8_t i = RTOS_NUM_TASKS;

    /* Linear scan from the top; waiter masks are tiny. */
    do
    {
        -- i;
        if(*pWaiters & rtos_tcbAry[i].bit)
        {
            /* makeReady clears the bit via pWaiters. */
            RTOS_ASSERT(rtos_tcbAry[i].pWaiters == pWaiters);
            rtos_kernel_makeReady(&rtos_tcbAry[i]);
            return i;
        }
    }
    while(i > 1);

    RTOS_ASSERT(false);
    return RTOS_NO_TASK;
}


/*
 * Semaphore
 */

void rtos_semInit(rtos_sem_t *pSem, uint8_t initialCount)
{
    pSem->count = initialCount;
    pSem->waiters = 0;
}


bool rtos_semTake(rtos_sem_t *pSem, rtos_tick_t timeout)
{
    bool got;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    if(pSem->count > 0)
    {
        -- pSem->count;
        got = true;
    }
    else if(timeout == RTOS_NO_WAIT)
    {
        got = false;
    }
    else
    {
        rtos_tcb_t * const pCur = rtos_pCurrent;

        pSem->waiters |= pCur->bit;
        pCur->pWaiters = &pSem->waiters;

        /* If we are woken by a give, the token was handed to us directly and the count is
           untouched. If we time out, the tick ISR removed us from the waiter mask. */
        got = !rtos_kernel_blockCurrent(timeout);
    }
    rtos_port_exitCritical(cs);
    return got;
}


void rtos_semGiveFromIsr(rtos_sem_t *pSem)
{
    if(pSem->waiters != 0)
        (void)rtos_kernel_releaseHighestWaiter(&pSem->waiters);
    else
    {
        RTOS_ASSERT(pSem->count < 0xFFu);
        ++ pSem->count;
    }
}


void rtos_semGive(rtos_sem_t *pSem)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    rtos_semGiveFromIsr(pSem);
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}


/*
 * Mutex
 */

void rtos_mutexInit(rtos_mutex_t *pMutex, uint8_t ceilingPrio)
{
    pMutex->ceiling = ceilingPrio;
    pMutex->owner = RTOS_NO_TASK;
    pMutex->savedPrio = 0;
    pMutex->waiters = 0;
}


void rtos_mutexLock(rtos_mutex_t *pMutex)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(pCur->prio != RTOS_IDLE_TASK);
    RTOS_ASSERT(pMutex->owner != pCur->prio);           /* Not recursive. */
    RTOS_ASSERT(pMutex->ceiling >= pCur->prio);         /* Ceiling misconfigured. */

    if(pMutex->owner == RTOS_NO_TASK)
    {
        pMutex->owner = pCur->prio;
    }
    else
    {
        /* Ceiling too low or owner blocked while holding the mutex. Wait for the hand-over. */
        pMutex->waiters |= pCur->bit;
        pCur->pWaiters = &pMutex->waiters;
        (void)rtos_kernel_blockCurrent(RTOS_FOREVER);
        RTOS_ASSERT(pMutex->owner == pCur->prio);
    }

    pMutex->savedPrio = pCur->effPrio;
    if(pMutex->ceiling > pCur->effPrio)
        pCur->effPrio = pMutex->ceiling;

    rtos_port_exitCritical(cs);
}


void rtos_mutexUnlock(rtos_mutex_t *pMutex)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(pMutex->owner == pCur->prio);

    /* LIFO unlock order is required when holding several mutexes. */
    RTOS_ASSERT(pMutex->savedPrio <= pCur->effPrio);
    pCur->effPrio = pMutex->savedPrio;

    if(pMutex->waiters != 0)
        pMutex->owner = rtos_kernel_releaseHighestWaiter(&pMutex->waiters);
    else
        pMutex->owner = RTOS_NO_TASK;

    /* Dropping the effective priority may let a waiting higher priority task run. */
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}
