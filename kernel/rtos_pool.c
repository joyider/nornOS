/**
 * @file rtos_pool.c
 * Fixed size block memory pool. The free blocks form a singly linked list threaded
 * through the blocks themselves, so allocation and release are O(1) and the pool needs no
 * bookkeeping memory beyond the blocks. A blocking allocation hands the released block
 * straight to the highest priority waiter (see rtos_queue.c for the rationale).
 */

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"


/*
 * Local functions
 */

/** Take a block from the free list. Interrupts disabled. NULL if empty. */
static inline void *take(rtos_pool_t *pPool)
{
    void * const pBlock = pPool->pFree;

    if(pBlock != NULL)
    {
        pPool->pFree = *(void **)pBlock;
        -- pPool->numFree;
    }
    return pBlock;
}


/*
 * Public API
 */

void rtos_poolInit(rtos_pool_t *pPool, uint8_t *storage, uint16_t blockSize, uint8_t numBlocks)
{
    uint8_t i;
    uint8_t *p = storage;

    RTOS_ASSERT(storage != NULL  &&  blockSize >= sizeof(void *)  &&  numBlocks > 0);
    pPool->storage = storage;
    pPool->blockSize = blockSize;
    pPool->numBlocks = numBlocks;
    pPool->numFree = numBlocks;
    pPool->waiters = 0;
    pPool->pFree = storage;
    for(i=1; i<numBlocks; ++i)
    {
        *(void **)p = p + blockSize;
        p += blockSize;
    }
    *(void **)p = NULL;
}


void *rtos_poolAlloc(rtos_pool_t *pPool, rtos_tick_t timeout)
{
    void *pBlock;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    pBlock = take(pPool);
    if(pBlock == NULL  &&  timeout != RTOS_NO_WAIT)
    {
        rtos_tcb_t * const pCur = rtos_pCurrent;

        /* The releasing task writes the block address into pBlock through pXfer. */
        pCur->pXfer = &pBlock;
        pPool->waiters |= pCur->bit;
        pCur->pWaiters = &pPool->waiters;
        if(rtos_kernel_blockCurrent(timeout))
            pBlock = NULL;
    }
    rtos_port_exitCritical(cs);
    return pBlock;
}


void *rtos_poolAllocFromIsr(rtos_pool_t *pPool)
{
    return take(pPool);
}


void rtos_poolFreeFromIsr(rtos_pool_t *pPool, void *pBlock)
{
    RTOS_ASSERT((uint8_t *)pBlock >= pPool->storage
                &&  (uint8_t *)pBlock < pPool->storage + (uint16_t)pPool->blockSize * pPool->numBlocks
                &&  ((uint16_t)((uint8_t *)pBlock - pPool->storage)) % pPool->blockSize == 0
               );
    if(pPool->waiters != 0)
    {
        const uint8_t id = rtos_kernel_releaseHighestWaiter(&pPool->waiters);
        *(void **)rtos_tcbAry[id].pXfer = pBlock;
    }
    else
    {
        RTOS_ASSERT(pPool->numFree < pPool->numBlocks);
        *(void **)pBlock = pPool->pFree;
        pPool->pFree = pBlock;
        ++ pPool->numFree;
    }
}


void rtos_poolFree(rtos_pool_t *pPool, void *pBlock)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    rtos_poolFreeFromIsr(pPool, pBlock);
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}


uint8_t rtos_poolFreeCount(const rtos_pool_t *pPool)
{
    return pPool->numFree;
}
