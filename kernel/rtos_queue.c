/**
 * @file rtos_queue.c
 * Message queue and mailbox.
 *
 * Both copy items by value and both use direct hand-over: if a task is blocked waiting for
 * an item, a send copies the item straight into that task's buffer (the TCB's pXfer) and
 * makes it ready, so the item can never be taken by a third task in between. Likewise a
 * receive from a full queue moves the item of the highest priority blocked sender into
 * the freed slot. Consequently a blocked task never has to re-check and loop.
 *
 * Invariants: readers != 0 implies the queue (or mailbox) is empty; writers != 0 implies
 * the queue is full.
 */

#include <string.h>

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"


/*
 * Local functions
 */

static inline uint8_t *slot(const rtos_queue_t *pQ, uint8_t idx)
{
    return pQ->buf + (uint16_t)idx * pQ->itemSize;
}


/**
 * Store an item, or hand it to a waiting reader. Interrupts disabled.
 *   @return false if the queue is full.
 */
static bool enqueue(rtos_queue_t *pQ, const void *pItem)
{
    if(pQ->readers != 0)
    {
        const uint8_t id = rtos_kernel_releaseHighestWaiter(&pQ->readers);
        memcpy(rtos_tcbAry[id].pXfer, pItem, pQ->itemSize);
        return true;
    }
    if(pQ->count >= pQ->capacity)
        return false;

    memcpy(slot(pQ, pQ->tail), pItem, pQ->itemSize);
    if(++pQ->tail >= pQ->capacity)
        pQ->tail = 0;
    ++ pQ->count;
    return true;
}


/**
 * Take the oldest item and refill the slot from a waiting writer, if any. Interrupts
 * disabled.
 *   @return false if the queue is empty.
 */
static bool dequeue(rtos_queue_t *pQ, void *pItem)
{
    if(pQ->count == 0)
        return false;

    memcpy(pItem, slot(pQ, pQ->head), pQ->itemSize);
    if(++pQ->head >= pQ->capacity)
        pQ->head = 0;
    -- pQ->count;

    if(pQ->writers != 0)
    {
        const uint8_t id = rtos_kernel_releaseHighestWaiter(&pQ->writers);
        memcpy(slot(pQ, pQ->tail), rtos_tcbAry[id].pXfer, pQ->itemSize);
        if(++pQ->tail >= pQ->capacity)
            pQ->tail = 0;
        ++ pQ->count;
    }
    return true;
}


/*
 * Queue API
 */

void rtos_queueInit(rtos_queue_t *pQ, uint8_t *storage, uint8_t itemSize, uint8_t capacity)
{
    RTOS_ASSERT(storage != NULL  &&  itemSize > 0  &&  capacity > 0);
    pQ->buf = storage;
    pQ->itemSize = itemSize;
    pQ->capacity = capacity;
    pQ->count = 0;
    pQ->head = 0;
    pQ->tail = 0;
    pQ->readers = 0;
    pQ->writers = 0;
}


bool rtos_queueSend(rtos_queue_t *pQ, const void *pItem, rtos_tick_t timeout)
{
    bool ok;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    ok = enqueue(pQ, pItem);
    if(!ok  &&  timeout != RTOS_NO_WAIT)
    {
        rtos_tcb_t * const pCur = rtos_pCurrent;

        pCur->pXfer = (void *)pItem;
        pQ->writers |= pCur->bit;
        pCur->pWaiters = &pQ->writers;
        ok = !rtos_kernel_blockCurrent(timeout);
    }
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
    return ok;
}


bool rtos_queueSendFromIsr(rtos_queue_t *pQ, const void *pItem)
{
    return enqueue(pQ, pItem);
}


bool rtos_queueReceive(rtos_queue_t *pQ, void *pItem, rtos_tick_t timeout)
{
    bool ok;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    ok = dequeue(pQ, pItem);
    if(!ok  &&  timeout != RTOS_NO_WAIT)
    {
        rtos_tcb_t * const pCur = rtos_pCurrent;

        pCur->pXfer = pItem;
        pQ->readers |= pCur->bit;
        pCur->pWaiters = &pQ->readers;
        ok = !rtos_kernel_blockCurrent(timeout);
    }
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
    return ok;
}


bool rtos_queueReceiveFromIsr(rtos_queue_t *pQ, void *pItem)
{
    return dequeue(pQ, pItem);
}


uint8_t rtos_queueCount(const rtos_queue_t *pQ)
{
    return pQ->count;
}


/*
 * Mailbox API
 */

void rtos_mboxInit(rtos_mbox_t *pMb, uint8_t *storage, uint8_t itemSize)
{
    RTOS_ASSERT(storage != NULL  &&  itemSize > 0);
    pMb->buf = storage;
    pMb->itemSize = itemSize;
    pMb->full = 0;
    pMb->readers = 0;
}


bool rtos_mboxPostFromIsr(rtos_mbox_t *pMb, const void *pItem)
{
    bool overwritten = false;

    if(pMb->readers != 0)
    {
        const uint8_t id = rtos_kernel_releaseHighestWaiter(&pMb->readers);
        memcpy(rtos_tcbAry[id].pXfer, pItem, pMb->itemSize);
    }
    else
    {
        overwritten = pMb->full != 0;
        memcpy(pMb->buf, pItem, pMb->itemSize);
        pMb->full = 1;
    }
    return overwritten;
}


bool rtos_mboxPost(rtos_mbox_t *pMb, const void *pItem)
{
    bool overwritten;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    overwritten = rtos_mboxPostFromIsr(pMb, pItem);
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
    return overwritten;
}


bool rtos_mboxFetch(rtos_mbox_t *pMb, void *pItem, rtos_tick_t timeout)
{
    bool ok;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    if(pMb->full)
    {
        memcpy(pItem, pMb->buf, pMb->itemSize);
        pMb->full = 0;
        ok = true;
    }
    else if(timeout == RTOS_NO_WAIT)
    {
        ok = false;
    }
    else
    {
        rtos_tcb_t * const pCur = rtos_pCurrent;

        pCur->pXfer = pItem;
        pMb->readers |= pCur->bit;
        pCur->pWaiters = &pMb->readers;
        ok = !rtos_kernel_blockCurrent(timeout);
    }
    rtos_port_exitCritical(cs);
    return ok;
}


bool rtos_mboxIsFull(const rtos_mbox_t *pMb)
{
    return pMb->full != 0;
}
