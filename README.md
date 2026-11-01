# nornOS

A small preemptive real time kernel for the ATmega2560 (Arduino Mega) and the ATmega328P
(Arduino UNO), written in C99 with the context switch in assembler. No dependency on the Arduino core; only avr-libc. Grew out
of the bTool brewing controller, whose application lives in applications/bTool.

## Layout

    kernel/        portable kernel, C99, no architecture knowledge: scheduler, time, events
                   (rtos_kernel.c), semaphore and mutex (rtos_sync.c), queue and mailbox
                   (rtos_queue.c), task notification (rtos_notify.c), software timers
                   (rtos_timer.c), memory pool (rtos_pool.c), public API (rtos.h)
    port/<arch>/   everything the kernel needs from the CPU: port.h (critical sections,
                   ISR entry, port types and constants), context switch in assembler,
                   tick timer, stack preparation, and arch.mk with the toolchain, the
                   boards of that architecture and the image/sim/upload rules
    port/avr/      ATmega2560 and ATmega328P
    port/xtensa/   ESP32 (Xtensa LX6): port_start.S (entry), port_vectors.S (vector table
                   with the window handlers), port_context.S (context switch, interrupt
                   entry), port_esp32.c (interrupt matrix, CCOMPARE0 tick, time base),
                   esp32.ld (ROM loadable image, no bootloader)
    port/host/     Linux port on ucontext, used by the unit tests only
    hal/           drivers with one API for all architectures: uart.h, gpio.h, delay.h,
                   hd44780 (LCD), dht11; hal/<arch>/ holds hal_arch.h (flash strings,
                   delays), gpio_arch.h, uart.c and board.h (pin map per board)
    config/        rtos_config.template.h, copy to the application as rtos_config.h
    test/          host unit tests of the kernel, one program per test_*.c
    applications/<app>/  one folder per application: rtos_config.h and app.mk (BOARD)

Adding an architecture means a new port/<arch>/ and hal/<arch>/ plus one line in the
Makefile mapping its boards to the architecture. The kernel and the generic HAL are not
touched; doc/porting.md describes the port interface.

## Build

    make APP=tc01             ELF/HEX in build/tc01-<board>, prints memory use
    make APP=tc01 BOARD=uno   build for another board than app.mk says
    make APP=tc01 sim         run in simavr, UART0 output on the terminal
    make APP=tc01 upload PORT=/dev/ttyACM0
    make host-test            kernel unit tests on the PC, with ASan and UBSan
    make test-all             host tests, every tc in simavr for uno and mega2560, and on
                              an ESP32 at /dev/ttyUSB0 if present (tools/test_all.sh);
                              AVR_HW="mega2560:/dev/ttyACM0" adds a real AVR board

## Boards

    mega2560   ATmega2560, 16 MHz, 8 KB RAM. Tick on timer 3 by default (1, 4, 5 possible).
    uno        ATmega328P, 16 MHz, 2 KB RAM. Tick on timer 1, the only 16 bit timer.

The application picks its board in app.mk. Pins are written as BOARD_PIN_Dn from
hal/avr/board.h, which maps the Arduino numbering to the ports of the selected MCU. Timer
0 is never touched; timer 2 is left to the application (board_timer2StartCtc). The port
layer needs nothing per board: RAMPZ and the 3 byte return address are selected by the
compiler's MCU macros.

    esp32      ESP32-D0WDQ6, dual core Xtensa LX6, 520 KB RAM, 4 MB flash. NodeMCU-32S
               style devkit with CP2102 on /dev/ttyUSB0, LED on GPIO2. CPU_MHZ=160 by
               default from the PLL (port_clock.c); 80 and 40 work, 240 is experimental.

The ESP32 image is loaded by the ROM directly into IRAM/DRAM from flash offset 0x1000; no
second stage bootloader, no ESP-IDF, no FreeRTOS. The kernel runs on the PRO CPU; the APP
CPU is held in reset until a second nornOS instance gets a job there. Peripheral
interrupts are routed with rtos_port_attachInterrupt to level 1 CPU interrupts and
dispatched to RTOS_ISR bodies. A context switch spills every live register window first
(see port_context.S), so the kernel keeps the same model on both architectures: switch in
task context when blocking, or at the end of an interrupt. tc01, tc02 and tc03 run on the
board with the same counters as on the AVR; stacks need about 400 bytes more than on the
AVR because of the interrupt frame and the window spill areas. Toolchain: Espressif xtensa-esp-elf (crosstool-NG)
under ~/dev/tools, esptool in a venv there; both paths can be overridden with XTENSA_ROOT
and ESP_PYTHON. `make APP=hello upload` flashes, `make APP=hello monitor` resets the board
and shows UART0 (tools/monitor.py, Ctrl-C to leave). `make APP=tc01 BOARD=esp32 sim` runs
the image in Espressif's QEMU (QEMU_ESP, tools/qemu_run.py); its timers drift about a
percent against the CPU clock, so test builds for it use
EXTRA_CFLAGS=-DTC_TOLERANCE_PERMILLE=30. The single precision FPU is switched lazily
between tasks: only tasks that use floating point pay for it, and interrupt handlers must
not use it (see port/xtensa/port.h).

Both cores: an application can run a second nornOS image on the APP CPU (AMP, one kernel
instance per core, nothing shared but an 8 KB window). `AMP_CORE1 = <dir>` in app.mk names
a sub-application built with the core 1 memory layout (port/xtensa/mem_core1.ld) and
embedded in the core 0 image, which copies it into place and starts the APP CPU at boot
(rtos_port_startAppCpu). hal/xtensa/amp.c is the link: one ring buffer per direction in
the shared window, 16 byte messages, the receiver woken by the other core's FROM_CPU
interrupt. tc07 shows it: 200 requests per second answered by core 1, 10 us round trip.

## Model

* One task per priority. Task id = priority, 1..RTOS_CFG_NUM_TASKS, higher runs first.
  Task 0 is the implicit idle task (the context that called rtos_start).
* 1 kHz tick from a 16 bit timer in CTC mode (RTOS_CFG_TIMER, default timer 3). 32 bit tick
  counter. rtos_waitPeriodic gives drift free periodic tasks with overrun detection.
* Blocking primitives: rtos_delay, rtos_waitPeriodic, rtos_waitEvents (per task event
  flags, any/all, timeout), rtos_semTake (counting semaphore), rtos_mutexLock,
  rtos_queueSend/Receive (FIFO of fixed size items, copied by value) and rtos_mboxFetch
  (one slot holding the latest item, a post overwrites).
* Queue and mailbox hand items directly to a blocked task, so a woken task never has to
  re-check or loop and no third task can take the item in between.
* Task notification: one 16 bit value per task, set/OR/increment from any task or ISR,
  rtos_notifyWait takes the value, rtos_notifyTake decrements it (semaphore without an
  object). Cheapest way to signal a task from an interrupt.
* Memory pool: fixed size blocks with an O(1) free list; a blocking allocation gets a
  released block handed over directly.
* Run time statistics: at every context switch the tick timer is read and the elapsed
  time charged to the task that ran. rtos_getTaskRunTime gives the per task total,
  rtos_getCpuLoad the non-idle share since the previous call. Costs about 60 cycles per
  switch; disable with RTOS_CFG_RUNTIME_STATS 0.
* Software timers: static rtos_timer_t objects, one shot or periodic without drift, in a
  list sorted by expiry. The callback runs in the tick interrupt with interrupts disabled,
  so keep it short and use *FromIsr functions or rtos_timerStart/Stop only.
* Mutexes use the immediate priority ceiling protocol: the owner runs at the ceiling, so
  with a correct ceiling nobody ever blocks on the mutex and deadlock is impossible.
* Interrupts that talk to the kernel are written with RTOS_ISR(vector) { ... } and use
  the *FromIsr functions; a task switch, if needed, happens once at the end of the ISR.
* Critical sections are short cli/sei pairs with the previous state saved (nestable).

## Primitives

    rtos_delay, rtos_waitPeriodic      time; periodic tasks with overrun detection
    events                             per task flags, wait any/all with timeout
    event groups                       flags shared by tasks, wait any/all, clear on exit
    notifications                      one 16 bit value per task: set, or, count
    semaphore                          counting, with an upper limit; binary as a case of it
    mutex                              immediate priority ceiling, deadlock detection
    queue, mailbox                     copies of fixed size items; the mailbox overwrites
    memory pool                        fixed blocks, O(1), blocking allocation
    heap                               first fit with coalescing, optional
    software timers                    one shot and periodic, in the tick or in a task
    suspend / resume                   take a task out of scheduling and back

doc/design.md explains the model and what was left out on purpose.

## Robustness

What the kernel does when things go wrong, all optional per configuration:

    rtos_panic         the one way to stop: records reason, task, arguments and tick in
                       memory that survives a reset, calls rtos_panicHook (print, log),
                       then halts or resets (RTOS_CFG_PANIC_ACTION). After the reset
                       rtos_getLastPanic hands the record back and rtos_getResetCause
                       says whether the hardware saw a software, watchdog, brown-out,
                       external or power-on reset. Assertions go through it.
    stack canary       RTOS_CFG_STACK_CHECK 2: the bottom of every stack is checked at
                       each context switch and tick; overflow = RTOS_PANIC_STACK_OVERFLOW
                       with the task id, long before the corruption reaches a neighbour.
    watchdog           RTOS_CFG_WATCHDOG: hardware watchdog fed by the idle task, plus
                       rtos_wdtMonitor/rtos_wdtCheckIn deadlines per task. A late task is
                       reported by name (RTOS_PANIC_WATCHDOG) while the system still runs;
                       a starved idle task (runaway task, interrupt storm) ends in a
                       hardware reset that the next boot sees as RTOS_RESET_WATCHDOG.
    deadlock           a mutex lock that would wait follows the chain of owners; if it
                       leads back to the caller: RTOS_PANIC_DEADLOCK with both tasks.
    task inspection    rtos_getTaskInfo: state, what the task waits on and with which
                       timeout, priorities, stack, run time, overruns.
    object statistics  queues count drops and their high water mark, mailboxes their
                       overwrites, pools their low water mark and failed allocations,
                       semaphores the gives lost at the limit (RTOS_SEM_INIT_BINARY).
    timer task         RTOS_CFG_TIMER_TASK_PRIO: software timer callbacks in a kernel
                       task instead of the tick interrupt, so they may use the whole API.
    trace              RTOS_CFG_TRACE: rtos_traceHook at switches, blocks, wake-ups, timer
                       callbacks and panics; hal/trace.c keeps a ring buffer with time
                       stamps that rtos_panicHook can dump.

AVR note: a watchdog reset is only survivable with a bootloader that disables the watchdog
at start. The original Mega 2560 bootloader does not, and the board then resets in a loop
until power is cycled. rtos_port_reset therefore jumps to the reset vector by default
(RTOS_CFG_AVR_RESET_VIA_WDT 0), which does not pass the bootloader, and tc05 skips its
hardware watchdog stage on such a port. Should a board get stuck anyway: unplug it, and
flash within the first second after plugging it in again.

## Testing

Three layers, all run by `make test-all` (about a minute with an ESP32 connected):

    test/test_*.c      deterministic unit tests on the host port: every primitive is driven
                       through a scripted sequence of calls, simulated ticks and simulated
                       interrupts, and the trace of (task, event, value) is compared with
                       the expected one. Built with -Wconversion, ASan and UBSan.
    test/test_robust.c robustness features on the host port, one child process per
                       scenario: deadlock, stack canary, task watchdog, timer task,
                       statistics and binary semaphore, task info, trace, assertion
    test/test_stress.c randomized stress test on the host port: six tasks pick random
                       operations on every primitive with random timeouts, the idle hook
                       injects random interrupts, and the kernel invariants are checked
                       after every operation and interrupt (ready mask, timed list, waiter
                       masks, ceiling priorities, pool and queue accounting, run time sum).
                       Queue items and pool blocks carry stamps that must survive. The
                       sequence is fixed by SEED, so a failure reproduces.
    applications/tc*   on the target: each test case checks its own counters at the end
                       and prints RESULT tcNN PASS or FAIL (hal/testresult.h), then halts,
                       which ends a simavr run. tools/test_all.sh collects the lines.

tc01..tc06 pass on a real Mega 2560 with the same counters as simavr, on the ESP32 board
and in QEMU. Not covered yet: a real UNO, and static analysis beyond the compiler warnings
(cppcheck is not installed here). .github/workflows/ci.yml runs the host tests, simavr
and QEMU on every push. doc/porting.md describes the port interface.

## Timing (tc04)

Measured by applications/tc04 with everything else in the test running at the same time.
switch = one context switch including the semaphore give/take around it; isrLatency =
hardware event to the first line of the RTOS_ISR body; jitter = how late a 10 ms
rtos_waitPeriodic task wakes relative to the ideal grid. simavr is cycle accurate for the
AVR; the ESP32 figures are from the board on the 40 MHz crystal.

    board      switch avg   isrLatency max   jitter max   notes
    uno        28 us        24 us            28 us        simavr, 4 us counter resolution
    mega2560   32 us        16 us            20 us        simavr, 8 us ISR counter resolution
    mega2560   32 us        24 us            24 us        real board
    esp32      10.9 us      12 us            5 us         40 MHz crystal
    esp32      2.9 us       4 us             2 us         160 MHz (default)
    esp32      1.9 us       2 us             0 us         240 MHz (experimental)

## Applications (applications/)

    hello  bring-up without the scheduler: banner, one line per second, LED, UART echo
    tc01   periodic task, events, semaphore from an ISR, ceiling mutex, stack check
    tc04   timing: context switch, interrupt latency, periodic jitter
    tc05   panic, reset and recovery: application panic, stack overflow, late task,
           starved idle; the stage survives each reset and the record is checked
    tc06   floating point kept in registers across preemption, lazy FPU on the ESP32
    tc07   ESP32 only: a second nornOS instance on the APP CPU answering requests over
           the shared memory link (ONLY_BOARDS in app.mk keeps it off the AVR boards)
    tc08   heap, event groups, suspend/resume and tickless idle together
           (tc01..tc04 use the application timer of hal/<arch>/board.h and run on every board)
    tc02   queue fed from an ISR with deliberate overflow, mailbox between tasks
    tc03   task notifications from ISR and timers, one shot and periodic software timers
    bTool  the brewing controller: DHT11 sensor, HD44780 LCD, thermostat relay, console

All test cases stop after 3 s so that `make APP=tcNN sim` terminates; set TCNN_STOP_AFTER_MS
to 0 for hardware. bTool runs forever.

## Context switch cost

Voluntary switch (a task blocks): 18 call-saved registers + SREG, about 100 cycles.
Preemption by an ISR: the RTOS_ISR wrapper saves the 15 call-clobbered registers, SREG and
RAMPZ, and only if a switch is needed the call-saved half as well.
