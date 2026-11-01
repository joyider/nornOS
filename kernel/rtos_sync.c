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
    rtos_semInitMax(pSem, initialCount, 0xFFu);
}


void rtos_semInitMax(rtos_sem_t *pSem, uint8_t initialCount, uint8_t maxCount)
{
    RTOS_ASSERT(maxCount >= 1  &&  initialCount <= maxCount);
    pSem->count = initialCount;
    pSem->maxCount = maxCount;
    pSem->waiters = 0;
    pSem->lost = 0;
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
        got = !rtos_kernel_blockOn(RTOS_WAIT_SEM, pSem, timeout);
    }
    rtos_port_exitCritical(cs);
    return got;
}


void rtos_semGiveFromIsr(rtos_sem_t *pSem)
{
    if(pSem->waiters != 0)
        (void)rtos_kernel_releaseHighestWaiter(&pSem->waiters);
    else if(pSem->count < pSem->maxCount)
        ++ pSem->count;
    else if(pSem->lost < 0xFFu)
        ++ pSem->lost;                  /* binary semantics: a surplus give is dropped */
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
        pMutex->savedPrio = pCur->effPrio;
        if(pMutex->ceiling > pCur->effPrio)
            pCur->effPrio = pMutex->ceiling;
    }
    else
    {
        /* Ceiling too low or owner blocked while holding the mutex. Wait for the hand-over;
           the unlock makes us owner and raises our priority before we run again. */
        uint8_t owner = pMutex->owner;
        uint8_t n;

        /* Deadlock check: follow the owners of the mutexes the owners are waiting for. If
           the chain comes back to us, nobody in it can ever run again. */
        for(n=0; n<RTOS_NUM_TASKS; ++n)
        {
            const rtos_tcb_t * const pO = &rtos_tcbAry[owner];
            if(pO->waitKind != RTOS_WAIT_MUTEX)
                break;
            owner = ((const rtos_mutex_t *)pO->pWaitObj)->owner;
            if(owner == pCur->prio)
                rtos_panic(RTOS_PANIC_DEADLOCK, pCur->prio, pMutex->owner);
        }

        pMutex->waiters |= pCur->bit;
        pCur->pWaiters = &pMutex->waiters;
        (void)rtos_kernel_blockOn(RTOS_WAIT_MUTEX, pMutex, RTOS_FOREVER);
        RTOS_ASSERT(pMutex->owner == pCur->prio);
    }

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
    {
        /* Hand over to the highest waiter. It owns the mutex from this moment, so it runs
           at the ceiling from this moment: no task below the ceiling may get in between. */
        rtos_tcb_t *pNext;
        pMutex->owner = rtos_kernel_releaseHighestWaiter(&pMutex->waiters);
        pNext = &rtos_tcbAry[pMutex->owner];
        pMutex->savedPrio = pNext->effPrio;
        if(pMutex->ceiling > pNext->effPrio)
            pNext->effPrio = pMutex->ceiling;
    }
    else
        pMutex->owner = RTOS_NO_TASK;

    /* Dropping the effective priority may let a waiting higher priority task run, and
       a new owner at the ceiling runs before us unless we are at the ceiling ourselves. */
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}
