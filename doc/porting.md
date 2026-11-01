# Porting nornOS to a new architecture

The kernel never touches hardware. Everything it needs comes from `port/<arch>/port.h`,
which every kernel file includes through `rtos.h`. A port is three or four files plus a
make fragment; the AVR port (`port/avr/`) is the smallest complete example, the host port
(`port/host/`) shows the minimum that makes the kernel run at all, and the Xtensa port
(`port/xtensa/`) shows a CPU with register windows, a lazy FPU and a ROM loader.

## 1. What `port.h` must define

| Name | Meaning |
|---|---|
| `RTOS_PORT_MIN_STACK` | Smallest task stack the port accepts: interrupt frame plus one switch frame plus some calls. |
| `RTOS_PORT_STACK_ALIGN` | Alignment of stack frames in the ABI (1 on AVR, 16 on Xtensa). |
| `RTOS_PORT_WEAK` | Attribute for overridable functions, normally `__attribute__((weak))`. |
| `RTOS_PORT_NOINIT` | Attribute placing data where a reset does not clear it (the panic record). |
| `RTOS_PORT_STR(s)` | String literal in program memory (`PSTR` on AVR, identity elsewhere). |
| `RTOS_PORT_RUNTIME_HZ` | Frequency of the run time counter. |
| `rtos_port_ctx_t` | Saved context of a task, the first field of the TCB; a stack pointer on both ports. |
| `rtos_port_cs_t` | What `rtos_port_enterCritical` returns and `rtos_port_exitCritical` takes back. |
| `rtos_port_runtime_t` | Width of run time counts, 32 or 64 bits. |
| `RTOS_ISR(name)` | Defines the body of an interrupt handler the kernel may switch tasks after. Keep the two level macro so that `name` can itself be a macro. |
| `RTOS_PORT_HIGHEST_BIT(m)` | Optional: index of the highest set bit when the CPU has an instruction for it. |

Functions, inline or not:

| Function | Contract |
|---|---|
| `rtos_port_enterCritical` / `rtos_port_exitCritical` | Mask every interrupt that may call the kernel; nestable; the saved state is restored exactly. |
| `rtos_port_enableInterrupts` | Unmask unconditionally; called when a task starts and when the idle task begins. |
| `rtos_port_idleSleep` | Wait for the next interrupt; may be a no-op. |
| `rtos_port_halt` | Stop forever with interrupts off; `noreturn`. |
| `rtos_port_reset` | Reset the system; `noreturn`. |
| `rtos_port_getResetCause` | One of `RTOS_RESET_*` for the last reset. |
| `rtos_port_switchContext` | Called with interrupts masked, in task or interrupt context: save what the ABI says a callee preserves plus the stack pointer into `rtos_pCurrent->ctx`, set `rtos_pCurrent = rtos_pNext`, load its stack pointer and return into it. |
| `rtos_port_initStack(area, size, entry)` | Prepare a stack so that the first switch into it starts `entry` (always `rtos_taskTrampoline`) with interrupts masked; the trampoline enables them. Return the initial `ctx`. |
| `rtos_port_startTimer` | Start the tick at `RTOS_CFG_TICK_HZ`; the interrupt calls `rtos_tickFromIsr` and then the ISR tail. |
| `rtos_port_getRunTimeCounter` | Free running time at `RTOS_PORT_RUNTIME_HZ`; interrupts are masked by the caller. |
| `rtos_getTimeUs` | Microseconds since start (public API, but port implemented). |
| `rtos_port_wdtStart(ms)` / `rtos_port_wdtKick` | Hardware watchdog in reset mode, when `RTOS_CFG_WATCHDOG` is used. |

## 2. The interrupt model

Every interrupt that may wake a task runs with the kernel's interrupts masked, calls its
`RTOS_ISR` body, then `rtos_isrTail()`. The tail switches context if the body made a
higher priority task ready. The switch happens on the interrupted task's stack, so the
interrupt frame must be fully restorable after other tasks have run; nothing may live on
an interrupt stack across the tail. On the AVR the wrapper in `port.h` does this in inline
assembler around the body; on Xtensa one assembler entry (`port_context.S`) serves every
level 1 interrupt and a C dispatcher finds the handler.

A port that routes interrupts through a table exposes `rtos_port_attachInterrupt` or
similar; the kernel does not care how a body gets called.

## 3. The context switch

A solicited switch (the task blocks) only needs the registers the ABI says a callee must
preserve, because the kernel called `rtos_port_switchContext` like any other function.
Preemption needs the rest too, and those were already saved by the interrupt entry. On a
CPU with register windows every live window must be spilled to the stack before the stack
pointer changes; see the comment block in `port/xtensa/port_context.S`.

Interrupts are masked throughout. The new task continues after its own call to
`rtos_port_switchContext`, with the interrupt mask it had there, which its critical section
then restores.

## 4. Time

The tick is a periodic interrupt; `rtos_tickFromIsr` does the kernel's bookkeeping. The
run time counter must be monotonic and consistent with the tick (take both from the same
hardware timer, or extend the tick with a cycle counter, as the Xtensa port does) because
`rtos_getCpuLoad` and `rtos_getTaskRunTime` take differences of it.

## 5. The make fragment

`port/<arch>/arch.mk` maps each board of the architecture to MCU, clock and flashing
tool, sets `CC`, `CFLAGS`, `ASFLAGS`, `LDFLAGS`, `LDLIBS`, and provides the targets
`image`, `sim`, `upload` and `monitor`. One line in the top Makefile maps the board name
to the architecture. `hal/<arch>/` holds `hal_arch.h` (flash strings, delays),
`gpio_arch.h`, `uart.c` and `board.h` with the pin map and the application timer the
test applications use.

## 6. Bringing it up

1. `applications/hello` without the scheduler: startup, UART and GPIO.
2. tc01: tick, one interrupt source, semaphore from the interrupt, mutex, events.
3. tc02 and tc03: queues, mailbox, notifications, software timers.
4. tc04: the timing figures, into the README.
5. tc05: panic, reset, persistent record, watchdog.
6. tc06: floating point across preemption, if the CPU has an FPU.

The host unit tests (`make host-test`) do not involve the port and should already pass;
they are the reference for what the kernel expects.
