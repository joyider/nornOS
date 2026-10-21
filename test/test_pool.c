/**
 * @file test_pool.c
 * Host unit test of the memory pool and the run time statistics.
 *
 * Covered: allocating all blocks, failing non-blocking allocation, blocking allocation
 * with timeout, hand-over of a released block to the highest waiter, LIFO reuse of a
 * released block, free count, per task run time measured at context switches and CPU
 * load computed from the idle task's run time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtos.h"

#define T1  1u
#define T2  2u
#define T3  3u

#define BLOCK_SIZE  16u
#define NUM_BLOCKS  3u

typedef struct { uint8_t task; const char *tag; int32_t val; } trace_t;

static trace_t _trace[64];
static uint8_t _nTrace = 0;
static volatile int _done = 0;
static uint32_t _idleLoops = 0;
static uint8_t _stack1[1], _stack2[1], _stack3[1];

static uint8_t _poolStorage[BLOCK_SIZE * NUM_BLOCKS];
static rtos_pool_t _pool = RTOS_POOL_INIT(_poolStorage, BLOCK_SIZE, NUM_BLOCKS);
static void *_blockA = NULL;

static void trace(uint8_t task, const char *tag, int32_t val)
{
    if(_nTrace < sizeof(_trace)/sizeof(_trace[0]))
    {
        _trace[_nTrace].task = task;
        _trace[_nTrace].tag = tag;
        _trace[_nTrace].val = val;
        ++ _nTrace;
    }
    if(rtos_getCurrentTask() != task)
    {
        printf("FAIL: trace from task %u but current task is %u\n", task, rtos_getCurrentTask());
        exit(1);
    }
}

static void task1(void *arg)
{
    void *p;
    uint8_t i;
    (void)arg;

    p = rtos_poolAlloc(&_pool, RTOS_FOREVER);
    trace(T1, "alloc", p == _blockA ? 1 : 0);
    rtos_poolFree(&_pool, p);
    p = rtos_poolAlloc(&_pool, RTOS_NO_WAIT);
    trace(T1, "alloc2", p == _blockA ? 1 : 0);

    /* Consume CPU time: two ticks pass while this task runs, then T3 preempts. */
    for(i=0; i<5; ++i)
        rtos_host_tick();

    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static void task2(void *arg)
{
    (void)arg;
    if(rtos_poolAlloc(&_pool, 2) == NULL)
        trace(T2, "allocTimeout", (int32_t)rtos_getTick());
    else
        trace(T2, "allocUnexpected", 0);
    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static void task3(void *arg)
{
    void *pB, *pC;
    (void)arg;

    _blockA = rtos_poolAlloc(&_pool, RTOS_NO_WAIT);
    pB = rtos_poolAlloc(&_pool, RTOS_NO_WAIT);
    pC = rtos_poolAlloc(&_pool, RTOS_NO_WAIT);
    trace(T3, "alloc3", (_blockA && pB && pC) ? rtos_poolFreeCount(&_pool) : -1);
    trace(T3, "allocFull", rtos_poolAlloc(&_pool, RTOS_NO_WAIT) == NULL ? 0 : 1);
    rtos_delay(1);

    /* Tick 2: T2 has timed out (but not run yet), T1 waits. A goes to T1, B to the free
       list. */
    rtos_poolFree(&_pool, _blockA);
    trace(T3, "freed", 0);
    rtos_poolFree(&_pool, pB);
    trace(T3, "freeCount", rtos_poolFreeCount(&_pool));
    rtos_delay(1);

    trace(T3, "wake", (int32_t)rtos_getTick());
    trace(T3, "runT1", (int32_t)rtos_getTaskRunTime(T1));
    trace(T3, "runIdle", (int32_t)rtos_getTaskRunTime(RTOS_IDLE_TASK));
    trace(T3, "load", rtos_getCpuLoad());
    rtos_poolFree(&_pool, pC);
    trace(T3, "end", (int32_t)rtos_getTick());
    _done = 1;
    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static const trace_t _expected[] =
{
    { T3, "alloc3", 0 },
    { T3, "allocFull", 0 },
    { T3, "freed", 0 },
    { T3, "freeCount", 1 },
    { T2, "allocTimeout", 2 },
    { T1, "alloc", 1 },
    { T1, "alloc2", 1 },
    { T3, "wake", 4 },
    { T3, "runT1", 2 },
    { T3, "runIdle", 2 },
    { T3, "load", 50 },
    { T3, "end", 4 },
};

static void verify(void)
{
    const uint8_t nExp = sizeof(_expected)/sizeof(_expected[0]);
    uint8_t i, fails = 0;

    for(i=0; i<_nTrace || i<nExp; ++i)
    {
        const trace_t *pGot = i < _nTrace ? &_trace[i] : NULL;
        const trace_t *pExp = i < nExp ? &_expected[i] : NULL;
        const int ok = pGot != NULL  &&  pExp != NULL
                       &&  pGot->task == pExp->task
                       &&  strcmp(pGot->tag, pExp->tag) == 0
                       &&  pGot->val == pExp->val;

        printf("%2u %s  got: T%u %-14s %4ld   expected: T%u %-14s %4ld\n"
               , i, ok ? "ok  " : "FAIL"
               , pGot ? pGot->task : 0, pGot ? pGot->tag : "-", pGot ? (long)pGot->val : 0L
               , pExp ? pExp->task : 0, pExp ? pExp->tag : "-", pExp ? (long)pExp->val : 0L
              );
        if(!ok)
            ++ fails;
    }
    printf("%s: %u entries, %u failures, %lu idle loops\n"
           , fails == 0 ? "PASS" : "FAIL", _nTrace, fails, (unsigned long)_idleLoops);
    exit(fails == 0 ? 0 : 1);
}

void test_idleHook(void)
{
    if(_done)
        verify();
    if(++_idleLoops > 100000ul)
    {
        printf("FAIL: runaway, test did not finish\n");
        verify();
    }
    rtos_host_tick();
}

void rtos_assertFailed(const char *file, uint16_t line)
{
    printf("ASSERTION FAILED: %s line %u (current task %u, trace entries %u)\n"
           , file, line, rtos_getCurrentTask(), _nTrace);
    exit(2);
}

int main(void)
{
    rtos_init();
    rtos_poolInit(&_pool, _poolStorage, BLOCK_SIZE, NUM_BLOCKS);
    rtos_createTask(T1, task1, NULL, _stack1, sizeof(_stack1));
    rtos_createTask(T2, task2, NULL, _stack2, sizeof(_stack2));
    rtos_createTask(T3, task3, NULL, _stack3, sizeof(_stack3));
    rtos_start();
    return 0;
}
