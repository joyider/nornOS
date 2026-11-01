#ifndef RTOS_INCLUDED
#define RTOS_INCLUDED
/**
 * @file rtos.h
 * Public API of the nornOS kernel.
 *
 * Design in one paragraph: a fixed, compile-time known set of tasks, one unique priority
 * per task (the task index is its priority, 0 is the implicit idle task, higher index means
 * higher priority), a ready bitmask scheduler, a 1 kHz tick driven sorted wake-up list,
 * per-task event flags, counting semaphores and immediate-priority-ceiling mutexes. The
 * kernel is written in C99 and talks to the hardware only through the port layer (port.h
 * plus port_context.S for AVR, port_host.c for tests on a PC).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "rtos_config.h"
#include "rtos_types.h"
#include "port.h"

/*
 * Defines
 */

/** Version string of the kernel. */
#define RTOS_VERSION            "2.0.0-dev"

/** Task id of the implicit idle task. Priority 0, always ready. */
#define RTOS_IDLE_TASK          0u

/** Number of tasks including the idle task. */
#define RTOS_NUM_TASKS          (RTOS_CFG_NUM_TASKS + 1u)

/** Pseudo task id meaning "no task". */
#define RTOS_NO_TASK            0xFFu

/** Timeout value meaning "wait forever". */
#define RTOS_FOREVER            ((rtos_tick_t)-1)

/** Timeout value meaning "do not block, just poll". */
#define RTOS_NO_WAIT            ((rtos_tick_t)0)

/** Option for rtos_waitEvents: resume when any of the masked events is pending. */
#define RTOS_WAIT_ANY           0u

/** Option for rtos_waitEvents: resume only when all masked events are pending. */
#define RTOS_WAIT_ALL           1u

/** rtos_notify operation: replace the value. */
#define RTOS_NOTIFY_SET         0u
/** rtos_notify operation: OR the value into the current one (flags). */
#define RTOS_NOTIFY_OR          1u
/** rtos_notify operation: increment the current value by one (counting). */
#define RTOS_NOTIFY_INC         2u

/** Static initializer for a counting semaphore without an upper limit. */
#define RTOS_SEM_INIT(initialCount)     { (initialCount), 0xFFu, 0, 0 }

/** Static initializer for a binary semaphore: a give while the token is available is
    lost (and counted in the lost field). */
#define RTOS_SEM_INIT_BINARY(initialCount)  { (initialCount), 1u, 0, 0 }

/** Static initializer for a counting semaphore with an upper limit. */
#define RTOS_SEM_INIT_MAX(initialCount, maxCount)  { (initialCount), (maxCount), 0, 0 }

/** Static initializer for a ceiling mutex. The ceiling is the highest priority of any task
    that will ever lock the mutex. */
#define RTOS_MUTEX_INIT(ceilingPrio)    { (ceilingPrio), RTOS_NO_TASK, 0, 0 }

/** Static initializer for a queue. \a storage is a uint8_t array of itemSize*capacity
    bytes. Capacity is at most 255. */
#define RTOS_QUEUE_INIT(storage, itemSize, capacity)                            \
    { (storage), (itemSize), (capacity), 0, 0, 0, 0, 0, 0, 0 }

/** Static initializer for a mailbox. \a storage is a uint8_t array of itemSize bytes. */
#define RTOS_MBOX_INIT(storage, itemSize)   { (storage), (itemSize), 0, 0, 0 }

/** Static initializer for a memory pool; rtos_poolInit must still be called to build the
    free list. */
#define RTOS_POOL_INIT(storage, blockSize, numBlocks)                           \
    { (storage), (blockSize), (numBlocks), 0, 0, NULL, (numBlocks), 0 }

/** Frequency of the run time counters returned by rtos_getTaskRunTime, in counts per
    second. RTOS_RUNTIME_TO_MS converts a count to milliseconds. */
#define RTOS_RUNTIME_HZ         RTOS_PORT_RUNTIME_HZ
#define RTOS_RUNTIME_TO_MS(t)   ((t) / (RTOS_RUNTIME_HZ / 1000ul))

/** Type of a run time count; width chosen by the port. */
typedef rtos_port_runtime_t rtos_runtime_t;

/** Convert milliseconds to ticks at compile time. */
#define RTOS_MS(ms)             ((rtos_tick_t)(((uint32_t)(ms) * RTOS_CFG_TICK_HZ) / 1000ul))


/*
 * Global type definitions
 */

/** The task function. It receives the argument given at creation and should never return;
    if it does, the task is silently terminated and never scheduled again. */
typedef void (*rtos_taskFunc_t)(void *arg);

/** Software timer callback. Runs in the tick interrupt with interrupts disabled: keep it
    short and use only *FromIsr kernel functions or rtos_timerStart/Stop. */
typedef void (*rtos_timerFunc_t)(void *arg);

/** A software timer. Treat as opaque; initialize with rtos_timerInit. */
typedef struct rtos_timer
{
    struct rtos_timer *pNext;   /**< Link in the sorted list of active timers. */
    struct rtos_timer *pNextPending; /**< Link in the list of expired timers waiting for
                                     the timer task (RTOS_CFG_TIMER_TASK_PRIO). */
    rtos_tick_t expiresAt;      /**< Absolute tick of the next expiry. */
    rtos_tick_t period;         /**< Reload period, 0 for a one shot timer. */
    rtos_timerFunc_t callback;  /**< Called at expiry. */
    void *arg;                  /**< Passed to the callback. */
    uint8_t active;             /**< 1 while in the list. */
    uint8_t pending;            /**< 1 while in the pending list. */
} rtos_timer_t;

/** A counting semaphore. Treat as opaque. */
typedef struct rtos_sem
{
    uint8_t count;              /**< Number of free tokens. */
    uint8_t maxCount;           /**< Upper limit of count; 1 for a binary semaphore. */
    rtos_taskmask_t waiters;    /**< Tasks blocked on this semaphore. */
    uint8_t lost;               /**< Gives that found the count at the limit (saturates). */
} rtos_sem_t;

/** A mutex using the immediate priority ceiling protocol. Treat as opaque. */
typedef struct rtos_mutex
{
    uint8_t ceiling;            /**< Priority the owner is raised to while holding it. */
    uint8_t owner;              /**< Owning task id or RTOS_NO_TASK. */
    uint8_t savedPrio;          /**< Owner's effective priority before the lock. */
    rtos_taskmask_t waiters;    /**< Fallback only: tasks blocked if ceiling was too low. */
} rtos_mutex_t;


/** A message queue: fixed size items copied by value, FIFO. Treat as opaque. */
typedef struct rtos_queue
{
    uint8_t *buf;               /**< itemSize * capacity bytes of storage. */
    uint8_t itemSize;           /**< Bytes per item. */
    uint8_t capacity;           /**< Number of items the storage holds. */
    uint8_t count;              /**< Items currently stored. */
    uint8_t head;               /**< Index of the oldest item. */
    uint8_t tail;               /**< Index where the next item is written. */
    rtos_taskmask_t readers;    /**< Tasks blocked in receive. */
    rtos_taskmask_t writers;    /**< Tasks blocked in send. */
    uint8_t maxCount;           /**< High water mark of count. */
    uint16_t dropped;           /**< Sends that failed: full and no wait, or timed out. */
} rtos_queue_t;

/** A mailbox: one slot holding the most recent item. A post overwrites an unread item.
    Treat as opaque. */
typedef struct rtos_mbox
{
    uint8_t *buf;               /**< itemSize bytes of storage. */
    uint8_t itemSize;           /**< Bytes per item. */
    uint8_t full;               /**< 1 if the slot holds an unread item. */
    rtos_taskmask_t readers;    /**< Tasks blocked in fetch. */
    uint16_t overwrites;        /**< Posts that replaced an unread item. */
} rtos_mbox_t;


/** A pool of fixed size memory blocks. Treat as opaque. */
typedef struct rtos_pool
{
    uint8_t *storage;           /**< blockSize * numBlocks bytes. */
    uint16_t blockSize;         /**< Bytes per block, at least sizeof(void *). */
    uint8_t numBlocks;          /**< Total number of blocks. */
    uint8_t numFree;            /**< Currently free blocks. */
    rtos_taskmask_t waiters;    /**< Tasks blocked in alloc. */
    void *pFree;                /**< Head of the free list. */
    uint8_t minFree;            /**< Low water mark of numFree. */
    uint16_t failed;            /**< Allocations that returned NULL. */
} rtos_pool_t;

/** Why the kernel stopped the system; the first field of rtos_panicInfo_t. */
typedef enum rtos_panicReason
{
    RTOS_PANIC_NONE = 0,
    RTOS_PANIC_ASSERT,          /**< pFile/line: the failed RTOS_ASSERT. */
    RTOS_PANIC_STACK_OVERFLOW,  /**< arg1: the task whose stack bottom was overwritten. */
    RTOS_PANIC_WATCHDOG,        /**< arg1: task that did not check in, RTOS_NO_TASK: idle. */
    RTOS_PANIC_DEADLOCK,        /**< arg1: task that would block, arg2: task it waits for. */
    RTOS_PANIC_FAULT,           /**< CPU exception; arg1: cause, arg2: address (port). */
    RTOS_PANIC_APPLICATION      /**< rtos_panic called by the application; args are its own. */
} rtos_panicReason_t;

/** What happened; filled in by rtos_panic and kept across a reset. */
typedef struct rtos_panicInfo
{
    uint8_t reason;             /**< rtos_panicReason_t. */
    uint8_t task;               /**< Task that was running. */
    uint16_t line;              /**< Source line of a failed assertion. */
    uint32_t arg1;
    uint32_t arg2;
    rtos_tick_t tick;           /**< Time of the panic. */
    const char *pFile;          /**< Source file of a failed assertion; only meaningful in
                                     the same firmware image. */
} rtos_panicInfo_t;

/** Cause of the last reset as the hardware reports it. */
#define RTOS_RESET_UNKNOWN      0u
#define RTOS_RESET_POWER_ON     1u
#define RTOS_RESET_EXTERNAL     2u
#define RTOS_RESET_BROWN_OUT    3u
#define RTOS_RESET_WATCHDOG     4u
#define RTOS_RESET_SOFTWARE     5u

/** What a task is blocked on, in rtos_taskInfo_t. */
#define RTOS_WAIT_NONE          0u
#define RTOS_WAIT_DELAY         1u
#define RTOS_WAIT_PERIODIC      2u
#define RTOS_WAIT_EVENTS        3u
#define RTOS_WAIT_SEM           4u
#define RTOS_WAIT_MUTEX         5u
#define RTOS_WAIT_QUEUE_SEND    6u
#define RTOS_WAIT_QUEUE_RECV    7u
#define RTOS_WAIT_MBOX          8u
#define RTOS_WAIT_POOL          9u
#define RTOS_WAIT_NOTIFY        10u
#define RTOS_WAIT_EVGROUP       11u

#define RTOS_TASK_NONE          0u  /**< Never created. */
#define RTOS_TASK_RUNNING       1u
#define RTOS_TASK_READY         2u
#define RTOS_TASK_BLOCKED       3u
#define RTOS_TASK_SUSPENDED     4u  /**< rtos_suspend; waitKind says what it was doing. */

/** rtos_evgroupWait option: clear the bits that satisfied the wait, atomically. */
#define RTOS_WAIT_CLEAR         0x02u

/** An event group: flags shared by any number of tasks. Treat as opaque. */
typedef struct rtos_evgroup
{
    rtos_evt_t bits;            /**< Current flags. */
    rtos_taskmask_t waiters;    /**< Tasks blocked in rtos_evgroupWait. */
} rtos_evgroup_t;

/** Static initializer for an event group. */
#define RTOS_EVGROUP_INIT(initialBits)  { (initialBits), 0 }

/** Heap statistics, see rtos_heapStats. */
typedef struct rtos_heapStats
{
    size_t total;               /**< Bytes handed to rtos_heapInit. */
    size_t free;                /**< Bytes in free blocks now. */
    size_t minFree;             /**< Lowest value of free so far. */
    size_t largestFree;         /**< Largest single free block now. */
    uint16_t allocs;            /**< Successful rtos_malloc calls. */
    uint16_t frees;             /**< rtos_free calls. */
    uint16_t failed;            /**< rtos_malloc calls that returned NULL. */
} rtos_heapStats_t;

/** Snapshot of a task for diagnostics, see rtos_getTaskInfo. */
typedef struct rtos_taskInfo
{
    uint8_t state;              /**< RTOS_TASK_*. */
    uint8_t prio;
    uint8_t effPrio;            /**< prio, or a mutex ceiling while holding it. */
    uint8_t waitKind;           /**< RTOS_WAIT_* while blocked. */
    const void *pWaitObj;       /**< The semaphore, mutex, queue, mailbox or pool. */
    bool hasTimeout;            /**< wakeAt is valid. */
    rtos_tick_t wakeAt;         /**< Tick of the timeout. */
    size_t stackSize;
    size_t stackFree;
    rtos_runtime_t runTime;
    uint8_t overruns;
} rtos_taskInfo_t;


/*
 * Global prototypes
 */

/* --- Kernel life cycle ------------------------------------------------------------- */

/** Reset all kernel data. Call once before creating tasks. */
void rtos_init(void);

/** Register a task. prio is 1..RTOS_CFG_NUM_TASKS and must be unique. The stack area must
    live as long as the system runs. */
void rtos_createTask( uint8_t prio
                    , rtos_taskFunc_t taskFunc
                    , void *arg
                    , uint8_t *stackArea
                    , size_t stackSize
                    );

/** Start the tick timer and the scheduler. Never returns; the calling context becomes
    the idle task. */
void rtos_start(void);

/* --- Time ---------------------------------------------------------------------------- */

/** Current tick count since rtos_start. */
rtos_tick_t rtos_getTick(void);

/** Block the calling task for at least \a ticks ticks (the real delay is ticks..ticks+1). */
void rtos_delay(rtos_tick_t ticks);

/** Block the calling task until the next period boundary. The boundary is computed from the
    previous boundary (or the task start) plus \a period, so the task runs drift free
    regardless of its execution time. Returns true if the boundary was already in the past
    (task overrun). In that case the task continues immediately and the next boundary is
    re-based on now. */
bool rtos_waitPeriodic(rtos_tick_t period);

/* --- Event flags --------------------------------------------------------------------- */

/** Wait until the events in \a mask are pending for the calling task (any or all of them
    depending on \a opt), or until \a timeout ticks elapse. The satisfying events are
    consumed and returned; 0 means timeout. RTOS_NO_WAIT polls, RTOS_FOREVER never times
    out. */
rtos_evt_t rtos_waitEvents(rtos_evt_t mask, uint8_t opt, rtos_tick_t timeout);

/** Post events to a task. May resume it and cause an immediate task switch. */
void rtos_postEvents(uint8_t taskId, rtos_evt_t events);

/** Post events to a task from inside an RTOS_ISR body. */
void rtos_postEventsFromIsr(uint8_t taskId, rtos_evt_t events);

/** Post events to all tasks. */
void rtos_broadcastEvents(rtos_evt_t events);

/* --- Counting semaphore ---------------------------------------------------------------- */

/** Initialize at run time, as an alternative to RTOS_SEM_INIT. */
void rtos_semInit(rtos_sem_t *pSem, uint8_t initialCount);

/** Initialize with an upper limit; maxCount 1 gives a binary semaphore. */
void rtos_semInitMax(rtos_sem_t *pSem, uint8_t initialCount, uint8_t maxCount);

/** Take one token, blocking up to \a timeout ticks. Returns false on timeout. */
bool rtos_semTake(rtos_sem_t *pSem, rtos_tick_t timeout);

/** Give one token. If a task waits for it the highest priority waiter gets it directly. */
void rtos_semGive(rtos_sem_t *pSem);

/** Give one token from inside an RTOS_ISR body. */
void rtos_semGiveFromIsr(rtos_sem_t *pSem);

/* --- Ceiling mutex ------------------------------------------------------------------- */

void rtos_mutexInit(rtos_mutex_t *pMutex, uint8_t ceilingPrio);

/** Lock the mutex. With a correct ceiling this never blocks: while a task holds the mutex
    no task of priority <= ceiling can preempt it. Not recursive. Unlock in LIFO order when
    holding several mutexes. */
void rtos_mutexLock(rtos_mutex_t *pMutex);

void rtos_mutexUnlock(rtos_mutex_t *pMutex);

/* --- Message queue ------------------------------------------------------------------ */

void rtos_queueInit(rtos_queue_t *pQ, uint8_t *storage, uint8_t itemSize, uint8_t capacity);

/** Copy an item into the queue. Blocks up to \a timeout ticks if the queue is full.
    If a task is blocked in receive the item goes straight to it. Returns false on
    timeout. */
bool rtos_queueSend(rtos_queue_t *pQ, const void *pItem, rtos_tick_t timeout);

/** Send from inside an RTOS_ISR body. Never blocks; returns false if the queue is full. */
bool rtos_queueSendFromIsr(rtos_queue_t *pQ, const void *pItem);

/** Copy the oldest item out of the queue, blocking up to \a timeout ticks if it is empty.
    Returns false on timeout. */
bool rtos_queueReceive(rtos_queue_t *pQ, void *pItem, rtos_tick_t timeout);

/** Receive from inside an RTOS_ISR body. Never blocks; returns false if empty. */
bool rtos_queueReceiveFromIsr(rtos_queue_t *pQ, void *pItem);

/** Number of items currently in the queue. */
uint8_t rtos_queueCount(const rtos_queue_t *pQ);

/* --- Mailbox ----------------------------------------------------------------------------- */

void rtos_mboxInit(rtos_mbox_t *pMb, uint8_t *storage, uint8_t itemSize);

/** Put an item in the mailbox, replacing any unread one. Never blocks. If a task is
    blocked in fetch the item goes straight to it. Returns true if an unread item was
    overwritten. */
bool rtos_mboxPost(rtos_mbox_t *pMb, const void *pItem);

/** Post from inside an RTOS_ISR body. */
bool rtos_mboxPostFromIsr(rtos_mbox_t *pMb, const void *pItem);

/** Take the item out of the mailbox, blocking up to \a timeout ticks if it is empty.
    Returns false on timeout. */
bool rtos_mboxFetch(rtos_mbox_t *pMb, void *pItem, rtos_tick_t timeout);

/** True if the mailbox holds an unread item. */
bool rtos_mboxIsFull(const rtos_mbox_t *pMb);

/* --- Task notification ----------------------------------------------------------------- */

/** Apply \a op with \a value to the notification value of a task and wake it if it is
    waiting. May cause an immediate task switch. */
void rtos_notify(uint8_t taskId, uint16_t value, uint8_t op);

/** As rtos_notify, from inside an RTOS_ISR body. */
void rtos_notifyFromIsr(uint8_t taskId, uint16_t value, uint8_t op);

/** Wait until the calling task has been notified, then return the value and reset it to
    0. \a pValue may be NULL. Returns false on timeout. */
bool rtos_notifyWait(uint16_t *pValue, rtos_tick_t timeout);

/** Wait until the calling task's notification value is non-zero, then decrement it.
    Returns false on timeout. Use with RTOS_NOTIFY_INC for a semaphore without an object. */
bool rtos_notifyTake(rtos_tick_t timeout);

/* --- Software timers ------------------------------------------------------------------ */

void rtos_timerInit(rtos_timer_t *pT, rtos_timerFunc_t callback, void *arg);

/** Arm the timer to fire after \a delay ticks and then every \a period ticks (0 for one
    shot). Restarts a running timer. Usable from tasks and from ISR bodies. */
void rtos_timerStart(rtos_timer_t *pT, rtos_tick_t delay, rtos_tick_t period);

void rtos_timerStop(rtos_timer_t *pT);

bool rtos_timerIsActive(const rtos_timer_t *pT);

/** Ticks until the next expiry, 0 if not active or due. */
rtos_tick_t rtos_timerRemaining(const rtos_timer_t *pT);

/* --- Memory pool ------------------------------------------------------------------- */

/** Build the free list over \a storage, which must hold blockSize*numBlocks bytes. */
void rtos_poolInit(rtos_pool_t *pPool, uint8_t *storage, uint16_t blockSize, uint8_t numBlocks);

/** Get a block, blocking up to \a timeout ticks if none is free. NULL on timeout. */
void *rtos_poolAlloc(rtos_pool_t *pPool, rtos_tick_t timeout);

/** Get a block from inside an RTOS_ISR body. NULL if none is free. */
void *rtos_poolAllocFromIsr(rtos_pool_t *pPool);

/** Return a block. If a task waits for one it gets this block directly. */
void rtos_poolFree(rtos_pool_t *pPool, void *pBlock);

void rtos_poolFreeFromIsr(rtos_pool_t *pPool, void *pBlock);

uint8_t rtos_poolFreeCount(const rtos_pool_t *pPool);

/* --- Event groups (RTOS_CFG_EVGROUPS) ---------------------------------------------- */

void rtos_evgroupInit(rtos_evgroup_t *pG, rtos_evt_t initialBits);

/** Set bits; every waiter whose condition is now met is woken, highest priority first.
    A waiter with RTOS_WAIT_CLEAR takes its bits out of the group as it is woken. */
void rtos_evgroupSet(rtos_evgroup_t *pG, rtos_evt_t bits);
void rtos_evgroupSetFromIsr(rtos_evgroup_t *pG, rtos_evt_t bits);

/** Clear bits; nobody is woken. */
void rtos_evgroupClear(rtos_evgroup_t *pG, rtos_evt_t bits);

rtos_evt_t rtos_evgroupGet(const rtos_evgroup_t *pG);

/** Wait for any (RTOS_WAIT_ANY) or all (RTOS_WAIT_ALL) bits of \a mask, optionally
    clearing them on success (| RTOS_WAIT_CLEAR). Returns the bits that satisfied the
    wait, 0 on timeout. */
rtos_evt_t rtos_evgroupWait(rtos_evgroup_t *pG, rtos_evt_t mask, uint8_t options, rtos_tick_t timeout);

/* --- Suspend and resume (RTOS_CFG_SUSPEND) ------------------------------------------ */

/** Keep a task from running until rtos_resume. A blocked task stays in its wait; a wake
    up that arrives meanwhile is kept and takes effect at resume. A task may suspend
    itself. Not the idle task. */
void rtos_suspend(uint8_t taskId);

/** Let a suspended task run again (if it is not still blocked). */
void rtos_resume(uint8_t taskId);
void rtos_resumeFromIsr(uint8_t taskId);

/* --- Heap (RTOS_CFG_HEAP) ----------------------------------------------------------- */

/** Give the allocator its memory. Call once before the first rtos_malloc. */
void rtos_heapInit(void *area, size_t size);

/** First fit allocation with coalescing on free; NULL if nothing fits. Task context. */
void *rtos_malloc(size_t size);
void rtos_free(void *p);

void rtos_heapStats(rtos_heapStats_t *pStats);

/* --- Diagnostics -------------------------------------------------------------------- */

/** Id (= priority) of the calling task. */
uint8_t rtos_getCurrentTask(void);

/** Number of detected period overruns of a task; optionally reset. */
uint8_t rtos_getOverrunCount(uint8_t taskId, bool reset);

/** Number of never touched bytes at the bottom of a task's stack. Requires
    RTOS_CFG_STACK_CHECK. Returns 0 for the idle task. */
size_t rtos_getStackFree(uint8_t taskId);

/** Microseconds since start, derived from the tick counter and the timer register. */
uint32_t rtos_getTimeUs(void);

/** Accumulated run time of a task in counts of RTOS_RUNTIME_HZ, measured at every
    context switch. Task 0 is the idle task. Requires RTOS_CFG_RUNTIME_STATS. */
rtos_runtime_t rtos_getTaskRunTime(uint8_t taskId);

/** CPU load in percent over the interval since the previous call (since start for the
    first call): the share of time not spent in the idle task. Requires
    RTOS_CFG_RUNTIME_STATS. */
uint8_t rtos_getCpuLoad(void);

/** Snapshot of a task. Returns false for an id out of range. */
bool rtos_getTaskInfo(uint8_t taskId, rtos_taskInfo_t *pInfo);

/* --- Panic ----------------------------------------------------------------------------- */

/** Stop the system: record the reason, call rtos_panicHook, then halt or reset according
    to RTOS_CFG_PANIC_ACTION. Applications use RTOS_PANIC_APPLICATION with their own
    arguments. May be called from any context. */
void rtos_panic(uint8_t reason, uint32_t arg1, uint32_t arg2) __attribute__((noreturn));

/** The record a panic left before the last reset, if any. Valid once per boot; copied out
    of the persistent storage by rtos_init. Returns false if there is none. */
bool rtos_getLastPanic(rtos_panicInfo_t *pInfo);

/** RTOS_RESET_* cause of the last reset. */
uint8_t rtos_getResetCause(void);

/* --- Hooks ----------------------------------------------------------------------------- */

/** Called by the kernel when an assertion fails. The default calls rtos_panic. */
void rtos_assertFailed(const char *file, uint16_t line);

/** Called by rtos_panic with interrupts disabled before the halt or reset. The weak
    default does nothing; applications print the information here. Must not block. */
void rtos_panicHook(const rtos_panicInfo_t *pInfo);

/* --- Watchdog (RTOS_CFG_WATCHDOG) --------------------------------------------------- */

/** Start the hardware watchdog with \a timeoutMs (rounded up to what the hardware
    offers). The idle task kicks it, but only while every monitored task checks in on
    time; a starved idle task (runaway task or interrupt storm) therefore resets the
    system without a panic record, a late task causes RTOS_PANIC_WATCHDOG. */
void rtos_wdtInit(uint32_t timeoutMs);

/** Require \a task to call rtos_wdtCheckIn at least every \a maxInterval ticks;
    0 stops monitoring it. The first deadline is maxInterval from now. */
void rtos_wdtMonitor(uint8_t task, rtos_tick_t maxInterval);

/** The calling task is alive; resets its deadline. */
void rtos_wdtCheckIn(void);

/* --- Trace (RTOS_CFG_TRACE) --------------------------------------------------------- */

#define RTOS_TRACE_SWITCH   1u  /**< a: task leaving, b: task entering. */
#define RTOS_TRACE_BLOCK    2u  /**< a: task, b: RTOS_WAIT_* kind. */
#define RTOS_TRACE_READY    3u  /**< a: task made ready, b: RTOS_WAIT_* it was in. */
#define RTOS_TRACE_TIMER    4u  /**< a: timer address, b: 0 in ISR context, 1 in task. */
#define RTOS_TRACE_PANIC    5u  /**< a: reason, b: arg1. */

/** Provided by the application when RTOS_CFG_TRACE is on. Called with interrupts
    disabled from inside the kernel; must be short and must not call the kernel. See
    hal/trace.h for a ring buffer recorder. */
void rtos_traceHook(uint8_t event, uint32_t a, uint32_t b);

/* --- Called from the port layer, not by applications -------------------------------------- */

void rtos_tickFromIsr(void);
void rtos_isrTail(void);
void rtos_taskTrampoline(void);

#endif  /* RTOS_INCLUDED */
