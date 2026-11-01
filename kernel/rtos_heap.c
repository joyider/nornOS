/**
 * @file rtos_heap.c
 * A small allocator for systems that need one: first fit over a doubly linked free list
 * with boundary tags, so that a freed block merges with free neighbours on both sides.
 * Deterministic in the sense that nothing happens behind the caller's back; allocation
 * time is linear in the number of free blocks. Every call runs in a critical section.
 *
 * Layout of a block: [size | used, padded to HEAP_ALIGN][ ... payload ... ][size] where
 * the trailing size is the boundary tag that lets the next block find this one's start.
 * Free blocks keep their list links in the payload. Sizes are multiples of HEAP_ALIGN, so
 * payloads are HEAP_ALIGN aligned; the used bit is bit 0 of the leading size word.
 */

#include "rtos.h"
#include "rtos_assert.h"

#if RTOS_CFG_HEAP

#define HEAP_ALIGN      (2u * sizeof(void *))
#define ALIGN_UP(n)     (((n) + HEAP_ALIGN - 1u) & ~(HEAP_ALIGN - 1u))
#define USED            1u
#define HDR             HEAP_ALIGN              /* leading size word, padded */
#define TAG             sizeof(size_t)          /* trailing size word */
#define OVERHEAD        (HDR + TAG)

/** Free list links, stored at the start of a free block's payload. */
typedef struct freeBlock
{
    struct freeBlock *pNext;
    struct freeBlock *pPrev;
} freeBlock_t;

#define LINKS(p)        ((freeBlock_t *)((uint8_t *)(p) + HDR))
#define BLOCK(pLinks)   ((uint8_t *)(pLinks) - HDR)
#define MIN_BLOCK       ALIGN_UP(HDR + sizeof(freeBlock_t) + TAG)

static uint8_t *_start, *_end;
static freeBlock_t *_pFree;     /**< Head of the free list. */
static rtos_heapStats_t _stats;

static inline size_t blockSize(const void *p)   { return *(const size_t *)p & ~(size_t)USED; }
static inline bool blockUsed(const void *p)     { return (*(const size_t *)p & USED) != 0; }
static inline void setSize(void *p, size_t size, bool used)
{
    *(size_t *)p = size | (used ? USED : 0u);
    *(size_t *)((uint8_t *)p + size - TAG) = size;
}

static void unlink(void *pBlock)
{
    freeBlock_t * const pL = LINKS(pBlock);
    if(pL->pPrev != NULL)
        pL->pPrev->pNext = pL->pNext;
    else
        _pFree = pL->pNext;
    if(pL->pNext != NULL)
        pL->pNext->pPrev = pL->pPrev;
}

static void link(void *pBlock)
{
    freeBlock_t * const pL = LINKS(pBlock);
    pL->pPrev = NULL;
    pL->pNext = _pFree;
    if(_pFree != NULL)
        _pFree->pPrev = pL;
    _pFree = pL;
}

void rtos_heapInit(void *area, size_t size)
{
    uint8_t *p = (uint8_t *)ALIGN_UP((uintptr_t)area);
    size_t n;

    RTOS_ASSERT(area != NULL  &&  size > 4u * MIN_BLOCK);
    n = (size - (size_t)(p - (uint8_t *)area)) & ~(HEAP_ALIGN - 1u);
    _start = p;
    _end = p + n;
    _pFree = NULL;
    setSize(p, n, false);
    link(p);
    _stats.total = n;
    _stats.free = n;
    _stats.minFree = n;
    _stats.largestFree = n;
    _stats.allocs = _stats.frees = _stats.failed = 0;
}

void *rtos_malloc(size_t size)
{
    const size_t need = ALIGN_UP(size + OVERHEAD) < MIN_BLOCK ? MIN_BLOCK : ALIGN_UP(size + OVERHEAD);
    freeBlock_t *pL;
    uint8_t *pB = NULL;
    void *result = NULL;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(_start != NULL);
    for(pL=_pFree; pL!=NULL; pL=pL->pNext)
    {
        if(blockSize(BLOCK(pL)) >= need)
        {
            pB = BLOCK(pL);
            break;
        }
    }
    if(pB != NULL)
    {
        const size_t have = blockSize(pB);
        unlink(pB);
        if(have - need >= MIN_BLOCK)
        {
            /* Split: the remainder stays free. */
            uint8_t * const pRest = pB + need;
            setSize(pRest, have - need, false);
            link(pRest);
            setSize(pB, need, true);
        }
        else
            setSize(pB, have, true);
        _stats.free -= blockSize(pB);
        if(_stats.free < _stats.minFree)
            _stats.minFree = _stats.free;
        if(_stats.allocs < 0xFFFFu)
            ++ _stats.allocs;
        result = pB + HDR;
    }
    else if(_stats.failed < 0xFFFFu)
        ++ _stats.failed;
    rtos_port_exitCritical(cs);
    return result;
}

void rtos_free(void *p)
{
    uint8_t *pB;
    size_t size;
    rtos_port_cs_t cs;

    if(p == NULL)
        return;
    pB = (uint8_t *)p - HDR;
    cs = rtos_port_enterCritical();
    RTOS_ASSERT(pB >= _start  &&  pB < _end  &&  blockUsed(pB));
    size = blockSize(pB);
    _stats.free += size;
    if(_stats.frees < 0xFFFFu)
        ++ _stats.frees;

    /* Merge with the next block if free. */
    if(pB + size < _end  &&  !blockUsed(pB + size))
    {
        unlink(pB + size);
        size += blockSize(pB + size);
    }
    /* Merge with the previous block if free: its tag sits right before us. */
    if(pB > _start)
    {
        const size_t prevSize = *(const size_t *)(pB - TAG);
        uint8_t * const pPrev = pB - prevSize;
        if(!blockUsed(pPrev))
        {
            unlink(pPrev);
            size += prevSize;
            pB = pPrev;
        }
    }
    setSize(pB, size, false);
    link(pB);
    rtos_port_exitCritical(cs);
}

void rtos_heapStats(rtos_heapStats_t *pStats)
{
    const freeBlock_t *pL;
    size_t largest = 0;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    for(pL=_pFree; pL!=NULL; pL=pL->pNext)
        if(blockSize(BLOCK(pL)) > largest)
            largest = blockSize(BLOCK(pL));
    _stats.largestFree = largest;
    *pStats = _stats;
    rtos_port_exitCritical(cs);
}

#endif  /* RTOS_CFG_HEAP */
