# nornOS design

A small real time kernel for microcontrollers: ATmega328P/2560 and ESP32 today. This
document records how it works and why, so that the choices can be revisited knowingly.

## Scheduling

Fixed priority, preemptive, one task per priority. The task id is its priority, 1 to
`RTOS_CFG_NUM_TASKS`; 0 is the idle task, which is the context that called `rtos_start`.
The scheduler is a bitmask of ready tasks and a highest-bit lookup: O(1), four to twenty
cycles depending on the CPU. There is no time slicing, deliberately: two tasks at the same
priority would need a per priority list and a quantum, and make the scheduling order
depend on history. A task runs until it blocks or a higher priority task becomes ready.

A task becomes ready in three ways: a timeout (the tick interrupt walks the head of a
list sorted by wake-up tick, so a tick costs the same whether 2 or 30 tasks sleep), an
object hand-over (semaphore token, queue item, mailbox item, pool block, event group bits,
written straight into the waiting task's variables by the giver, so the woken task finds
its result without touching the object again), or a notification or event posted to it.

Context switches happen in two places only: in task context when the running task blocks
or makes a higher task ready, and at the end of an interrupt handler (`rtos_isrTail`) when
the handler did. An interrupt handler therefore never switches in the middle; the port
guarantees the interrupt frame survives other tasks running in between.

## Mutual exclusion

The mutex follows the immediate priority ceiling protocol: locking raises the owner to
the mutex's ceiling at once, so no task that could ever want the mutex runs while it is
held, and the owner can never be preempted by a contender. With correct ceilings nobody
blocks on a mutex, a chain of mutexes cannot deadlock, and the worst case blocking time of
a task is one critical section of a lower task. If a ceiling is too low, or the owner
blocks while holding the mutex (allowed but discouraged), contenders fall back to waiting
and get the mutex handed over in priority order; the hand-over raises the new owner
immediately. A lock that would wait follows the chain of owners and panics on a cycle.

Critical sections are short interrupt masks with the previous state restored, nestable.
There is no kernel lock besides them; every kernel operation runs with interrupts masked
for a bounded number of instructions, which is what gives the interrupt latency figures
in the README.

## Time

A fixed 1 kHz tick (configurable) drives delays, timeouts, periodic tasks and software
timers. `rtos_waitPeriodic` keeps a period base per task so periods do not drift; a
missed period is counted as an overrun and re-based rather than replayed. Software timers
are a sorted list served by the tick; their callbacks run in the tick interrupt unless the
timer task is configured, in which case they run in a kernel task of chosen priority.

Tickless idle (`RTOS_CFG_TICKLESS`) lets the idle task move the tick timer out to the next
deadline and sleep; the missed ticks are counted in one go on wake-up. Only the ESP32 port
implements it; the AVR ports sleep one tick at a time.

Run time statistics are taken at every context switch from a free running counter, so the
per task run times always add up to the elapsed time and `rtos_getCpuLoad` is exact.

## Memory

Everything is static: tasks, stacks and objects are declared by the application and
registered with the kernel; nothing is allocated behind the scenes. The memory pool gives
fixed blocks with O(1) alloc and free; the heap (`RTOS_CFG_HEAP`, off by default) is a
first fit allocator with coalescing for the cases that need one, deterministic in that it
does only what the caller asked and reports its statistics.

## Failure

One path: `rtos_panic`. Assertions, stack overflow (canary at the bottom of every stack,
checked at every switch and tick), the task watchdog, deadlock detection and CPU
exceptions all end there. The record survives a reset, the application's hook can print
or log before the halt or reset, and the next boot reads the record and the hardware's
reset cause. Queues, mailboxes, pools and semaphores count what they drop or overwrite.

## Ports

The kernel talks to hardware through `port.h` only (doc/porting.md). The AVR port is the
simplest: the ISR wrapper and the switch save what the ABI requires and nothing else. The
Xtensa port adds the register window spill, a lazy FPU context, the ROM loader image
format, the PLL and a second kernel instance on the other core (AMP) with a shared
memory link. The host port runs the kernel on Linux ucontext for deterministic unit tests
with simulated interrupts, which is where every primitive is specified by its trace.

## What is deliberately absent

Time slicing (see above), dynamic task creation and deletion (static systems do not need
them, and deletion is the source of most use-after-free bugs in kernels), priority
inheritance (the ceiling protocol is simpler and has better bounds), SMP (the two ESP32
cores run two independent kernels instead: no locks in the scheduler, no cache
coherence questions, and a message link that is as fast as a shared memory copy).
