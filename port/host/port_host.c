/**
 * @file port_host.c
 * Host port implementation on top of ucontext. Single threaded: the "CPU" is the test
 * process, an interrupt is a function the test runs explicitly, and the idle sleep returns
 * immediately so the idle hook can drive simulated time.
 */

#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdio.h>
#include <ucontext.h>

#include "rtos.h"
#include "rtos_kernel.h"

#define HOST_STACK_SIZE (256u * 1024u)

static int _irqDisabled = 0;

rtos_port_cs_t rtos_port_enterCritical(void)
{
    const int s = _irqDisabled;
    _irqDisabled = 1;
    return s;
}

void rtos_port_exitCritical(rtos_port_cs_t s)
{
    _irqDisabled = s;
}

void rtos_port_enableInterrupts(void)
{
    _irqDisabled = 0;
}

void rtos_port_idleSleep(void)
{
}

void rtos_port_halt(void)
{
    fprintf(stderr, "rtos_port_halt: kernel halted\n");
    exit(2);
}

int rtos_host_interruptsDisabled(void)
{
    return _irqDisabled;
}

static ucontext_t *getCtx(rtos_tcb_t *pT)
{
    if(pT->ctx == NULL)
    {
        /* Idle task: adopt the current (main) context. */
        pT->ctx = calloc(1, sizeof(ucontext_t));
    }
    return (ucontext_t *)pT->ctx;
}

void rtos_port_switchContext(void)
{
    rtos_tcb_t * const pFrom = rtos_pCurrent;
    rtos_tcb_t * const pTo = rtos_pNext;

    if(!_irqDisabled)
    {
        fprintf(stderr, "rtos_port_switchContext called with interrupts enabled\n");
        exit(2);
    }
    rtos_pCurrent = pTo;
    swapcontext(getCtx(pFrom), getCtx(pTo));
}

rtos_port_ctx_t rtos_port_initStack(uint8_t *stackArea, uint16_t stackSize, void (*entry)(void))
{
    ucontext_t * const pCtx = calloc(1, sizeof(ucontext_t));

    (void)stackArea;
    (void)stackSize;
    getcontext(pCtx);
    pCtx->uc_stack.ss_sp = malloc(HOST_STACK_SIZE);
    pCtx->uc_stack.ss_size = HOST_STACK_SIZE;
    pCtx->uc_link = NULL;
    makecontext(pCtx, entry, 0);
    return pCtx;
}

void rtos_port_startTimer(void)
{
}

void rtos_host_simulateIsr(void (*isrBody)(void))
{
    const int s = _irqDisabled;

    _irqDisabled = 1;
    isrBody();
    rtos_isrTail();
    _irqDisabled = s;
}

void rtos_host_tick(void)
{
    rtos_host_simulateIsr(rtos_tickFromIsr);
}

uint32_t rtos_port_getRunTimeCounter(void)
{
    return rtos_tickCount;
}

uint32_t rtos_getTimeUs(void)
{
    return rtos_getTick() * (1000000ul / RTOS_CFG_TICK_HZ);
}
