#ifndef RTOS_PORT_INCLUDED
#define RTOS_PORT_INCLUDED
/**
 * @file port.h
 * Host (Linux/PC) port of the kernel for unit testing without hardware. Contexts are
 * ucontext_t, critical sections are counters, and interrupts are simulated by the test
 * calling rtos_host_simulateIsr. See port_host.c.
 */

#include <stdint.h>
#include <stddef.h>
#include "rtos_config.h"
#include "rtos_types.h"

#define RTOS_PORT_MIN_STACK     1u
/* The host port runs tasks on its own malloc'd stacks and ignores the area given to
   rtos_createTask, so the tests may pass 1 byte arrays. */
#define RTOS_PORT_STACK_ALIGN   1u
#define RTOS_PORT_WEAK          __attribute__((weak))
#define RTOS_PORT_NOINIT
#define RTOS_PORT_STR(s)        (s)
#define RTOS_PORT_RUNTIME_HZ    RTOS_CFG_TICK_HZ

/** Define an interrupt body on the host; it is just a function the test can simulate. */
#define RTOS_ISR(vector)        RTOS_ISR_(vector)
#define RTOS_ISR_(vector)       void vector##_rtosBody(void)

typedef void *rtos_port_ctx_t;
typedef int rtos_port_cs_t;
typedef uint32_t rtos_port_runtime_t;

rtos_port_cs_t rtos_port_enterCritical(void);
void rtos_port_exitCritical(rtos_port_cs_t s);
void rtos_port_enableInterrupts(void);
void rtos_port_idleSleep(void);
void rtos_port_idleSleepTicks(rtos_tick_t ticks);
void rtos_port_halt(void) __attribute__((noreturn));
void rtos_port_switchContext(void);
rtos_port_ctx_t rtos_port_initStack(uint8_t *stackArea, size_t stackSize, void (*entry)(void));
void rtos_port_startTimer(void);
rtos_port_runtime_t rtos_port_getRunTimeCounter(void);
void rtos_port_reset(void) __attribute__((noreturn));
uint8_t rtos_port_getResetCause(void);
void rtos_port_wdtStart(uint32_t timeoutMs);
void rtos_port_wdtKick(void);

/* Host specific test support. */

/** Run \a isrBody as if it were an interrupt: with interrupts disabled, followed by the
    kernel's ISR tail which may switch task. */
void rtos_host_simulateIsr(void (*isrBody)(void));

/** Advance the system time by one tick, as the timer interrupt would. */
void rtos_host_tick(void);

/** True while inside a simulated ISR or a critical section. */
int rtos_host_interruptsDisabled(void);

#endif  /* RTOS_PORT_INCLUDED */
