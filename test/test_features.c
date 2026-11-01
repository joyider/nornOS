/**
 * @file test_features.c
 * Heap, event groups, suspend/resume and the tickless query on the host port. Trace
 * based like test_kernel: tasks record (task, tag, value) and the sequence is compared
 * with the expected one at the end.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtos.h"
#include "rtos_kernel.h"

#define T1 1
#define T2 2
#define T3 3

static uint8_t _stack1[1], _stack2[1], _stack3[1];
static rtos_evgroup_t _group = RTOS_EVGROUP_INIT(0);
static uint8_t _heapArea[1024];
static uint32_t _idleLoops;
static bool _done;

typedef struct entry { uint8_t task; const char *tag; long value; } entry_t;
static entry_t _trace[64];
static uint8_t _n;

static void trace(uint8_t task, const char *tag, long value)
{
    if(_n < 64)
    {
        _trace[_n].task = task; _trace[_n].tag = tag; _trace[_n].value = value;
        ++ _n;
    }
}

static const entry_t _expected[] =
{
    /* --- heap, in T3 before anything else --- */
    { T3, "heapFreeAfterInit", 1 },     /* free == total */
    { T3, "alloc3", 1 },                /* three blocks, distinct, aligned */
    { T3, "freeMiddle", 1 },
    { T3, "coalesce", 1 },              /* free all: one block again */
    { T3, "tooBig", 1 },                /* NULL and failed == 1 */
    { T3, "stats", 1 },
    /* --- event group: T2 waits ALL 0x3 with clear, T1 waits ANY 0x1, T3 sets. T3 is
       the highest priority, so it runs on until it blocks; then T2 before T1. --- */
    { T3, "bitsAfterSet1", 0x1 },       /* T1 got 0x1 handed over, did not clear it */
    { T3, "bitsAfterSet2", 0x0 },       /* set 0x2: T2 took 0x3 and cleared it */
    { T2, "evALL", 0x3 },
    { T1, "evANY", 0x1 },
    { T1, "evTimeout", 0 },
    /* --- suspend/resume --- */
    { T3, "infoSuspended", RTOS_TASK_SUSPENDED },
    { T1, "resumedAfterEvent", 1 },     /* the event arrived while suspended, taken at resume */
    { T3, "selfSuspend", 1 },
    { T3, "tickless", 1 },              /* a timer 5 ticks out bounds the next wake */
    { T3, "end", 0 },
};

static void verify(void)
{
    uint8_t i, failures = 0;
    const uint8_t expected = (uint8_t)(sizeof(_expected) / sizeof(_expected[0]));
    for(i=0; i<_n || i<expected; ++i)
    {
        const bool ok = i < _n  &&  i < expected
                        &&  _trace[i].task == _expected[i].task
                        &&  strcmp(_trace[i].tag, _expected[i].tag) == 0
                        &&  _trace[i].value == _expected[i].value;
        if(!ok)
        {
            ++ failures;
            printf("  %2u: got %s, expected %s\n", i,
                   i < _n ? _trace[i].tag : "(nothing)", i < expected ? _expected[i].tag : "(nothing)");
            if(i < _n) printf("      got task %u value %ld\n", _trace[i].task, _trace[i].value);
        }
    }
    printf("%s: %u entries, %u failures, %lu idle loops\n", failures == 0 ? "PASS" : "FAIL",
           _n, failures, (unsigned long)_idleLoops);
    exit(failures == 0 ? 0 : 1);
}

void test_idleHook(void)
{
    if(_done) verify();
    if(++_idleLoops > 100000ul) { printf("FAIL: runaway\n"); verify(); }
    rtos_host_tick();
}

/* ---- tasks --------------------------------------------------------------------------- */

static void task1(void *arg)
{
    rtos_evt_t got;
    (void)arg;
    got = rtos_evgroupWait(&_group, 0x1, RTOS_WAIT_ANY, RTOS_FOREVER);
    trace(T1, "evANY", got);
    got = rtos_evgroupWait(&_group, 0x8, RTOS_WAIT_ANY, 2);
    trace(T1, "evTimeout", got);
    /* T3 suspends us while we wait here, then posts the event, then resumes us. */
    got = rtos_waitEvents(0x4, RTOS_WAIT_ANY, RTOS_FOREVER);
    trace(T1, "resumedAfterEvent", got == 0x4);
    rtos_delay(3);
    for(;;)
        rtos_delay(100);
}

static void task2(void *arg)
{
    rtos_evt_t got;
    (void)arg;
    got = rtos_evgroupWait(&_group, 0x3, RTOS_WAIT_ALL | RTOS_WAIT_CLEAR, RTOS_FOREVER);
    trace(T2, "evALL", got);
    for(;;)
        rtos_delay(100);
}

void resumeCb(void *arg);

static void task3(void *arg)
{
    rtos_heapStats_t st;
    rtos_taskInfo_t info;
    void *a, *b, *c;
    (void)arg;

    /* heap */
    rtos_heapInit(_heapArea, sizeof(_heapArea));
    rtos_heapStats(&st);
    trace(T3, "heapFreeAfterInit", st.free == st.total && st.largestFree == st.total);
    a = rtos_malloc(100); b = rtos_malloc(200); c = rtos_malloc(50);
    trace(T3, "alloc3", a && b && c && a != b && b != c
                        && ((uintptr_t)a % (2 * sizeof(void *))) == 0
                        && ((uintptr_t)b % (2 * sizeof(void *))) == 0);
    memset(a, 0xA1, 100); memset(b, 0xB2, 200); memset(c, 0xC3, 50);
    rtos_free(b);
    rtos_heapStats(&st);
    trace(T3, "freeMiddle", ((uint8_t *)a)[99] == 0xA1 && ((uint8_t *)c)[49] == 0xC3 && st.frees == 1);
    rtos_free(a); rtos_free(c);
    rtos_heapStats(&st);
    trace(T3, "coalesce", st.free == st.total && st.largestFree == st.total);
    a = rtos_malloc(5000);
    rtos_heapStats(&st);
    trace(T3, "tooBig", a == NULL && st.failed == 1);
    trace(T3, "stats", st.allocs == 3 && st.frees == 3 && st.minFree < st.total);

    /* event group */
    rtos_delay(1);                      /* let T1 and T2 block in their waits */
    rtos_evgroupSet(&_group, 0x1);      /* T1 wakes (ANY), T2 needs 0x3 */
    trace(T3, "bitsAfterSet1", rtos_evgroupGet(&_group));
    rtos_evgroupSet(&_group, 0x2);      /* T2 wakes with 0x3 and clears it */
    trace(T3, "bitsAfterSet2", rtos_evgroupGet(&_group));
    rtos_delay(3);                      /* T1's second wait times out (2 ticks) */

    /* suspend/resume: T1 is blocked in rtos_waitEvents(0x4) now */
    rtos_suspend(T1);
    rtos_getTaskInfo(T1, &info);
    trace(T3, "infoSuspended", info.state);
    rtos_postEvents(T1, 0x4);           /* consumed, T1 becomes ready-pending */
    rtos_delay(1);                      /* T1 must not run here */
    rtos_resume(T1);                    /* T1 (lower prio) runs when we block */
    rtos_delay(1);

    /* suspend self, resumed by a timer callback through the ISR variant */
    {
        static rtos_timer_t tmr;
        rtos_timerInit(&tmr, resumeCb, NULL);
        rtos_timerStart(&tmr, 2, 0);
        rtos_suspend(T3);
        trace(T3, "selfSuspend", 1);
    }

    /* tickless query: the nearest of the timed waits and a timer 5 ticks out */
    {
        static rtos_timer_t tmr2;
        rtos_tick_t n;
        rtos_port_cs_t cs;
        rtos_timerInit(&tmr2, resumeCb, NULL);
        rtos_timerStart(&tmr2, 5, 0);
        cs = rtos_port_enterCritical();
        n = rtos_kernel_ticksToNextWake();
        rtos_port_exitCritical(cs);
        trace(T3, "tickless", n >= 1 && n <= 5);
    }
    trace(T3, "end", 0);
    _done = true;
    for(;;)
        rtos_delay(100);
}

void resumeCb(void *arg)
{
    (void)arg;
    rtos_resumeFromIsr(T3);
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
