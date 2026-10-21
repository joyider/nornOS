/**
 * @file test_queue.c
 * Host unit test of the message queue and the mailbox. Task 3 scripts the scenario,
 * task 1 consumes the queue, task 2 consumes the mailbox. See test_kernel.c for the
 * trace mechanism.
 *
 * Covered: send to a blocked reader (direct hand-over), FIFO order, blocking on a full
 * queue with hand-over into the freed slot, non-blocking send on full, send and receive
 * from a simulated ISR, receive timeout, mailbox hand-over, overwrite detection and
 * fetch timeout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtos.h"

#define T1  1u
#define T2  2u
#define T3  3u

typedef struct { uint8_t task; const char *tag; int32_t val; } trace_t;

static trace_t _trace[64];
static uint8_t _nTrace = 0;
static volatile int _done = 0;
static uint32_t _idleLoops = 0;
static uint8_t _stack1[1], _stack2[1], _stack3[1];

static uint8_t _qStorage[2 * sizeof(uint16_t)];
static rtos_queue_t _q = RTOS_QUEUE_INIT(_qStorage, sizeof(uint16_t), 2);
static uint8_t _mbStorage[sizeof(uint16_t)];
static rtos_mbox_t _mb = RTOS_MBOX_INIT(_mbStorage, sizeof(uint16_t));

static volatile rtos_tick_t _rxTimeout = RTOS_FOREVER;

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

static void send(uint16_t v, rtos_tick_t timeout)
{
    if(rtos_queueSend(&_q, &v, timeout))
        trace(T3, "sent", v);
    else
        trace(T3, "sendFull", v);
}

static void isrSend70(void)
{
    const uint16_t v = 70;
    const bool ok = rtos_queueSendFromIsr(&_q, &v);
    trace(T3, ok ? "isrSent" : "isrSendFull", v);
}

static void isrReceive(void)
{
    uint16_t v = 0;
    const bool ok = rtos_queueReceiveFromIsr(&_q, &v);
    trace(T3, ok ? "isrRx" : "isrRxEmpty", v);
}

static void task1(void *arg)
{
    (void)arg;
    for(;;)
    {
        uint16_t v = 0;
        if(rtos_queueReceive(&_q, &v, _rxTimeout))
            trace(T1, "rx", v);
        else
            trace(T1, "rxTimeout", (int32_t)rtos_getTick());
    }
}

static void task2(void *arg)
{
    uint16_t v = 0;
    (void)arg;

    (void)rtos_mboxFetch(&_mb, &v, RTOS_FOREVER);
    trace(T2, "mb", v);
    (void)rtos_mboxFetch(&_mb, &v, RTOS_FOREVER);
    trace(T2, "mb", v);
    if(rtos_mboxFetch(&_mb, &v, 2))
        trace(T2, "mb", v);
    else
        trace(T2, "mbTimeout", (int32_t)rtos_getTick());
    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static void task3(void *arg)
{
    uint16_t v;
    (void)arg;

    /* Let the consumers start and block. */
    rtos_delay(1);

    /* Phase A, tick 2: hand-over, FIFO, blocking on full, non-blocking on full. */
    send(10, RTOS_NO_WAIT);
    send(20, RTOS_NO_WAIT);
    send(30, RTOS_NO_WAIT);
    send(40, 5);
    send(50, RTOS_NO_WAIT);
    rtos_delay(3);

    /* Phase B, tick 6: receive timeout, ISR send and receive. */
    _rxTimeout = 2;
    send(60, RTOS_NO_WAIT);
    rtos_delay(1);
    rtos_host_simulateIsr(isrSend70);
    send(80, RTOS_NO_WAIT);
    rtos_host_simulateIsr(isrReceive);
    send(90, RTOS_NO_WAIT);
    rtos_delay(2);

    /* Phase C, tick 11: mailbox. */
    _rxTimeout = RTOS_FOREVER;
    v = 1; trace(T3, "post", rtos_mboxPost(&_mb, &v) ? 1 : 0);
    v = 2; trace(T3, "post", rtos_mboxPost(&_mb, &v) ? 1 : 0);
    v = 3; trace(T3, "post", rtos_mboxPost(&_mb, &v) ? 1 : 0);
    rtos_delay(5);

    trace(T3, "end", (int32_t)rtos_getTick());
    _done = 1;
    for(;;)
        rtos_delay(RTOS_FOREVER);
}

static const trace_t _expected[] =
{
    { T3, "sent", 10 },
    { T3, "sent", 20 },
    { T3, "sent", 30 },
    { T1, "rx", 10 },
    { T3, "sent", 40 },
    { T3, "sendFull", 50 },
    { T1, "rx", 20 },
    { T1, "rx", 30 },
    { T1, "rx", 40 },
    { T3, "sent", 60 },
    { T1, "rx", 60 },
    { T3, "isrSent", 70 },
    { T3, "sent", 80 },
    { T3, "isrRx", 70 },
    { T3, "sent", 90 },
    { T1, "rxTimeout", 8 },
    { T1, "rx", 80 },
    { T1, "rx", 90 },
    { T1, "rxTimeout", 10 },
    { T3, "post", 0 },
    { T3, "post", 0 },
    { T3, "post", 1 },
    { T2, "mb", 1 },
    { T2, "mb", 3 },
    { T1, "rxTimeout", 12 },
    { T2, "mbTimeout", 13 },
    { T3, "end", 17 },
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
