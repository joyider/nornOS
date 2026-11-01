/**
 * @file rtos_kernel.c
 * Scheduler, task management, system time, delays and event flags.
 *
 * Scheduling model: one task per priority, the task id is the priority, task 0 is the idle
 * task and is always ready. The scheduler keeps a bitmask of ready tasks; the running task
 * is the highest set bit unless the current task holds a ceiling mutex, in which case it
 * keeps running until a task above its effective priority becomes ready.
 *
 * Blocking model: a task blocks by clearing its own ready bit and calling schedule(). It is
 * made ready again by an event, a semaphore, a mutex hand-over or the tick ISR (timeout).
 * Timed waits sit in one list sorted by wake-up tick, so the tick ISR only inspects the head.
 *
 * All kernel functions that touch shared state run inside a port critical section. The
 * context switch itself is done by the port layer (rtos_port_switchContext), which saves
 * the callee-saved registers of the current task, stores its stack pointer in the TCB and
 * loads the next task.
 */

#include "rtos.h"
#include "rtos_assert.h"
#include "rtos_kernel.h"


/*
 * Defines
 */

/** Byte pattern written into unused stack space for rtos_getStackFree. */
#define STACK_FILL_PATTERN      RTOS_STACK_FILL


/*
 * Local prototypes
 */

static void insertTimed(rtos_tcb_t *pT);
static void removeTimed(rtos_tcb_t *pT);


/*
 * Data definitions
 */

/** All task control blocks, index = task id = priority. Element 0 is the idle task. */
rtos_tcb_t rtos_tcbAry[RTOS_NUM_TASKS];

/** The running task. Read and written by the port layer during a context switch. */
rtos_tcb_t *rtos_pCurrent = &rtos_tcbAry[RTOS_IDLE_TASK];

/** The task to switch to. Written by the kernel right before rtos_port_switchContext. */
rtos_tcb_t *rtos_pNext = &rtos_tcbAry[RTOS_IDLE_TASK];

/** Set whenever a task was made ready from an ISR; evaluated by rtos_isrTail. */
volatile uint8_t rtos_needSwitch = 0;

/** Bit n set: task n is ready to run. Bit 0 (idle) is always set. */
static rtos_taskmask_t _readyMask = 1u;

/** Head of the list of tasks waiting with a timeout, sorted by wake-up tick. */
static rtos_tcb_t *_pTimedHead = NULL;

/** The system time. */
volatile rtos_tick_t rtos_tickCount = 0;

#if RTOS_CFG_RUNTIME_STATS
/** Run time counter value at the last context switch. */
static rtos_runtime_t _lastSwitchTime = 0;
/** Reference values of the previous rtos_getCpuLoad call. */
static rtos_runtime_t _loadRefTime = 0;
static rtos_runtime_t _loadRefIdle = 0;
#endif

/** Index of the highest set bit of a nibble; 0 for 0. */
static const uint8_t _hiBitNibble[16] = {0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3};


/*
 * Function implementation
 */

/** Index of the highest set bit of a byte. Must not be called with 0. */
static inline uint8_t highestBit8(uint8_t b)
{
    return (b & 0xF0u) ? (uint8_t)(4u + _hiBitNibble[b >> 4]) : _hiBitNibble[b];
}


/**
 * Index of the highest set bit of a task mask. Must not be called with 0 (never happens,
 * the idle bit is always set). A port with a count-leading-zeros instruction defines
 * RTOS_PORT_HIGHEST_BIT(m) and the nibble table is not used.
 */
static inline uint8_t highestBit(rtos_taskmask_t m)
{
#if defined(RTOS_PORT_HIGHEST_BIT)
    return (uint8_t)RTOS_PORT_HIGHEST_BIT(m);
#elif RTOS_CFG_NUM_TASKS + 1 > 16
    if(m & 0xFF000000ul)
        return (uint8_t)(24u + highestBit8((uint8_t)(m >> 24)));
    if(m & 0x00FF0000ul)
        return (uint8_t)(16u + highestBit8((uint8_t)(m >> 16)));
    if(m & 0x0000FF00ul)
        return (uint8_t)(8u + highestBit8((uint8_t)(m >> 8)));
    return highestBit8((uint8_t)m);
#elif RTOS_CFG_NUM_TASKS + 1 > 8
    if(m & 0xFF00u)
        return (uint8_t)(8u + highestBit8((uint8_t)(m >> 8)));
    return highestBit8((uint8_t)m);
#else
    return highestBit8(m);
#endif
}


/**
 * Charge the time since the last switch to the current task. Interrupts disabled.
 */
static inline void accountRunTime(void)
{
#if RTOS_CFG_RUNTIME_STATS
    const rtos_runtime_t now = rtos_port_getRunTimeCounter();
    rtos_pCurrent->runTime += now - _lastSwitchTime;
    _lastSwitchTime = now;
#endif
}


/**
 * Stack bottom canary: the first bytes of a task's stack must still hold the fill
 * pattern. Checked for the running task before every switch and at every tick, so an
 * overflow is caught within a tick of happening instead of when the corruption bites.
 */
static inline void checkStack(const rtos_tcb_t *pT)
{
#if RTOS_CFG_STACK_CHECK >= 2
    const uint8_t * const p = pT->pStackArea;
    if(p != NULL
       &&  (p[0] != STACK_FILL_PATTERN  ||  p[1] != STACK_FILL_PATTERN
            ||  p[2] != STACK_FILL_PATTERN  ||  p[3] != STACK_FILL_PATTERN)
      )
        rtos_panic(RTOS_PANIC_STACK_OVERFLOW, pT->prio, 0);
#else
    (void)pT;
#endif
}


/**
 * Make a blocked task ready. Interrupts must be disabled. Does not switch; the caller
 * decides when to run the scheduler (immediately in task context, at ISR exit in ISR
 * context).
 */
rtos_taskmask_t rtos_kernel_getReadyMask(void)
{
    return _readyMask;
}


rtos_tcb_t *rtos_kernel_getTimedHead(void)
{
    return _pTimedHead;
}


void rtos_kernel_makeReady(rtos_tcb_t *pT)
{
    if(pT->flags & RTOS_TF_IN_TIMED_LIST)
        removeTimed(pT);
    if(pT->pWaiters != NULL)
    {
        *pT->pWaiters &= (rtos_taskmask_t)~pT->bit;
        pT->pWaiters = NULL;
    }
    RTOS_TRACE(RTOS_TRACE_READY, pT->prio, pT->waitKind);
#if RTOS_CFG_SUSPEND
    if(pT->flags & RTOS_TF_SUSPENDED)
    {
        /* The wake up is consumed but the task stays off the ready mask until resume. */
        pT->flags |= RTOS_TF_READY_PENDING;
        return;
    }
#endif
    _readyMask |= pT->bit;
    rtos_needSwitch = 1;
}


/**
 * Pick the task to run and switch to it if it is not the current one. Interrupts must be
 * disabled. Called in task context only.
 */
void rtos_kernel_schedule(void)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    const uint8_t hi = highestBit(_readyMask);

    rtos_needSwitch = 0;

    /* Switch if the current task blocked itself, or if a ready task is above the current
       task's effective priority (base priority or a mutex ceiling). */
    if((_readyMask & pCur->bit) == 0  ||  hi > pCur->effPrio)
    {
        rtos_pNext = &rtos_tcbAry[hi];
        if(rtos_pNext != pCur)
        {
            checkStack(pCur);
            RTOS_TRACE(RTOS_TRACE_SWITCH, pCur->prio, rtos_pNext->prio);
            accountRunTime();
            rtos_port_switchContext();
        }
    }
}


/**
 * Block the current task until it is made ready again, with an optional timeout.
 * Interrupts must be disabled. Returns true if the wake-up was caused by the timeout.
 */
bool rtos_kernel_blockCurrent(rtos_tick_t timeout)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;

    RTOS_ASSERT(pCur != &rtos_tcbAry[RTOS_IDLE_TASK]);

    pCur->flags &= (uint8_t)~RTOS_TF_TIMED_OUT;
    if(timeout != RTOS_FOREVER)
    {
        pCur->wakeAt = rtos_tickCount + timeout;
        insertTimed(pCur);
    }
    RTOS_TRACE(RTOS_TRACE_BLOCK, pCur->prio, pCur->waitKind);
    _readyMask &= (rtos_taskmask_t)~pCur->bit;
    rtos_kernel_schedule();

    /* We are back: someone made us ready. The waiter pointer has been cleared by
       whoever did it. */
    pCur->waitKind = RTOS_WAIT_NONE;
    pCur->pWaitObj = NULL;
    return (pCur->flags & RTOS_TF_TIMED_OUT) != 0;
}


bool rtos_kernel_blockOn(uint8_t waitKind, const void *pWaitObj, rtos_tick_t timeout)
{
    rtos_pCurrent->waitKind = waitKind;
    rtos_pCurrent->pWaitObj = pWaitObj;
    return rtos_kernel_blockCurrent(timeout);
}


/**
 * Insert a task into the timed list, sorted by wake-up time. Interrupts disabled.
 */
static void insertTimed(rtos_tcb_t *pT)
{
    rtos_tcb_t **ppLink = &_pTimedHead;

    while(*ppLink != NULL
          &&  (rtos_tickdiff_t)((*ppLink)->wakeAt - pT->wakeAt) <= 0
         )
    {
        ppLink = &(*ppLink)->pNextTimed;
    }
    pT->pNextTimed = *ppLink;
    *ppLink = pT;
    pT->flags |= RTOS_TF_IN_TIMED_LIST;
}


/**
 * Remove a task from the timed list. Interrupts disabled.
 */
static void removeTimed(rtos_tcb_t *pT)
{
    rtos_tcb_t **ppLink = &_pTimedHead;

    while(*ppLink != NULL  &&  *ppLink != pT)
        ppLink = &(*ppLink)->pNextTimed;

    RTOS_ASSERT(*ppLink == pT);
    *ppLink = pT->pNextTimed;
    pT->flags &= (uint8_t)~RTOS_TF_IN_TIMED_LIST;
}


/**
 * The tick. Called by the port layer from the timer ISR with interrupts disabled.
 */
void rtos_tickFromIsr(void)
{
    const rtos_tick_t now = ++rtos_tickCount;

    checkStack(rtos_pCurrent);

    while(_pTimedHead != NULL  &&  (rtos_tickdiff_t)(_pTimedHead->wakeAt - now) <= 0)
    {
        rtos_tcb_t * const pT = _pTimedHead;

        _pTimedHead = pT->pNextTimed;
        pT->flags = (uint8_t)((pT->flags & ~RTOS_TF_IN_TIMED_LIST) | RTOS_TF_TIMED_OUT);
        pT->evtWait = 0;
        rtos_kernel_makeReady(pT);
    }

#if RTOS_CFG_SW_TIMERS
    rtos_kernel_timerTick(now);
#endif
}


/**
 * Called by the port layer at the end of every RTOS_ISR with interrupts disabled. Switches
 * to a higher priority task if one was made ready by the ISR.
 */
void rtos_isrTail(void)
{
    if(rtos_needSwitch)
    {
        const uint8_t hi = highestBit(_readyMask);

        rtos_needSwitch = 0;
        if(hi > rtos_pCurrent->effPrio)
        {
            rtos_pNext = &rtos_tcbAry[hi];
            checkStack(rtos_pCurrent);
            RTOS_TRACE(RTOS_TRACE_SWITCH, rtos_pCurrent->prio, hi);
            accountRunTime();
            rtos_port_switchContext();
        }
    }
}


/**
 * First code executed in a new task's context. Reached through the return address placed
 * on the prepared stack. Interrupts are disabled on entry.
 */
void rtos_taskTrampoline(void)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;

    pCur->periodBase = rtos_tickCount;
    rtos_port_enableInterrupts();
    pCur->taskFunc(pCur->arg);

    /* The task function returned. Terminate the task: block forever. */
    (void)rtos_port_enterCritical();
    _readyMask &= (rtos_taskmask_t)~pCur->bit;
    rtos_kernel_schedule();

    /* Never reached. */
    for(;;)
        ;
}


/*
 * Public API
 */

void rtos_init(void)
{
    uint8_t i;

    for(i=0; i<RTOS_NUM_TASKS; ++i)
    {
        rtos_tcb_t * const pT = &rtos_tcbAry[i];

        pT->ctx = (rtos_port_ctx_t)0;
        pT->taskFunc = NULL;
        pT->arg = NULL;
        pT->pNextTimed = NULL;
        pT->pWaiters = NULL;
        pT->pXfer = NULL;
        pT->wakeAt = 0;
        pT->periodBase = 0;
        pT->evtPending = 0;
        pT->evtWait = 0;
        pT->notifyValue = 0;
#if RTOS_CFG_RUNTIME_STATS
        pT->runTime = 0;
#endif
        pT->pStackArea = NULL;
        pT->stackSize = 0;
        pT->bit = (rtos_taskmask_t)1u << i;
        pT->prio = i;
        pT->effPrio = i;
        pT->flags = 0;
        pT->cntOverrun = 0;
        pT->waitKind = RTOS_WAIT_NONE;
        pT->pWaitObj = NULL;
    }
    rtos_kernel_panicInit();
    _readyMask = rtos_tcbAry[RTOS_IDLE_TASK].bit;
    _pTimedHead = NULL;
    rtos_tickCount = 0;
    rtos_needSwitch = 0;
    rtos_pCurrent = &rtos_tcbAry[RTOS_IDLE_TASK];
    rtos_pNext = rtos_pCurrent;
}


void rtos_createTask( uint8_t prio
                    , rtos_taskFunc_t taskFunc
                    , void *arg
                    , uint8_t *stackArea
                    , size_t stackSize
                    )
{
    rtos_tcb_t *pT;

    RTOS_ASSERT(prio >= 1  &&  prio < RTOS_NUM_TASKS);
    RTOS_ASSERT(taskFunc != NULL  &&  stackArea != NULL);
    pT = &rtos_tcbAry[prio];
    RTOS_ASSERT(pT->taskFunc == NULL);

    /* Trim the area to the alignment the port's ABI requires for stack frames. */
    {
        const size_t misalign = (size_t)((uintptr_t)stackArea % RTOS_PORT_STACK_ALIGN);
        if(misalign != 0)
        {
            const size_t skip = RTOS_PORT_STACK_ALIGN - misalign;
            RTOS_ASSERT(stackSize > skip);
            stackArea += skip;
            stackSize -= skip;
        }
        stackSize -= stackSize % RTOS_PORT_STACK_ALIGN;
    }
    RTOS_ASSERT(stackSize >= RTOS_PORT_MIN_STACK);

    pT->taskFunc = taskFunc;
    pT->arg = arg;
    pT->pStackArea = stackArea;
    pT->stackSize = stackSize;

#if RTOS_CFG_STACK_CHECK
    {
        size_t i;
        for(i=0; i<stackSize; ++i)
            stackArea[i] = STACK_FILL_PATTERN;
    }
#endif
    pT->ctx = rtos_port_initStack(stackArea, stackSize, rtos_taskTrampoline);
    _readyMask |= pT->bit;
}


void rtos_start(void)
{
#if RTOS_CFG_TIMER_TASK_PRIO > 0
    rtos_kernel_timerTaskCreate();
#endif
    rtos_port_startTimer();
    (void)rtos_port_enterCritical();
#if RTOS_CFG_RUNTIME_STATS
    _lastSwitchTime = rtos_port_getRunTimeCounter();
    _loadRefTime = _lastSwitchTime;
    _loadRefIdle = 0;
#endif
    rtos_kernel_schedule();
    rtos_port_enableInterrupts();

    /* From here on this context is the idle task. */
    for(;;)
    {
#ifdef RTOS_CFG_IDLE_HOOK
        RTOS_CFG_IDLE_HOOK();
#endif
#if RTOS_CFG_WATCHDOG
        rtos_kernel_wdtIdle();
#endif
#if RTOS_CFG_TICKLESS
        rtos_port_idleSleepTicks(rtos_kernel_ticksToNextWake());
#elif RTOS_CFG_IDLE_SLEEP
        rtos_port_idleSleep();
#endif
    }
}


rtos_tick_t rtos_getTick(void)
{
    rtos_tick_t t;
    const rtos_port_cs_t cs = rtos_port_enterCritical();
    t = rtos_tickCount;
    rtos_port_exitCritical(cs);
    return t;
}


void rtos_delay(rtos_tick_t ticks)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    /* +1: the call is not synchronized with the tick, so the first tick may come
       immediately. The delay is guaranteed to be at least \a ticks. */
    if(ticks != RTOS_FOREVER)
        ++ ticks;
    (void)rtos_kernel_blockOn(RTOS_WAIT_DELAY, NULL, ticks);
    rtos_port_exitCritical(cs);
}


bool rtos_waitPeriodic(rtos_tick_t period)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    bool overrun = false;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(period > 0);
    pCur->periodBase += period;
    if((rtos_tickdiff_t)(pCur->periodBase - rtos_tickCount) <= 0)
    {
        /* The boundary is already in the past: overrun. Re-base to now so that the task
           runs immediately and the next period starts from here. */
        overrun = true;
        if(pCur->cntOverrun < 0xFFu)
            ++ pCur->cntOverrun;
        pCur->periodBase = rtos_tickCount;
    }
    else
    {
        pCur->wakeAt = pCur->periodBase;
        pCur->waitKind = RTOS_WAIT_PERIODIC;
        insertTimed(pCur);
        _readyMask &= (rtos_taskmask_t)~pCur->bit;
        rtos_kernel_schedule();
        pCur->waitKind = RTOS_WAIT_NONE;
    }
    rtos_port_exitCritical(cs);
    return overrun;
}


/**
 * The event condition check shared by wait and post.
 *   @return The satisfying events, or 0 if the condition is not met.
 */
static inline rtos_evt_t evtMatch(rtos_evt_t pending, rtos_evt_t mask, uint8_t flags)
{
    const rtos_evt_t got = pending & mask;

    if(flags & RTOS_TF_WAIT_ALL)
        return got == mask ? mask : 0;
    else
        return got;
}


rtos_evt_t rtos_waitEvents(rtos_evt_t mask, uint8_t opt, rtos_tick_t timeout)
{
    rtos_tcb_t * const pCur = rtos_pCurrent;
    rtos_evt_t got;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(mask != 0);
    if(opt == RTOS_WAIT_ALL)
        pCur->flags |= RTOS_TF_WAIT_ALL;
    else
        pCur->flags &= (uint8_t)~RTOS_TF_WAIT_ALL;

    got = evtMatch(pCur->evtPending, mask, pCur->flags);
    if(got == 0  &&  timeout != RTOS_NO_WAIT)
    {
        pCur->evtWait = mask;
        (void)rtos_kernel_blockOn(RTOS_WAIT_EVENTS, NULL, timeout);
        pCur->evtWait = 0;
        got = evtMatch(pCur->evtPending, mask, pCur->flags);
    }
    pCur->evtPending &= (rtos_evt_t)~got;
    rtos_port_exitCritical(cs);
    return got;
}


/**
 * Post events to a task; interrupts disabled. Makes the task ready if its wait condition
 * is now met. Returns nothing; scheduling is up to the caller.
 */
static inline void postEventsInner(rtos_tcb_t *pT, rtos_evt_t events)
{
    pT->evtPending |= events;
    if(pT->evtWait != 0  &&  evtMatch(pT->evtPending, pT->evtWait, pT->flags) != 0)
    {
        pT->evtWait = 0;
        rtos_kernel_makeReady(pT);
    }
}


void rtos_postEvents(uint8_t taskId, rtos_evt_t events)
{
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(taskId >= 1  &&  taskId < RTOS_NUM_TASKS);
    postEventsInner(&rtos_tcbAry[taskId], events);
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}


void rtos_postEventsFromIsr(uint8_t taskId, rtos_evt_t events)
{
    RTOS_ASSERT(taskId >= 1  &&  taskId < RTOS_NUM_TASKS);
    postEventsInner(&rtos_tcbAry[taskId], events);
}


void rtos_broadcastEvents(rtos_evt_t events)
{
    uint8_t i;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    for(i=1; i<RTOS_NUM_TASKS; ++i)
        if(rtos_tcbAry[i].taskFunc != NULL)
            postEventsInner(&rtos_tcbAry[i], events);
    rtos_kernel_schedule();
    rtos_port_exitCritical(cs);
}


uint8_t rtos_getCurrentTask(void)
{
    return rtos_pCurrent->prio;
}


uint8_t rtos_getOverrunCount(uint8_t taskId, bool reset)
{
    uint8_t n;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    n = rtos_tcbAry[taskId].cntOverrun;
    if(reset)
        rtos_tcbAry[taskId].cntOverrun = 0;
    rtos_port_exitCritical(cs);
    return n;
}


size_t rtos_getStackFree(uint8_t taskId)
{
#if RTOS_CFG_STACK_CHECK
    const rtos_tcb_t * const pT = &rtos_tcbAry[taskId];
    const uint8_t *p = pT->pStackArea;
    size_t n = 0;

    if(p == NULL)
        return 0;
    while(n < pT->stackSize  &&  *p == STACK_FILL_PATTERN)
    {
        ++ p;
        ++ n;
    }
    return n;
#else
    (void)taskId;
    return 0;
#endif
}


#if RTOS_CFG_SUSPEND
void rtos_suspend(uint8_t taskId)
{
    rtos_tcb_t *pT;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(taskId >= 1  &&  taskId < RTOS_NUM_TASKS);
    pT = &rtos_tcbAry[taskId];
    if((pT->flags & RTOS_TF_SUSPENDED) == 0)
    {
        pT->flags |= RTOS_TF_SUSPENDED;
        if(_readyMask & pT->bit)
        {
            /* Was runnable: remember that, so resume makes it runnable again. */
            _readyMask &= (rtos_taskmask_t)~pT->bit;
            pT->flags |= RTOS_TF_READY_PENDING;
        }
        if(pT == rtos_pCurrent)
            rtos_kernel_schedule();
    }
    rtos_port_exitCritical(cs);
}


void rtos_resume(uint8_t taskId)
{
    rtos_tcb_t *pT;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    RTOS_ASSERT(taskId >= 1  &&  taskId < RTOS_NUM_TASKS);
    pT = &rtos_tcbAry[taskId];
    if(pT->flags & RTOS_TF_SUSPENDED)
    {
        pT->flags &= (uint8_t)~RTOS_TF_SUSPENDED;
        if(pT->flags & RTOS_TF_READY_PENDING)
        {
            pT->flags &= (uint8_t)~RTOS_TF_READY_PENDING;
            _readyMask |= pT->bit;
            rtos_needSwitch = 1;
            rtos_kernel_schedule();
        }
    }
    rtos_port_exitCritical(cs);
}


void rtos_resumeFromIsr(uint8_t taskId)
{
    rtos_tcb_t * const pT = &rtos_tcbAry[taskId];

    RTOS_ASSERT(taskId >= 1  &&  taskId < RTOS_NUM_TASKS);
    if((pT->flags & RTOS_TF_SUSPENDED) != 0)
    {
        pT->flags &= (uint8_t)~RTOS_TF_SUSPENDED;
        if(pT->flags & RTOS_TF_READY_PENDING)
        {
            pT->flags &= (uint8_t)~RTOS_TF_READY_PENDING;
            _readyMask |= pT->bit;
            rtos_needSwitch = 1;        /* the ISR tail switches */
        }
    }
}
#endif


rtos_tick_t rtos_kernel_ticksToNextWake(void)
{
    rtos_tick_t next = RTOS_FOREVER;
    if(_pTimedHead != NULL)
    {
        const rtos_tickdiff_t d = (rtos_tickdiff_t)(_pTimedHead->wakeAt - rtos_tickCount);
        next = d <= 0 ? 0u : (rtos_tick_t)d;
    }
#if RTOS_CFG_SW_TIMERS
    {
        const rtos_tick_t t = rtos_kernel_timerTicksToNext();
        if(t < next)
            next = t;
    }
#endif
    return next;
}


bool rtos_getTaskInfo(uint8_t taskId, rtos_taskInfo_t *pInfo)
{
    const rtos_tcb_t *pT;
    rtos_port_cs_t cs;

    if(taskId >= RTOS_NUM_TASKS  ||  pInfo == NULL)
        return false;
    pT = &rtos_tcbAry[taskId];
    cs = rtos_port_enterCritical();
    if(taskId != RTOS_IDLE_TASK  &&  pT->taskFunc == NULL)
        pInfo->state = RTOS_TASK_NONE;
    else if(pT->flags & RTOS_TF_SUSPENDED)
        pInfo->state = RTOS_TASK_SUSPENDED;
    else if(pT == rtos_pCurrent)
        pInfo->state = RTOS_TASK_RUNNING;
    else if(_readyMask & pT->bit)
        pInfo->state = RTOS_TASK_READY;
    else
        pInfo->state = RTOS_TASK_BLOCKED;
    pInfo->prio = pT->prio;
    pInfo->effPrio = pT->effPrio;
    pInfo->waitKind = pInfo->state >= RTOS_TASK_BLOCKED ? pT->waitKind : RTOS_WAIT_NONE;
    pInfo->pWaitObj = pInfo->state >= RTOS_TASK_BLOCKED ? pT->pWaitObj : NULL;
    pInfo->hasTimeout = (pT->flags & RTOS_TF_IN_TIMED_LIST) != 0;
    pInfo->wakeAt = pT->wakeAt;
    pInfo->stackSize = pT->stackSize;
    pInfo->overruns = pT->cntOverrun;
#if RTOS_CFG_RUNTIME_STATS
    accountRunTime();
    pInfo->runTime = pT->runTime;
#else
    pInfo->runTime = 0;
#endif
    rtos_port_exitCritical(cs);
    pInfo->stackFree = rtos_getStackFree(taskId);
    return true;
}


rtos_runtime_t rtos_getTaskRunTime(uint8_t taskId)
{
#if RTOS_CFG_RUNTIME_STATS
    rtos_runtime_t t;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    accountRunTime();
    t = rtos_tcbAry[taskId].runTime;
    rtos_port_exitCritical(cs);
    return t;
#else
    (void)taskId;
    return 0;
#endif
}


uint8_t rtos_getCpuLoad(void)
{
#if RTOS_CFG_RUNTIME_STATS
    rtos_runtime_t total, idle;
    uint8_t load;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    accountRunTime();
    total = _lastSwitchTime - _loadRefTime;
    idle = rtos_tcbAry[RTOS_IDLE_TASK].runTime - _loadRefIdle;
    _loadRefTime = _lastSwitchTime;
    _loadRefIdle = rtos_tcbAry[RTOS_IDLE_TASK].runTime;
    rtos_port_exitCritical(cs);

    if(total == 0  ||  idle >= total)
        load = 0;
    else
        load = (uint8_t)(100u - (uint8_t)((idle * 100ul) / total));
    return load;
#else
    return 0;
#endif
}


