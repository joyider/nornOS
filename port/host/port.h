#ifndef RTOS_PORT_INCLUDED
#define RTOS_PORT_INCLUDED
/**
 * @file port.h
 * Host (Linux/PC) port of the kernel for unit testing without hardware. Contexts are
 * ucontext_t, critical sections are counters, and interrupts are simulated by the test
 * calling rtos_host_simulateIsr. See port_host.c.
 */

#include <stdint.h>
#include "rtos_config.h"
#include "rtos_types.h"

#define RTOS_PORT_MIN_STACK     1u
#define RTOS_PORT_WEAK          __attribute__((weak))
#define RTOS_PORT_STR(s)        (s)
#define RTOS_PORT_RUNTIME_UNIT_US   (1000000ul / RTOS_CFG_TICK_HZ)

/** Define an interrupt body on the host; it is just a function the test can simulate. */
#define RTOS_ISR(vector)        void vector##_rtosBody(void)

typedef void *rtos_port_ctx_t;
typedef int rtos_port_cs_t;

rtos_port_cs_t rtos_port_enterCritical(void);
void rtos_port_exitCritical(rtos_port_cs_t s);
void rtos_port_enableInterrupts(void);
void rtos_port_idleSleep(void);
void rtos_port_halt(void);
void rtos_port_switchContext(void);
rtos_port_ctx_t rtos_port_initStack(uint8_t *stackArea, uint16_t stackSize, void (*entry)(void));
void rtos_port_startTimer(void);
uint32_t rtos_port_getRunTimeCounter(void);

/* Host specific test support. */

/** Run \a isrBody as if it were an interrupt: with interrupts disabled, followed by the
    kernel's ISR tail which may switch task. */
void rtos_host_simulateIsr(void (*isrBody)(void));

/** Advance the system time by one tick, as the timer interrupt would. */
void rtos_host_tick(void);

/** True while inside a simulated ISR or a critical section. */
int rtos_host_interruptsDisabled(void);

#endif  /* RTOS_PORT_INCLUDED */
