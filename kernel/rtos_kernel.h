#ifndef RTOS_KERNEL_INCLUDED
#define RTOS_KERNEL_INCLUDED
/**
 * @file rtos_kernel.h
 * Kernel internal interface shared between rtos_kernel.c, rtos_sync.c and the port layer.
 * Not for application use.
 */

#include "rtos.h"

/*
 * Defines
 */

/** TCB flag: event wait requires all masked events. */
#define RTOS_TF_WAIT_ALL        0x01u
/** TCB flag: task is linked into the timed list. */
#define RTOS_TF_IN_TIMED_LIST   0x02u
/** TCB flag: the last blocking call ended by timeout. */
#define RTOS_TF_TIMED_OUT       0x04u
/** TCB flag: task is blocked in rtos_notifyWait or rtos_notifyTake. */
#define RTOS_TF_WAIT_NOTIFY     0x08u
/** TCB flag: the notify wait is a take, i.e. needs a non-zero value. */
#define RTOS_TF_NOTIFY_TAKE     0x10u
/** TCB flag: a notification has been received and not yet consumed. */
#define RTOS_TF_NOTIFY_PENDING  0x20u
/** Suspended by rtos_suspend: does not run until rtos_resume. */
#define RTOS_TF_SUSPENDED       0x40u
/** Made ready while suspended: becomes ready at rtos_resume. */
#define RTOS_TF_READY_PENDING   0x80u
/** rtos_evgroupWait: clear the satisfying bits in the group when woken. */
#define RTOS_TF_EVG_CLEAR       RTOS_TF_NOTIFY_TAKE   /* reused: the two never coexist */


/*
 * Global type definitions
 */

/** The task control block. The context field must stay first: the AVR port addresses it
    with offset 0 from assembly code. */
typedef struct rtos_tcb
{
    rtos_port_ctx_t ctx;            /**< Saved stack pointer (AVR) or context handle. */
    rtos_taskFunc_t taskFunc;       /**< Entry function, NULL if the slot is unused. */
    void *arg;                      /**< Argument passed to the entry function. */
    struct rtos_tcb *pNextTimed;    /**< Link in the sorted timed wait list. */
    rtos_taskmask_t *pWaiters;      /**< Waiter mask of the object we block on, or NULL. */
    void *pXfer;                    /**< Item buffer for direct hand-over while blocked. */
    rtos_tick_t wakeAt;             /**< Absolute tick at which a timed wait ends. */
    rtos_tick_t periodBase;         /**< Last period boundary of rtos_waitPeriodic. */
    rtos_evt_t evtPending;          /**< Events posted but not yet consumed. */
    rtos_evt_t evtWait;             /**< Events awaited, 0 if not waiting for events. */
    uint16_t notifyValue;           /**< Task notification value. */
#if RTOS_CFG_RUNTIME_STATS
    rtos_runtime_t runTime;         /**< Accumulated run time, RTOS_RUNTIME_HZ. */
#endif
    uint8_t *pStackArea;            /**< Stack memory, for the high water mark. */
    size_t stackSize;               /**< Size of the stack memory. */
    const void *pWaitObj;           /**< Object waited on; see waitKind. */
    rtos_taskmask_t bit;            /**< 1 << prio, precomputed. */
    uint8_t prio;                   /**< Base priority = task id = array index. */
    uint8_t effPrio;                /**< Effective priority, raised by ceiling mutexes. */
    uint8_t flags;                  /**< RTOS_TF_* bits. */
    uint8_t cntOverrun;             /**< Saturating count of period overruns. */
    uint8_t waitKind;               /**< RTOS_WAIT_* while blocked, else RTOS_WAIT_NONE. */
} rtos_tcb_t;


/*
 * Global data declarations
 */

extern rtos_tcb_t rtos_tcbAry[RTOS_NUM_TASKS];
extern rtos_tcb_t *rtos_pCurrent;
extern rtos_tcb_t *rtos_pNext;
extern volatile uint8_t rtos_needSwitch;
extern volatile rtos_tick_t rtos_tickCount;


/*
 * Global prototypes (interrupts must be disabled when calling)
 */

void rtos_kernel_makeReady(rtos_tcb_t *pT);
void rtos_kernel_schedule(void);
bool rtos_kernel_blockCurrent(rtos_tick_t timeout);

/** Block the current task on an object: records what it waits on, then blockCurrent. */
bool rtos_kernel_blockOn(uint8_t waitKind, const void *pWaitObj, rtos_tick_t timeout);

/** Byte every task stack is filled with at creation; also the stack bottom canary. */
#define RTOS_STACK_FILL     0xA5u

/** Persistent panic record, placed by the port so that it survives a reset. */
typedef struct rtos_panicRecord
{
    uint32_t magic;
    rtos_panicInfo_t info;
    uint32_t check;
} rtos_panicRecord_t;
extern rtos_panicRecord_t rtos_panicRecord;
#define RTOS_PANIC_MAGIC    0x50414E43ul

/** Called by rtos_init: latch and clear the persistent record. */
void rtos_kernel_panicInit(void);

/** Ticks until the next timed wake up or software timer expiry; RTOS_FOREVER if none.
    Interrupts disabled. Used by tickless idle. */
rtos_tick_t rtos_kernel_ticksToNextWake(void);

/** Ticks until the first software timer expires; RTOS_FOREVER if none. */
rtos_tick_t rtos_kernel_timerTicksToNext(void);

#if RTOS_CFG_TRACE
# define RTOS_TRACE(evt, a, b)  rtos_traceHook((uint8_t)(evt), (uint32_t)(a), (uint32_t)(b))
#else
# define RTOS_TRACE(evt, a, b)  do { } while(0)
#endif

/** Idle task part of the watchdog: check the monitored tasks, kick the hardware. */
void rtos_kernel_wdtIdle(void);

/** Timer task created by rtos_start when RTOS_CFG_TIMER_TASK_PRIO is set. */
void rtos_kernel_timerTaskCreate(void);
uint8_t rtos_kernel_releaseHighestWaiter(rtos_taskmask_t *pWaiters);
void rtos_kernel_timerTick(rtos_tick_t now);

/* Read-only views for tests and debuggers; no locking, call with interrupts disabled. */
rtos_taskmask_t rtos_kernel_getReadyMask(void);
rtos_tcb_t *rtos_kernel_getTimedHead(void);

#endif  /* RTOS_KERNEL_INCLUDED */
