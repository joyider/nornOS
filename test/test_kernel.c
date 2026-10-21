/**
 * @file test_kernel.c
 * Unit test of the kernel on the host. Three tasks run a scripted scenario and append to
 * a trace; the idle hook drives simulated time and finally compares the trace with the
 * expected sequence.
 *
 * Covered: start order, semaphore hand-over and timeout, delay accuracy, event ANY/ALL,
 * ceiling mutex (no preemption while held), mutex fallback hand-over when the ceiling is
 * too low, drift free periodic wait and overrun detection, task termination.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtos.h"

#define T1  1u
#define T2  2u
#define T3  3u

#define EVT_GO  0x0001u
#define EVT_A   0x0002u
#define EVT_B   0x0004u

typedef struct { uint8_t task; const char *tag; int32_t val; } trace_t;

static trace_t _trace[64];
static uint8_t _nTrace = 0;
static volatile int _done = 0;
static uint32_t _idleLoops = 0;

static uint8_t _stack1[1], _stack2[1], _stack3[1];
static rtos_sem_t _sem = RTOS_SEM_INIT(0);
static rtos_mutex_t _m1 = RTOS_MUTEX_INIT(T3);     /* Correct ceiling. */
static rtos_mutex_t _m2 = RTOS_MUTEX_INIT(T2);     /* Owner will block while holding it. */

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

static void isrPostGoToT3(void) { rtos_postEventsFromIsr(T3, EVT_GO); }

static void task3(void *arg)
{
    (void)arg;
    for(;;)
    {
        (void)rtos_waitEvents(EVT_GO, RTOS_WAIT_ANY, RTOS_FOREVER);
        trace(T3, "go", (int32_t)rtos_getTick());
        (void)rtos_waitEvents(EVT_A | EVT_B, RTOS_WAIT_ALL, RTOS_FOREVER);
        trace(T3, "ab", (int32_t)rtos_getTick());
    }
}

static void task2(void *arg)
{
    rtos_tick_t t0;
    uint8_t i;
    (void)arg;

    (void)rtos_semTake(&_sem, RTOS_FOREVER);
    trace(T2, "sem", (int32_t)rtos_getTick());
    trace(T2, "semTimeout", rtos_semTake(&_sem, 3) ? 1 : 0);

    (void)rtos_waitEvents(EVT_GO, RTOS_WAIT_ANY, RTOS_FOREVER);
    trace(T2, "go", (int32_t)rtos_getTick());
    rtos_mutexLock(&_m2);
    trace(T2, "m2locked", 0);
    rtos_mutexUnlock(&_m2);

    t0 = rtos_getTick();
    rtos_delay(5);
    trace(T2, "delay5", (int32_t)(rtos_getTick() - t0));

    trace(T2, "periodic100", rtos_waitPeriodic(100) ? 1 : 0);
    trace(T2, "tick", (int32_t)rtos_getTick());

    /* Burn 5 ticks of "execution time", then a 2 tick period must report an overrun. */
    for(i=0; i<5; ++i)
        rtos_host_tick();
    trace(T2, "overrun", rtos_waitPeriodic(2) ? 1 : 0);
    trace(T2, "periodic2", rtos_waitPeriodic(2) ? 1 : 0);
    trace(T2, "tick", (int32_t)rtos_getTick());

    /* Return: the task terminates. */
}

static void task1(void *arg)
{
    rtos_tick_t t0;
    (void)arg;

    trace(T1, "start", (int32_t)rtos_getTick());

    /* Semaphore hand-over to a higher task preempts immediately. */
    rtos_semGive(&_sem);
    trace(T1, "afterGive", 0);

    t0 = rtos_getTick();
    rtos_delay(10);
    trace(T1, "delay10", (int32_t)(rtos_getTick() - t0));

    /* Ceiling mutex: T3 becomes ready in an ISR but must not run until unlock. */
    rtos_mutexLock(&_m1);
    trace(T1, "m1locked", 0);
    rtos_host_simulateIsr(isrPostGoToT3);
    trace(T1, "stillRunning", 0);
    rtos_mutexUnlock(&_m1);
    trace(T1, "m1unlocked", 0);

    /* Wait for all: A alone does not wake T3. */
    rtos_postEvents(T3, EVT_A);
    trace(T1, "postedA", 0);
    rtos_postEvents(T3, EVT_B);
    trace(T1, "postedB", 0);

    /* Owner blocks while holding a mutex: T2 becomes ready but cannot preempt (ceiling),
       runs when T1 delays, blocks on the mutex and gets it handed over at unlock. */
    rtos_mutexLock(&_m2);
    rtos_postEvents(T2, EVT_GO);
    trace(T1, "m2heldWhileT2Ready", 0);
    rtos_delay(1);
    trace(T1, "m2wake", (int32_t)rtos_getTick());
    rtos_mutexUnlock(&_m2);
    trace(T1, "m2unlocked", 0);

    rtos_delay(200);
    trace(T1, "end", (int32_t)rtos_getTick());
    _done = 1;
    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static const trace_t _expected[] =
{
    { T1, "start", 0 },
    { T2, "sem", 0 },
    { T1, "afterGive", 0 },
    { T2, "semTimeout", 0 },
    { T1, "delay10", 11 },
    { T1, "m1locked", 0 },
    { T1, "stillRunning", 0 },
    { T3, "go", 11 },
    { T1, "m1unlocked", 0 },
    { T1, "postedA", 0 },
    { T3, "ab", 11 },
    { T1, "postedB", 0 },
    { T1, "m2heldWhileT2Ready", 0 },
    { T2, "go", 11 },
    { T1, "m2wake", 13 },
    { T2, "m2locked", 0 },
    { T1, "m2unlocked", 0 },
    { T2, "delay5", 6 },
    { T2, "periodic100", 0 },
    { T2, "tick", 100 },
    { T2, "overrun", 1 },
    { T2, "periodic2", 0 },
    { T2, "tick", 107 },
    { T1, "end", 214 },
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

        printf("%2u %s  got: T%u %-20s %4ld   expected: T%u %-20s %4ld\n"
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

int main(void)
{
    rtos_init();
    rtos_createTask(T1, task1, NULL, _stack1, sizeof(_stack1));
    rtos_createTask(T2, task2, NULL, _stack2, sizeof(_stack2));
    rtos_createTask(T3, task3, NULL, _stack3, sizeof(_stack3));
    rtos_start();
    return 0;
}

void rtos_assertFailed(const char *file, uint16_t line)
{
    printf("ASSERTION FAILED: %s line %u (current task %u, trace entries %u)\n"
           , file, line, rtos_getCurrentTask(), _nTrace);
    exit(2);
}
