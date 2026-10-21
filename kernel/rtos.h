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

/** Static initializer for a counting semaphore. */
#define RTOS_SEM_INIT(initialCount)     { (initialCount), 0 }

/** Static initializer for a ceiling mutex. The ceiling is the highest priority of any task
    that will ever lock the mutex. */
#define RTOS_MUTEX_INIT(ceilingPrio)    { (ceilingPrio), RTOS_NO_TASK, 0, 0 }

/** Static initializer for a queue. \a storage is a uint8_t array of itemSize*capacity
    bytes. Capacity is at most 255. */
#define RTOS_QUEUE_INIT(storage, itemSize, capacity)                            \
    { (storage), (itemSize), (capacity), 0, 0, 0, 0, 0 }

/** Static initializer for a mailbox. \a storage is a uint8_t array of itemSize bytes. */
#define RTOS_MBOX_INIT(storage, itemSize)   { (storage), (itemSize), 0, 0 }

/** Static initializer for a memory pool; rtos_poolInit must still be called to build the
    free list. */
#define RTOS_POOL_INIT(storage, blockSize, numBlocks)                           \
    { (storage), (blockSize), (numBlocks), 0, 0, NULL }

/** Unit of the run time counters returned by rtos_getTaskRunTime, in microseconds. */
#define RTOS_RUNTIME_UNIT_US    RTOS_PORT_RUNTIME_UNIT_US

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
    rtos_tick_t expiresAt;      /**< Absolute tick of the next expiry. */
    rtos_tick_t period;         /**< Reload period, 0 for a one shot timer. */
    rtos_timerFunc_t callback;  /**< Called at expiry. */
    void *arg;                  /**< Passed to the callback. */
    uint8_t active;             /**< 1 while in the list. */
} rtos_timer_t;

/** A counting semaphore. Treat as opaque. */
typedef struct rtos_sem
{
    uint8_t count;              /**< Number of free tokens. */
    rtos_taskmask_t waiters;    /**< Tasks blocked on this semaphore. */
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
} rtos_queue_t;

/** A mailbox: one slot holding the most recent item. A post overwrites an unread item.
    Treat as opaque. */
typedef struct rtos_mbox
{
    uint8_t *buf;               /**< itemSize bytes of storage. */
    uint8_t itemSize;           /**< Bytes per item. */
    uint8_t full;               /**< 1 if the slot holds an unread item. */
    rtos_taskmask_t readers;    /**< Tasks blocked in fetch. */
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
} rtos_pool_t;


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
                    , uint16_t stackSize
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

void rtos_semInit(rtos_sem_t *pSem, uint8_t initialCount);

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

/* --- Diagnostics -------------------------------------------------------------------- */

/** Id (= priority) of the calling task. */
uint8_t rtos_getCurrentTask(void);

/** Number of detected period overruns of a task; optionally reset. */
uint8_t rtos_getOverrunCount(uint8_t taskId, bool reset);

/** Number of never touched bytes at the bottom of a task's stack. Requires
    RTOS_CFG_STACK_CHECK. Returns 0 for the idle task. */
uint16_t rtos_getStackFree(uint8_t taskId);

/** Microseconds since start, derived from the tick counter and the timer register. */
uint32_t rtos_getTimeUs(void);

/** Accumulated run time of a task in units of RTOS_RUNTIME_UNIT_US, measured at every
    context switch. Task 0 is the idle task. Requires RTOS_CFG_RUNTIME_STATS. */
uint32_t rtos_getTaskRunTime(uint8_t taskId);

/** CPU load in percent over the interval since the previous call (since start for the
    first call): the share of time not spent in the idle task. Requires
    RTOS_CFG_RUNTIME_STATS. */
uint8_t rtos_getCpuLoad(void);

/* --- Hooks ----------------------------------------------------------------------------- */

/** Called by the kernel when an assertion fails. Weak default halts the CPU. */
void rtos_assertFailed(const char *file, uint16_t line);

/* --- Called from the port layer, not by applications -------------------------------------- */

void rtos_tickFromIsr(void);
void rtos_isrTail(void);
void rtos_taskTrampoline(void);

#endif  /* RTOS_INCLUDED */
