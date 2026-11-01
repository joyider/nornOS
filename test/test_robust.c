/**
 * @file test_robust.c
 * Robustness features on the host port: panic with record and hook, stack canary,
 * deadlock detection, task watchdog, timer task, object statistics, binary semaphore,
 * task inspection and the trace hook. Each scenario ends in a panic or an exit, so the
 * program runs every scenario in a child process (re-executing itself with the scenario
 * number) and judges the exit codes. Run with a number to run one scenario in place.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "rtos.h"
#include "rtos_kernel.h"

#define T1 1
#define T2 2
#define T3 3
#define TIMER_TASK RTOS_CFG_TIMER_TASK_PRIO

static uint8_t _stack1[16], _stack2[16];
static int _scenario;
static uint32_t _idleLoops;

static rtos_mutex_t _m1 = RTOS_MUTEX_INIT(T2);
static rtos_mutex_t _m2 = RTOS_MUTEX_INIT(T2);
static rtos_sem_t _bin = RTOS_SEM_INIT_BINARY(0);
static uint8_t _qStorage[2 * sizeof(uint16_t)];
static rtos_queue_t _q = RTOS_QUEUE_INIT(_qStorage, sizeof(uint16_t), 2);
static uint8_t _mbStorage[sizeof(uint16_t)];
static rtos_mbox_t _mb = RTOS_MBOX_INIT(_mbStorage, sizeof(uint16_t));
static uint8_t _poolStorage[2 * 8];
static rtos_pool_t _pool = RTOS_POOL_INIT(_poolStorage, 8, 2);
static rtos_timer_t _timer;
static rtos_sem_t _semBlock = RTOS_SEM_INIT(0);

static uint32_t _traceSwitches, _traceBlocks;
static uint8_t _timerCbTask = 0xFF;
static bool _timerCbDone = false;


static void pass(const char *what)
{
    printf("scenario %d PASS: %s\n", _scenario, what);
    exit(0);
}

static void failf(const char *what, long a, long b)
{
    printf("scenario %d FAIL: %s (%ld, %ld)\n", _scenario, what, a, b);
    exit(1);
}

#define EXPECT(cond, a, b)  do { if(!(cond)) failf(#cond, (long)(a), (long)(b)); } while(0)


/* ---- hooks --------------------------------------------------------------------------- */

void rtos_traceHook(uint8_t event, uint32_t a, uint32_t b)
{
    (void)a; (void)b;
    if(event == RTOS_TRACE_SWITCH) ++ _traceSwitches;
    if(event == RTOS_TRACE_BLOCK)  ++ _traceBlocks;
}

void rtos_panicHook(const rtos_panicInfo_t *pInfo)
{
    printf("panic reason=%u task=%u arg1=%lu arg2=%lu tick=%lu\n", pInfo->reason, pInfo->task,
           (unsigned long)pInfo->arg1, (unsigned long)pInfo->arg2, (unsigned long)pInfo->tick);
    switch(_scenario)
    {
    case 1:
        EXPECT(pInfo->reason == RTOS_PANIC_DEADLOCK, pInfo->reason, 0);
        EXPECT(pInfo->arg1 == T1 && pInfo->arg2 == T2, pInfo->arg1, pInfo->arg2);
        pass("deadlock T1<->T2 detected");
        break;
    case 2:
        EXPECT(pInfo->reason == RTOS_PANIC_STACK_OVERFLOW, pInfo->reason, 0);
        EXPECT(pInfo->arg1 == T1, pInfo->arg1, 0);
        pass("stack canary of T1 detected");
        break;
    case 3:
        EXPECT(pInfo->reason == RTOS_PANIC_WATCHDOG, pInfo->reason, 0);
        EXPECT(pInfo->arg1 == T1, pInfo->arg1, 0);
        EXPECT(pInfo->task == RTOS_IDLE_TASK, pInfo->task, 0);
        pass("task watchdog caught T1");
        break;
    case 7:
        EXPECT(pInfo->reason == RTOS_PANIC_ASSERT, pInfo->reason, 0);
        EXPECT(pInfo->line != 0 && pInfo->pFile != NULL, pInfo->line, 0);
        pass("assertion became a panic with file and line");
        break;
    default:
        failf("unexpected panic", pInfo->reason, pInfo->arg1);
    }
}

void test_idleHook(void)
{
    rtos_host_tick();
    if(++_idleLoops > 20000ul)
        failf("runaway: scenario did not finish", 0, 0);
}


/* ---- scenario tasks ------------------------------------------------------------------ */

/* 1: T1 holds m1 and blocks; T2 takes m2 and waits for m1; T1 wakes and asks for m2. */
static void s1_task1(void *arg)
{
    (void)arg;
    rtos_mutexLock(&_m1);
    rtos_delay(2);
    rtos_mutexLock(&_m2);               /* deadlock: panics */
    failf("deadlock not detected", 0, 0);
}
static void s1_task2(void *arg)
{
    (void)arg;
    rtos_delay(1);
    rtos_mutexLock(&_m2);
    rtos_mutexLock(&_m1);               /* owner T1 is blocked in delay: fallback wait */
    failf("T2 got m1", 0, 0);
}

/* 2: T1 overwrites the bottom of its own stack, then blocks: the switch checks it. */
static void s2_task1(void *arg)
{
    (void)arg;
    rtos_tcbAry[T1].pStackArea[0] = 0;
    rtos_delay(1);
    failf("stack corruption not detected", 0, 0);
}

/* 3: T1 is monitored every 5 ticks, checks in twice, then stops. */
static void s3_task1(void *arg)
{
    (void)arg;
    rtos_wdtInit(1000);
    rtos_wdtMonitor(T1, 5);
    rtos_delay(3); rtos_wdtCheckIn();
    rtos_delay(3); rtos_wdtCheckIn();
    rtos_delay(50);
    failf("watchdog did not fire", 0, 0);
}

/* 4: timer callback runs in the timer task and may block. */
static void s4_callback(void *arg)
{
    (void)arg;
    _timerCbTask = rtos_getCurrentTask();
    rtos_delay(1);                      /* legal in task context only */
    _timerCbDone = true;
}
static void s4_task1(void *arg)
{
    (void)arg;
    rtos_timerInit(&_timer, s4_callback, NULL);
    rtos_timerStart(&_timer, 2, 0);
    rtos_delay(10);
    EXPECT(_timerCbTask == TIMER_TASK, _timerCbTask, TIMER_TASK);
    EXPECT(_timerCbDone, 0, 0);
    EXPECT(!rtos_timerIsActive(&_timer), 0, 0);
    /* Stop while pending: start with delay 1, stop right after expiry before the task
       runs: T1 is above the timer task? No: timer task has prio 4 > T1, so it runs first.
       Instead check a periodic timer that keeps re-arming while pending. */
    _timerCbDone = false;
    rtos_timerStart(&_timer, 1, 1);
    rtos_delay(5);
    rtos_timerStop(&_timer);
    EXPECT(_timerCbDone, 0, 0);
    EXPECT(!rtos_timerIsActive(&_timer) && !_timer.pending, _timer.pending, 0);
    pass("timer callbacks run in the timer task");
}

/* 5: statistics, binary semaphore, task info. */
static void s5_task2(void *arg)
{
    (void)arg;
    (void)rtos_semTake(&_semBlock, RTOS_FOREVER);
    for(;;)
        rtos_delay(100);
}
static void s5_task1(void *arg)
{
    uint16_t v = 1;
    rtos_taskInfo_t info;
    void *b1, *b2, *b3;
    (void)arg;

    rtos_semGive(&_bin);
    rtos_semGive(&_bin);
    EXPECT(_bin.count == 1 && _bin.lost == 1, _bin.count, _bin.lost);
    EXPECT(rtos_semTake(&_bin, RTOS_NO_WAIT) && !rtos_semTake(&_bin, RTOS_NO_WAIT), 0, 0);

    EXPECT(rtos_queueSend(&_q, &v, RTOS_NO_WAIT), 0, 0);
    EXPECT(rtos_queueSend(&_q, &v, RTOS_NO_WAIT), 0, 0);
    EXPECT(!rtos_queueSend(&_q, &v, RTOS_NO_WAIT), 0, 0);
    EXPECT(!rtos_queueSend(&_q, &v, 2), 0, 0);
    EXPECT(_q.maxCount == 2 && _q.dropped == 2, _q.maxCount, _q.dropped);

    EXPECT(!rtos_mboxPost(&_mb, &v) && rtos_mboxPost(&_mb, &v), 0, 0);
    EXPECT(_mb.overwrites == 1, _mb.overwrites, 0);

    rtos_poolInit(&_pool, _poolStorage, 8, 2);
    b1 = rtos_poolAlloc(&_pool, RTOS_NO_WAIT);
    b2 = rtos_poolAlloc(&_pool, RTOS_NO_WAIT);
    b3 = rtos_poolAlloc(&_pool, 1);
    EXPECT(b1 != NULL && b2 != NULL && b3 == NULL, 0, 0);
    EXPECT(_pool.minFree == 0 && _pool.failed == 1, _pool.minFree, _pool.failed);
    rtos_poolFree(&_pool, b1);
    rtos_poolFree(&_pool, b2);

    rtos_delay(1);                      /* let T2 block on the semaphore */
    EXPECT(rtos_getTaskInfo(T2, &info), 0, 0);
    EXPECT(info.state == RTOS_TASK_BLOCKED, info.state, 0);
    EXPECT(info.waitKind == RTOS_WAIT_SEM && info.pWaitObj == &_semBlock, info.waitKind, 0);
    EXPECT(!info.hasTimeout, info.hasTimeout, 0);
    EXPECT(rtos_getTaskInfo(T1, &info) && info.state == RTOS_TASK_RUNNING, info.state, 0);
    EXPECT(info.stackSize == 16 && info.prio == T1 && info.effPrio == T1, info.stackSize, 0);
    EXPECT(rtos_getTaskInfo(T3, &info) && info.state == RTOS_TASK_NONE, info.state, 0);
    EXPECT(rtos_getTaskInfo(RTOS_IDLE_TASK, &info) && info.state == RTOS_TASK_READY, info.state, 0);
    EXPECT(!rtos_getTaskInfo(RTOS_NUM_TASKS, &info), 0, 0);
    rtos_mutexLock(&_m1);
    EXPECT(rtos_getTaskInfo(T1, &info) && info.effPrio == T2, info.effPrio, 0);
    rtos_mutexUnlock(&_m1);
    pass("statistics, binary semaphore and task info");
}

/* 6: trace hook sees switches and blocks. */
static void s6_task1(void *arg)
{
    (void)arg;
    rtos_delay(3);
    EXPECT(_traceSwitches >= 2 && _traceBlocks >= 1, _traceSwitches, _traceBlocks);
    pass("trace hook");
}

/* 7: an assertion failure goes through rtos_panic. */
static void s7_task1(void *arg)
{
    (void)arg;
    rtos_semInitMax(&_bin, 5, 1);       /* initial > max: asserts */
    failf("assertion did not panic", 0, 0);
}


static int runScenario(int n)
{
    _scenario = n;
    rtos_init();
    switch(n)
    {
    case 1:
        rtos_createTask(T1, s1_task1, NULL, _stack1, sizeof(_stack1));
        rtos_createTask(T2, s1_task2, NULL, _stack2, sizeof(_stack2));
        break;
    case 2: rtos_createTask(T1, s2_task1, NULL, _stack1, sizeof(_stack1)); break;
    case 3: rtos_createTask(T1, s3_task1, NULL, _stack1, sizeof(_stack1)); break;
    case 4: rtos_createTask(T1, s4_task1, NULL, _stack1, sizeof(_stack1)); break;
    case 5:
        rtos_createTask(T1, s5_task1, NULL, _stack1, sizeof(_stack1));
        rtos_createTask(T2, s5_task2, NULL, _stack2, sizeof(_stack2));
        break;
    case 6: rtos_createTask(T1, s6_task1, NULL, _stack1, sizeof(_stack1)); break;
    case 7: rtos_createTask(T1, s7_task1, NULL, _stack1, sizeof(_stack1)); break;
    default:
        return 2;
    }
    rtos_start();
    return 1;
}

#define NUM_SCENARIOS 7

int main(int argc, char **argv)
{
    int n, failures = 0;

    if(argc > 1)
        return runScenario(atoi(argv[1]));

    for(n=1; n<=NUM_SCENARIOS; ++n)
    {
        char num[8];
        int status = 0;
        pid_t pid;

        snprintf(num, sizeof(num), "%d", n);
        fflush(stdout);
        pid = fork();
        if(pid == 0)
        {
            execl(argv[0], argv[0], num, (char *)NULL);
            _exit(99);
        }
        waitpid(pid, &status, 0);
        if(!WIFEXITED(status)  ||  WEXITSTATUS(status) != 0)
        {
            printf("scenario %d: exit status %d\n", n, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            ++ failures;
        }
    }
    printf("%s: test_robust (%d scenarios, %d failures)\n", failures == 0 ? "PASS" : "FAIL",
           NUM_SCENARIOS, failures);
    return failures == 0 ? 0 : 1;
}
