/**
 * @file rtos_notify.c
 * Task notifications: one 16 bit value per task that any task or ISR can set, OR into or
 * increment, and that the owning task can wait for. No object to declare, no waiter list,
 * no copying; this is the cheapest way to signal a task, e.g. from an interrupt.
 *
 *   rtos_notifyWait  waits for any notification and takes the whole value (mailbox/flags
 *                    style).
 *   rtos_notifyTake  waits until the value is non-zero and decrements it (counting
 *                    semaphore style).
 */

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"


/**
 * Apply a notification to a task and wake it if it is waiting for one. Interrupts
 * disabled.
 */
static inline void notifyInner(rtos_tcb_t *pT, uint16_t value, uint8_t op)
{
    switch(op)
    {
    case RTOS_NOTIFY_OR:
        pT->notifyValue |= value;
        break;
    case RTOS_NOTIFY_INC:
        if(pT->notifyValue < 0xFFFFu)
            ++ pT->notifyValue;
        break;
    default:
        pT->notifyValue = value;
        break;
    }
    pT->flags |= RTOS_TF_NOTIFY_PENDING;

    if(pT->flags & RTOS_TF_WAIT_NOTIFY)
    {
        /* A task in rtos_notifyTake is only interested in a non-zero value. */
        if((pT->flags & RTOS_TF_NOTIFY_TAKE) == 0  ||  pT->notifyValue != 0)
        {
            pT->flags &= (uint8_t)~RTOS_TF_WAIT_NOTIFY;
            rtos_kernel_makeReady(pT);
        }
    }
}


void rtos_notifyFromIsr(uint8_t taskId, uint16_t value, uint8_t op)
{
    RTOS_ASSERT(taskId >= 1  &&  taskId < RTOS_NUM_TASKS);
    notifyInner(&rtos_tcbAry[taskId], value, op);
}


void rtos_notify(uint8_t taskId, uint16_t value, uint8_t op)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    rtos_notifyFromIsr(taskId, value, op);
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}


bool rtos_notifyWait(uint16_t *pValue, rtos_tick_t timeout)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    bool ok;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    if((pCur->flags & RTOS_TF_NOTIFY_PENDING) == 0  &&  timeout != RTOS_NO_WAIT)
    {
        pCur->flags = (uint8_t)((pCur->flags & ~RTOS_TF_NOTIFY_TAKE) | RTOS_TF_WAIT_NOTIFY);
        (void)rtos_kernel_blockCurrent(timeout);
        pCur->flags &= (uint8_t)~RTOS_TF_WAIT_NOTIFY;
    }
    ok = (pCur->flags & RTOS_TF_NOTIFY_PENDING) != 0;
    if(ok)
    {
        if(pValue != NULL)
            *pValue = pCur->notifyValue;
        pCur->notifyValue = 0;
        pCur->flags &= (uint8_t)~RTOS_TF_NOTIFY_PENDING;
    }
    rtos_port_exitCritical(cs);
    return ok;
}


bool rtos_notifyTake(rtos_tick_t timeout)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    bool ok;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    if(pCur->notifyValue == 0  &&  timeout != RTOS_NO_WAIT)
    {
        pCur->flags |= RTOS_TF_WAIT_NOTIFY | RTOS_TF_NOTIFY_TAKE;
        (void)rtos_kernel_blockCurrent(timeout);
        pCur->flags &= (uint8_t)~(RTOS_TF_WAIT_NOTIFY | RTOS_TF_NOTIFY_TAKE);
    }
    ok = pCur->notifyValue != 0;
    if(ok)
    {
        -- pCur->notifyValue;
        if(pCur->notifyValue == 0)
            pCur->flags &= (uint8_t)~RTOS_TF_NOTIFY_PENDING;
    }
    rtos_port_exitCritical(cs);
    return ok;
}
