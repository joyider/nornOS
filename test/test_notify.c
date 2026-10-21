/**
 * @file test_notify.c
 * Host unit test of task notifications and software timers. Task 3 scripts the scenario,
 * task 2 consumes notifications, task 1 consumes events posted by timer callbacks.
 *
 * Covered: notify SET/OR/INC, notifyWait value and timeout, notifyTake counting and
 * timeout, a SET of 0 not waking a take, notify from a simulated ISR, one shot timer,
 * periodic timer without drift, restart from the callback, stop from the callback, stop
 * before expiry, rtos_timerRemaining, callbacks running with interrupts disabled.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtos.h"

#define T1  1u
#define T2  2u
#define T3  3u

#define EVT_A   0x0001u
#define EVT_P   0x0002u

typedef struct { uint8_t task; const char *tag; int32_t val; } trace_t;

static trace_t _trace[64];
static uint8_t _nTrace = 0;
static volatile int _done = 0;
static uint32_t _idleLoops = 0;
static uint8_t _stack1[1], _stack2[1], _stack3[1];

static rtos_timer_t _tmrA, _tmrP, _tmrS;
static uint8_t _cntA = 0, _cntP = 0, _cntS = 0;

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

static void checkIsrContext(const char *who)
{
    if(!rtos_host_interruptsDisabled())
    {
        printf("FAIL: timer callback %s ran with interrupts enabled\n", who);
        exit(1);
    }
}

static void cbA(void *arg)
{
    (void)arg;
    checkIsrContext("A");
    rtos_postEventsFromIsr(T1, EVT_A);
    if(++_cntA == 1)
        rtos_timerStart(&_tmrA, 5, 0);      /* Restart once from inside the callback. */
}

static void cbP(void *arg)
{
    (void)arg;
    checkIsrContext("P");
    rtos_postEventsFromIsr(T1, EVT_P);
    if(++_cntP == 3)
        rtos_timerStop(&_tmrP);             /* Stop from inside the callback. */
}

static void cbS(void *arg)
{
    (void)arg;
    ++ _cntS;                               /* Must never run. */
}

static void isrNotifyInc(void)
{
    rtos_notifyFromIsr(T2, 0, RTOS_NOTIFY_INC);
}

static void task1(void *arg)
{
    (void)arg;
    for(;;)
    {
        const rtos_evt_t got = rtos_waitEvents(EVT_A | EVT_P, RTOS_WAIT_ANY, RTOS_FOREVER);
        trace(T1, got == EVT_A ? "evA" : got == EVT_P ? "evP" : "evBoth", (int32_t)rtos_getTick());
    }
}

static void task2(void *arg)
{
    uint16_t v = 0;
    (void)arg;

    (void)rtos_notifyWait(&v, RTOS_FOREVER);
    trace(T2, "wait", v);
    if(rtos_notifyWait(&v, 1))
        trace(T2, "wait", v);
    else
        trace(T2, "waitTimeout", (int32_t)rtos_getTick());

    (void)rtos_notifyTake(RTOS_FOREVER);
    trace(T2, "take", 1);
    trace(T2, "take", rtos_notifyTake(RTOS_NO_WAIT) ? 1 : 0);
    trace(T2, "take", rtos_notifyTake(RTOS_NO_WAIT) ? 1 : 0);
    if(rtos_notifyTake(2))
        trace(T2, "take", 1);
    else
        trace(T2, "takeTimeout", (int32_t)rtos_getTick());
    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static void task3(void *arg)
{
    (void)arg;
    rtos_delay(1);

    /* Phase A, tick 2: notifications. */
    rtos_notify(T2, 0x0005u, RTOS_NOTIFY_SET);  trace(T3, "set", 0);
    rtos_notify(T2, 0x0010u, RTOS_NOTIFY_OR);   trace(T3, "or", 0);
    rtos_delay(1);

    /* Tick 4: T2 is blocked in take. */
    rtos_notify(T2, 0, RTOS_NOTIFY_SET);        trace(T3, "set0", 0);
    rtos_notify(T2, 0, RTOS_NOTIFY_INC);        trace(T3, "inc", 0);
    rtos_notify(T2, 0, RTOS_NOTIFY_INC);        trace(T3, "inc", 0);
    rtos_host_simulateIsr(isrNotifyInc);        trace(T3, "isrInc", 0);
    rtos_delay(1);

    /* Phase B, tick 6: timers. */
    rtos_timerInit(&_tmrA, cbA, NULL);
    rtos_timerInit(&_tmrP, cbP, NULL);
    rtos_timerInit(&_tmrS, cbS, NULL);
    rtos_timerStart(&_tmrA, 3, 0);
    rtos_timerStart(&_tmrP, 2, 2);
    rtos_timerStart(&_tmrS, 1, 0);
    rtos_timerStop(&_tmrS);
    trace(T3, "timersStarted", (int32_t)rtos_timerRemaining(&_tmrA));
    rtos_delay(10);

    trace(T3, "end", (int32_t)rtos_getTick());
    trace(T3, "cntS", _cntS);
    trace(T3, "active", (rtos_timerIsActive(&_tmrA) ? 1 : 0) + (rtos_timerIsActive(&_tmrP) ? 2 : 0) + (rtos_timerIsActive(&_tmrS) ? 4 : 0));
    _done = 1;
    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static const trace_t _expected[] =
{
    { T3, "set", 0 },
    { T3, "or", 0 },
    { T2, "wait", 0x15 },
    { T2, "waitTimeout", 3 },
    { T3, "set0", 0 },
    { T3, "inc", 0 },
    { T3, "inc", 0 },
    { T3, "isrInc", 0 },
    { T2, "take", 1 },
    { T2, "take", 1 },
    { T2, "take", 1 },
    { T3, "timersStarted", 3 },
    { T2, "takeTimeout", 6 },
    { T1, "evP", 8 },
    { T1, "evA", 9 },
    { T1, "evP", 10 },
    { T1, "evP", 12 },
    { T1, "evA", 14 },
    { T3, "end", 17 },
    { T3, "cntS", 0 },
    { T3, "active", 0 },
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
    rtos_createTask(T1, task1, NULL, _stack1, sizeof(_stack1));
    rtos_createTask(T2, task2, NULL, _stack2, sizeof(_stack2));
    rtos_createTask(T3, task3, NULL, _stack3, sizeof(_stack3));
    rtos_start();
    return 0;
}
