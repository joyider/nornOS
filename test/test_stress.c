/**
 * @file test_stress.c
 * Randomized stress test of the kernel on the host port. Six tasks pick random operations
 * on shared semaphore, mutex, queue, mailbox, pool, events, notifications and software
 * timers with random timeouts; the idle hook advances time and injects random interrupts
 * that use the FromIsr variants. After every operation and every interrupt the kernel's
 * invariants are checked:
 *
 *   - the idle bit and the running task's bit are set in the ready mask
 *   - a task is in the timed list exactly when its flag says so; the list is sorted
 *   - a task that is not ready is in the timed list, registered as a waiter, or waiting
 *     forever for events or a notification; a ready task is none of these
 *   - no waiter mask of any object contains a ready task
 *   - the mutex owner, if any, runs at the ceiling; everybody else at their own priority
 *   - queue fill never exceeds the capacity; pool free count matches the free list and
 *     the blocks held by tasks and the ISR
 *   - the run times of all tasks add up to the elapsed time
 *
 * Data integrity: queue items carry producer and sequence number and must arrive in
 * order per producer; pool blocks carry an owner stamp that must be intact when freed.
 * Liveness: every task must have completed operations when the run ends.
 *
 * The random sequence is fixed (SEED), so a failure is reproducible; set SEED or TICKS
 * on the command line for other runs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtos.h"
#include "rtos_kernel.h"

#ifndef SEED
# define SEED   0x1234567ul
#endif
#ifndef TICKS
# define TICKS  30000ul
#endif

#define NUM_TASKS       RTOS_CFG_NUM_TASKS
#define POOL_BLOCKS     5
#define BLOCK_SIZE      16
#define QUEUE_CAP       4
#define HELD_MAX        2
#define PRODUCER_ISR    0xF

typedef struct item { uint8_t producer; uint8_t pad; uint16_t seq; } item_t;

static uint8_t _stacks[NUM_TASKS + 1][1];

static rtos_sem_t   _sem   = RTOS_SEM_INIT(1);
static rtos_mutex_t _mutex = RTOS_MUTEX_INIT(NUM_TASKS);
static uint8_t      _qStorage[QUEUE_CAP * sizeof(item_t)];
static rtos_queue_t _queue = RTOS_QUEUE_INIT(_qStorage, sizeof(item_t), QUEUE_CAP);
static uint8_t      _mbStorage[sizeof(item_t)];
static rtos_mbox_t  _mbox  = RTOS_MBOX_INIT(_mbStorage, sizeof(item_t));
static uint8_t      _poolStorage[POOL_BLOCKS * BLOCK_SIZE];
static rtos_pool_t  _pool  = RTOS_POOL_INIT(_poolStorage, BLOCK_SIZE, POOL_BLOCKS);
static rtos_timer_t _timers[NUM_TASKS + 1];

static uint32_t _rng = SEED;
static uint32_t _ops[NUM_TASKS + 1];
static uint32_t _checks;
static uint32_t _isrs;
static uint32_t _failures;
static uint16_t _seqNext[16];           /* per producer, next sequence to send */
static uint16_t _seqSeen[NUM_TASKS + 1][16]; /* per consumer and producer, last sequence */
static void    *_held[NUM_TASKS + 1][HELD_MAX];
static void    *_heldIsr;
static uint8_t  _mutexHolder = RTOS_NO_TASK;
static uint32_t _opCount[16];


static uint32_t rnd(void)
{
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return _rng;
}

static void fail(const char *what, uint32_t a, uint32_t b)
{
    ++ _failures;
    printf("FAIL: %s (%lu, %lu) at tick %lu, task %u\n", what, (unsigned long)a,
           (unsigned long)b, (unsigned long)rtos_getTick(), rtos_getCurrentTask());
    if(_failures > 10)
    {
        printf("too many failures, giving up\n");
        exit(1);
    }
}

#define CHECK(cond, a, b)   do { if(!(cond)) fail(#cond, (uint32_t)(a), (uint32_t)(b)); } while(0)


/* ---- invariants ---------------------------------------------------------------------- */

static bool inTimedList(const rtos_tcb_t *pT)
{
    const rtos_tcb_t *p;
    for(p=rtos_kernel_getTimedHead(); p!=NULL; p=p->pNextTimed)
        if(p == pT)
            return true;
    return false;
}

static uint8_t freeListLength(void)
{
    uint8_t n = 0;
    void *p = _pool.pFree;
    while(p != NULL  &&  n <= POOL_BLOCKS)
    {
        p = *(void **)p;
        ++ n;
    }
    return n;
}

static void checkInvariants(void)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();
    const rtos_taskmask_t ready = rtos_kernel_getReadyMask();
    const rtos_tcb_t *p;
    uint8_t i, heldBlocks = 0;
    rtos_runtime_t runSum = 0;

    ++ _checks;
    CHECK(ready & 1u, ready, 0);
    CHECK(ready & rtos_pCurrent->bit, ready, rtos_pCurrent->prio);

    for(p=rtos_kernel_getTimedHead(); p!=NULL && p->pNextTimed!=NULL; p=p->pNextTimed)
        CHECK((rtos_tickdiff_t)(p->pNextTimed->wakeAt - p->wakeAt) >= 0, p->wakeAt, p->pNextTimed->wakeAt);

    for(i=1; i<=NUM_TASKS; ++i)
    {
        const rtos_tcb_t * const pT = &rtos_tcbAry[i];
        const bool isReady = (ready & pT->bit) != 0;
        const bool timed = inTimedList(pT);
        const bool waiter = pT->pWaiters != NULL;
        const bool forever = (pT->flags & RTOS_TF_WAIT_NOTIFY) != 0  ||  pT->evtWait != 0;

        CHECK(timed == ((pT->flags & RTOS_TF_IN_TIMED_LIST) != 0), i, pT->flags);
        if(isReady)
            CHECK(!timed && !waiter, i, pT->flags);
        else
            CHECK(timed || waiter || forever, i, pT->flags);
        if(waiter)
            CHECK((*pT->pWaiters & pT->bit) != 0, i, *pT->pWaiters);
        if(_mutex.owner == i)
            CHECK(pT->effPrio == _mutex.ceiling, pT->effPrio, _mutex.ceiling);
        else
            CHECK(pT->effPrio == pT->prio, pT->effPrio, pT->prio);
    }
    CHECK((_sem.waiters & ready) == 0, _sem.waiters, ready);
    CHECK((_mutex.waiters & ready) == 0, _mutex.waiters, ready);
    CHECK((_queue.readers & ready) == 0, _queue.readers, ready);
    CHECK((_queue.writers & ready) == 0, _queue.writers, ready);
    CHECK((_mbox.readers & ready) == 0, _mbox.readers, ready);
    CHECK((_pool.waiters & ready) == 0, _pool.waiters, ready);
    /* The test's own holder variable is set after the lock returns; between a hand-over
       and that moment the kernel knows the owner and the test does not yet. */
    CHECK(_mutex.owner == RTOS_NO_TASK || _mutexHolder == RTOS_NO_TASK
          || _mutex.owner == _mutexHolder, _mutex.owner, _mutexHolder);
    CHECK(_queue.count <= QUEUE_CAP, _queue.count, QUEUE_CAP);

    for(i=1; i<=NUM_TASKS; ++i)
    {
        uint8_t k;
        for(k=0; k<HELD_MAX; ++k)
            if(_held[i][k] != NULL)
                ++ heldBlocks;
    }
    if(_heldIsr != NULL)
        ++ heldBlocks;
    CHECK(_pool.numFree == freeListLength(), _pool.numFree, freeListLength());
    CHECK(_pool.numFree + heldBlocks <= POOL_BLOCKS, _pool.numFree, heldBlocks);

    for(i=0; i<=NUM_TASKS; ++i)
        runSum += rtos_getTaskRunTime(i);
    CHECK(runSum == rtos_tickCount, (uint32_t)runSum, rtos_tickCount);

    rtos_port_exitCritical(cs);
}


/* ---- helpers ------------------------------------------------------------------------- */

static rtos_tick_t randomTimeout(void)
{
    const uint32_t r = rnd() % 8u;
    if(r == 7)
        return RTOS_FOREVER;
    return (rtos_tick_t)r;             /* 0 = RTOS_NO_WAIT */
}

static uint8_t randomTask(void)
{
    return (uint8_t)(1u + rnd() % NUM_TASKS);
}

static void stampBlock(void *pBlock, uint8_t owner)
{
    uint8_t *b = pBlock;
    uint8_t i;
    for(i=0; i<BLOCK_SIZE; ++i)
        b[i] = (uint8_t)(owner * 37u + i);
}

static void verifyBlock(const void *pBlock, uint8_t owner)
{
    const uint8_t *b = pBlock;
    uint8_t i;
    for(i=0; i<BLOCK_SIZE; ++i)
        if(b[i] != (uint8_t)(owner * 37u + i))
        {
            fail("pool block corrupted", owner, i);
            return;
        }
}

/* Order is guaranteed per producer and consumer, not globally: an item handed directly
   to a blocked consumer is counted when that consumer runs, which may be after a higher
   priority consumer took the next item out of the buffer. Per consumer the sequence of a
   producer must be strictly increasing: no duplicates, no reordering. */
static void receivedItem(uint8_t me, const item_t *pItem)
{
    const uint8_t prod = pItem->producer;
    CHECK(prod < 16, prod, 0);
    if(prod < 16)
    {
        CHECK((int16_t)(pItem->seq - _seqSeen[me][prod]) > 0, pItem->seq, _seqSeen[me][prod]);
        _seqSeen[me][prod] = pItem->seq;
    }
}

static void timerCallback(void *arg)
{
    const uint8_t task = (uint8_t)(uintptr_t)arg;
    rtos_postEventsFromIsr(task, 0x8);
}


/* ---- the tasks ----------------------------------------------------------------------- */

static void randomOp(uint8_t me)
{
    const uint32_t op = rnd() % 16u;
    ++ _opCount[op];
    switch(op)
    {
    case 0:
    case 1:
        rtos_delay((rtos_tick_t)(1u + rnd() % 4u));
        break;
    case 2:
    {
        const rtos_evt_t mask = (rtos_evt_t)(1u + rnd() % 15u);
        const rtos_evt_t got = rtos_waitEvents(mask, (rnd() & 1u) ? RTOS_WAIT_ALL : RTOS_WAIT_ANY,
                                               randomTimeout());
        CHECK((got & ~mask) == 0, got, mask);
        break;
    }
    case 3:
        rtos_postEvents(randomTask(), (rtos_evt_t)(1u + rnd() % 15u));
        break;
    case 4:
        if(rtos_semTake(&_sem, randomTimeout()))
        {
            if(rnd() & 1u)
                rtos_delay(1);
            rtos_semGive(&_sem);
        }
        break;
    case 5:
        rtos_mutexLock(&_mutex);
        CHECK(_mutexHolder == RTOS_NO_TASK, _mutexHolder, me);
        _mutexHolder = me;
        if((rnd() & 3u) == 0)
            rtos_delay(1);             /* blocks while holding: exercises the fallback */
        _mutexHolder = RTOS_NO_TASK;
        rtos_mutexUnlock(&_mutex);
        break;
    case 6:
    case 7:
    {
        item_t it;
        it.producer = me;
        it.pad = 0;
        it.seq = (uint16_t)(_seqNext[me] + 1u);
        if(rtos_queueSend(&_queue, &it, randomTimeout()))
            _seqNext[me] = it.seq;
        break;
    }
    case 8:
    case 9:
    {
        item_t it;
        if(rtos_queueReceive(&_queue, &it, randomTimeout()))
            receivedItem(me, &it);
        break;
    }
    case 10:
    {
        item_t it;
        it.producer = me;
        it.pad = 1;
        it.seq = (uint16_t)rnd();
        (void)rtos_mboxPost(&_mbox, &it);
        break;
    }
    case 11:
    {
        item_t it;
        if(rtos_mboxFetch(&_mbox, &it, randomTimeout()))
            CHECK(it.pad == 1 && it.producer >= 1 && it.producer <= NUM_TASKS, it.producer, it.pad);
        break;
    }
    case 12:
        rtos_notify(randomTask(), (uint16_t)(rnd() & 0xFFu),
                    (rnd() & 1u) ? RTOS_NOTIFY_OR : RTOS_NOTIFY_INC);
        break;
    case 13:
    {
        uint16_t v = 0;
        if(rnd() & 1u)
            (void)rtos_notifyWait(&v, randomTimeout());
        else
            (void)rtos_notifyTake(randomTimeout());
        break;
    }
    case 14:
    {
        uint8_t k;
        for(k=0; k<HELD_MAX; ++k)
            if(_held[me][k] == NULL)
                break;
        if(k < HELD_MAX)
        {
            void *b = rtos_poolAlloc(&_pool, randomTimeout());
            if(b != NULL)
            {
                stampBlock(b, me);
                _held[me][k] = b;
            }
        }
        else
        {
            /* Drop the reference before the free: the free may hand the block to a
               waiting task and switch to it, and that task records it as held. */
            void *b;
            k = (uint8_t)(rnd() % HELD_MAX);
            b = _held[me][k];
            _held[me][k] = NULL;
            verifyBlock(b, me);
            rtos_poolFree(&_pool, b);
        }
        break;
    }
    default:
        if(rtos_timerIsActive(&_timers[me]))
            rtos_timerStop(&_timers[me]);
        else
            rtos_timerStart(&_timers[me], (rtos_tick_t)(1u + rnd() % 5u),
                            (rnd() & 1u) ? (rtos_tick_t)(2u + rnd() % 3u) : 0u);
        break;
    }
}

static void task(void *arg)
{
    const uint8_t me = (uint8_t)(uintptr_t)arg;
    for(;;)
    {
        randomOp(me);
        ++ _ops[me];
        checkInvariants();
    }
}


/* ---- interrupts, injected from the idle hook ------------------------------------------ */

static void isrEvents(void)
{
    rtos_postEventsFromIsr(randomTask(), (rtos_evt_t)(1u + rnd() % 15u));
}

static void isrSem(void)
{
    if(_sem.count < 2)
        rtos_semGiveFromIsr(&_sem);
}

static void isrQueue(void)
{
    item_t it;
    it.producer = PRODUCER_ISR;
    it.pad = 0;
    it.seq = (uint16_t)(_seqNext[PRODUCER_ISR] + 1u);
    if(rtos_queueSendFromIsr(&_queue, &it))
        _seqNext[PRODUCER_ISR] = it.seq;
}

static void isrNotify(void)
{
    rtos_notifyFromIsr(randomTask(), 1, RTOS_NOTIFY_INC);
}

static void isrPool(void)
{
    if(_heldIsr == NULL)
    {
        _heldIsr = rtos_poolAllocFromIsr(&_pool);
        if(_heldIsr != NULL)
            stampBlock(_heldIsr, PRODUCER_ISR);
    }
    else
    {
        void * const b = _heldIsr;
        _heldIsr = NULL;
        verifyBlock(b, PRODUCER_ISR);
        rtos_poolFreeFromIsr(&_pool, b);
    }
}

static void isrMbox(void)
{
    item_t it;
    it.producer = (uint8_t)randomTask();
    it.pad = 1;
    it.seq = 0;
    (void)rtos_mboxPostFromIsr(&_mbox, &it);
}

static void (* const _isrs_[])(void) = { isrEvents, isrSem, isrQueue, isrNotify, isrPool, isrMbox };

static void finish(void)
{
    uint8_t i;
    bool live = true;

    printf("ticks=%lu checks=%lu isrs=%lu ops:", (unsigned long)rtos_getTick(),
           (unsigned long)_checks, (unsigned long)_isrs);
    for(i=1; i<=NUM_TASKS; ++i)
    {
        printf(" %lu", (unsigned long)_ops[i]);
        if(_ops[i] == 0)
            live = false;
    }
    printf("\nop histogram:");
    for(i=0; i<16; ++i)
        printf(" %lu", (unsigned long)_opCount[i]);
    printf("\nqueue items sent: task");
    for(i=1; i<=NUM_TASKS; ++i)
        printf(" %u", _seqNext[i]);
    printf(" isr %u\n", _seqNext[PRODUCER_ISR]);
    if(!live)
        fail("a task never ran", 0, 0);
    printf("%s: test_stress (%lu failures)\n", _failures == 0 ? "PASS" : "FAIL",
           (unsigned long)_failures);
    exit(_failures == 0 ? 0 : 1);
}

void test_idleHook(void)
{
    if(rtos_getTick() >= TICKS)
        finish();
    rtos_host_tick();
    checkInvariants();
    if((rnd() & 3u) == 0)
    {
        ++ _isrs;
        rtos_host_simulateIsr(_isrs_[rnd() % (sizeof(_isrs_) / sizeof(_isrs_[0]))]);
        checkInvariants();
    }
}


int main(void)
{
    uint8_t i;

    rtos_init();
    rtos_poolInit(&_pool, _poolStorage, BLOCK_SIZE, POOL_BLOCKS);
    for(i=1; i<=NUM_TASKS; ++i)
    {
        rtos_timerInit(&_timers[i], timerCallback, (void *)(uintptr_t)i);
        rtos_createTask(i, task, (void *)(uintptr_t)i, _stacks[i], sizeof(_stacks[i]));
    }
    rtos_start();
    return 0;
}
