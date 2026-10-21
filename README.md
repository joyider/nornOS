# nornOS

A small preemptive real time kernel for the ATmega2560 (Arduino Mega) and the ATmega328P
(Arduino UNO), written in C99 with the context switch in assembler. No dependency on the Arduino core; only avr-libc. Grew out
of the bTool brewing controller, whose application lives in applications/bTool.

## Layout

    kernel/      portable kernel: scheduler, time, events (rtos_kernel.c), semaphore and
                 mutex (rtos_sync.c), queue and mailbox (rtos_queue.c), task notification
                 (rtos_notify.c), software timers (rtos_timer.c), memory pool
                 (rtos_pool.c), public API (rtos.h)
    port/avr/    AVR port: port.h (critical sections, RTOS_ISR wrapper), port_context.S
                 (context switch), port_avr.c (tick timer, stack preparation, us clock)
    port/host/   Linux port on ucontext, used by the unit tests only
    hal/         optional C drivers for applications: uart, gpio, hd44780 (LCD), dht11,
                 board.h (Arduino pin numbers per board, timer 2 for the application)
    config/      rtos_config.template.h, copy to the application as rtos_config.h
    test/        host unit tests of the kernel, one program per test_*.c
    applications/<app>/  one folder per application: rtos_config.h and app.mk (BOARD)

## Build

    make APP=tc01             ELF/HEX in build/tc01-<board>, prints memory use
    make APP=tc01 BOARD=uno   build for another board than app.mk says
    make APP=tc01 sim         run in simavr, UART0 output on the terminal
    make APP=tc01 upload PORT=/dev/ttyACM0
    make host-test            kernel unit test on the PC

## Boards

    mega2560   ATmega2560, 16 MHz, 8 KB RAM. Tick on timer 3 by default (1, 4, 5 possible).
    uno        ATmega328P, 16 MHz, 2 KB RAM. Tick on timer 1, the only 16 bit timer.

The application picks its board in app.mk. Pins are written as BOARD_PIN_Dn from
hal/board.h, which maps the Arduino numbering to the ports of the selected MCU. Timer 0
is never touched; timer 2 is left to the application (board_timer2StartCtc). The port
layer needs nothing per board: RAMPZ and the 3 byte return address are selected by the
compiler's MCU macros.

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

## Applications (applications/)

    tc01   periodic task, events, semaphore from an ISR, ceiling mutex, stack check
    tc02   queue fed from an ISR with deliberate overflow, mailbox between tasks
    tc03   task notifications from ISR and timers, one shot and periodic software timers
    bTool  the brewing controller: DHT11 sensor, HD44780 LCD, thermostat relay, console

All test cases stop after 3 s so that `make APP=tcNN sim` terminates; set TCNN_STOP_AFTER_MS
to 0 for hardware. bTool runs forever.

## Context switch cost

Voluntary switch (a task blocks): 18 call-saved registers + SREG, about 100 cycles.
Preemption by an ISR: the RTOS_ISR wrapper saves the 15 call-clobbered registers, SREG and
RAMPZ, and only if a switch is needed the call-saved half as well.
