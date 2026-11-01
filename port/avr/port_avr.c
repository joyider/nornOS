/**
 * @file port_avr.c
 * AVR port: tick timer on a 16 bit timer in CTC mode, stack preparation and the
 * microsecond clock.
 *
 * Timer selection: RTOS_CFG_TIMER is one of the 16 bit timers of the MCU: 1, 3, 4 or 5 on
 * the ATmega2560, only 1 on the ATmega328P. Left undefined it defaults to 3 where that
 * exists (keeps timer 1 free for PWM on the Mega) and to 1 otherwise. The prescaler is 64,
 * so the tick frequency must divide F_CPU/64 exactly; at 16 MHz and 1 kHz this gives
 * OCRnA = 249 and a counter resolution of 4 us.
 */

#include <avr/wdt.h>
#include "rtos.h"
#include "rtos_kernel.h"


/*
 * Defines
 */

#ifndef RTOS_CFG_TIMER
# if defined(TCCR3A)
#  define RTOS_CFG_TIMER 3
# else
#  define RTOS_CFG_TIMER 1
# endif
#endif

#define CAT2_(a, b)       a##b
#define CAT2(a, b)        CAT2_(a, b)
#define CAT3_(a, b, c)    a##b##c
#define CAT3(a, b, c)     CAT3_(a, b, c)

#define TCCRnA      CAT3(TCCR, RTOS_CFG_TIMER, A)
#define TCCRnB      CAT3(TCCR, RTOS_CFG_TIMER, B)
#define TCNTn       CAT2(TCNT, RTOS_CFG_TIMER)
#define OCRnA       CAT3(OCR,  RTOS_CFG_TIMER, A)
#define TIMSKn      CAT2(TIMSK, RTOS_CFG_TIMER)
#define TIFRn       CAT2(TIFR, RTOS_CFG_TIMER)
#define OCIEnA      CAT3(OCIE, RTOS_CFG_TIMER, A)
#define OCFnA       CAT3(OCF,  RTOS_CFG_TIMER, A)
#define WGMn2       CAT3(WGM,  RTOS_CFG_TIMER, 2)
#define CSn1        CAT3(CS,   RTOS_CFG_TIMER, 1)
#define CSn0        CAT3(CS,   RTOS_CFG_TIMER, 0)
#define TICK_VECT   CAT3(TIMER, RTOS_CFG_TIMER, _COMPA_vect)

#define PRESCALER   64ul
#define TIMER_TOP   (F_CPU / PRESCALER / RTOS_CFG_TICK_HZ - 1ul)
#define US_PER_COUNT (PRESCALER * 1000000ul / F_CPU)
#define US_PER_TICK  (1000000ul / RTOS_CFG_TICK_HZ)

#if (F_CPU / PRESCALER) % RTOS_CFG_TICK_HZ != 0
# error RTOS_CFG_TICK_HZ does not divide F_CPU/64 exactly
#endif
#if TIMER_TOP < 1 || TIMER_TOP > 65535
# error RTOS_CFG_TICK_HZ out of range for a 16 bit timer with prescaler 64
#endif
#if RTOS_CFG_TIMER != 1 && RTOS_CFG_TIMER != 3 && RTOS_CFG_TIMER != 4 && RTOS_CFG_TIMER != 5
# error RTOS_CFG_TIMER must be one of the 16 bit timers 1, 3, 4, 5
#endif
#if (RTOS_CFG_TIMER == 3 && !defined(TCCR3A)) || (RTOS_CFG_TIMER == 4 && !defined(TCCR4A)) \
    || (RTOS_CFG_TIMER == 5 && !defined(TCCR5A))
# error RTOS_CFG_TIMER names a timer this MCU does not have (the ATmega328P has only timer 1)
#endif


/*
 * Reset handling
 */

/** MCUSR as found at startup, before it is cleared. */
static uint8_t _mcusr __attribute__((section(".noinit")));

/** Set by rtos_port_reset so that the watchdog reset it causes reads as a software reset. */
static uint8_t _softResetMark __attribute__((section(".noinit")));
#define SOFT_RESET_MARK     0xC3u

/** Runs before main (and before .data/.bss setup, hence only .noinit data): a watchdog
    reset leaves the watchdog enabled with the shortest timeout, so it must be disabled at
    once, and MCUSR must be cleared for the next reset cause to be readable. */
void rtos_port_init3(void) __attribute__((naked, section(".init3"), used));
void rtos_port_init3(void)
{
    _mcusr = MCUSR;
    MCUSR = 0;
    wdt_disable();
}


void rtos_port_reset(void)
{
    cli();
    _softResetMark = SOFT_RESET_MARK;
#if RTOS_CFG_AVR_RESET_VIA_WDT
    /* A real reset through the watchdog. Only with a bootloader that disables the
       watchdog at start (Optiboot, recent Mega bootloaders); the original Mega 2560
       bootloader keeps resetting until power is cycled. */
    wdt_enable(WDTO_15MS);
    for(;;)
        ;
#else
    /* Restart the application image: peripherals keep their state, so every driver
       must initialize fully, but no bootloader is involved. Interrupt enables of
       peripherals are cleared here so that nothing fires before main sets them again. */
    wdt_disable();
    TIMSK0 = 0; TIMSK1 = 0; TIMSK2 = 0;
# if defined(TIMSK3)
    TIMSK3 = 0; TIMSK4 = 0; TIMSK5 = 0;
# endif
    __asm__ __volatile__ ("jmp 0");
    for(;;)
        ;
#endif
}


uint8_t rtos_port_getResetCause(void)
{
    if(_softResetMark == SOFT_RESET_MARK)
    {
        _softResetMark = 0;             /* rtos_port_reset, by watchdog or by jump */
        return RTOS_RESET_SOFTWARE;
    }
    if(_mcusr & _BV(WDRF))
        return RTOS_RESET_WATCHDOG;
    if(_mcusr & _BV(BORF))
        return RTOS_RESET_BROWN_OUT;
    if(_mcusr & _BV(EXTRF))
        return RTOS_RESET_EXTERNAL;
    if(_mcusr & _BV(PORF))
        return RTOS_RESET_POWER_ON;
    return RTOS_RESET_UNKNOWN;
}


/*
 * Watchdog
 */

void rtos_port_wdtStart(uint32_t timeoutMs)
{
    uint8_t wdto;
    if(timeoutMs <= 15)        wdto = WDTO_15MS;
    else if(timeoutMs <= 30)   wdto = WDTO_30MS;
    else if(timeoutMs <= 60)   wdto = WDTO_60MS;
    else if(timeoutMs <= 120)  wdto = WDTO_120MS;
    else if(timeoutMs <= 250)  wdto = WDTO_250MS;
    else if(timeoutMs <= 500)  wdto = WDTO_500MS;
    else if(timeoutMs <= 1000) wdto = WDTO_1S;
    else if(timeoutMs <= 2000) wdto = WDTO_2S;
    else if(timeoutMs <= 4000) wdto = WDTO_4S;
    else                       wdto = WDTO_8S;
    wdt_enable(wdto);
}


void rtos_port_wdtKick(void)
{
    wdt_reset();
}


/*
 * Function implementation
 */

void rtos_port_startTimer(void)
{
    TCCRnA = 0;
    TCCRnB = 0;
    TCNTn  = 0;
    OCRnA  = (uint16_t)TIMER_TOP;
    TIFRn  = _BV(OCFnA);
    TCCRnB = _BV(WGMn2) | _BV(CSn1) | _BV(CSn0);
    TIMSKn = _BV(OCIEnA);
}


/** The tick interrupt. */
RTOS_ISR(TICK_VECT)
{
    rtos_tickFromIsr();
}


uint32_t rtos_getTimeUs(void)
{
    rtos_tick_t t;
    uint16_t cnt;
    const rtos_port_cs_t cs = rtos_port_enterCritical();

    t = rtos_getTick();
    cnt = TCNTn;
    if(TIFRn & _BV(OCFnA))
    {
        /* A tick is pending but not yet counted. */
        ++ t;
        cnt = TCNTn;
    }
    rtos_port_exitCritical(cs);
    return (uint32_t)t * US_PER_TICK + (uint32_t)cnt * US_PER_COUNT;
}


rtos_port_runtime_t rtos_port_getRunTimeCounter(void)
{
    rtos_tick_t t = rtos_tickCount;
    uint16_t cnt = TCNTn;

    if(TIFRn & _BV(OCFnA))
    {
        /* A tick is pending but not yet counted. */
        ++ t;
        cnt = TCNTn;
    }
    return (uint32_t)t * (TIMER_TOP + 1ul) + cnt;
}


rtos_port_ctx_t rtos_port_initStack(uint8_t *stackArea, size_t stackSize, void (*entry)(void))
{
    uint8_t *sp = stackArea + stackSize - 1;
    const uint16_t addr = (uint16_t)entry;   /* avr-gcc function pointers are word addresses */
    uint8_t i;

    /* Return address, pushed as the CPU does it: low byte at the highest address. */
    *sp-- = (uint8_t)addr;
    *sp-- = (uint8_t)(addr >> 8);
#if defined(__AVR_3_BYTE_PC__)
    *sp-- = 0;
#endif

    /* r2-r17, r28, r29: 18 registers, content does not matter. */
    for(i=0; i<18; ++i)
        *sp-- = 0;

    /* SREG with the I flag cleared: the trampoline enables interrupts itself. */
    *sp-- = 0;

    return sp;
}
