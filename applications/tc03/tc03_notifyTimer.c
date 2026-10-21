/**
 * @file tc03_notifyTimer.c
 * Test case 3 for the v2 kernel: task notifications and software timers.
 *
 *   timer "blink" (periodic 250 ms): notifies task 1 with INC; task 1 toggles the LED.
 *   timer "burst" (one shot, restarts itself alternating 100/400 ms): ORs a flag into
 *                 task 3's notification value.
 *   ISR timer 2 at 500 Hz: notifies task 2 with INC; task 2 counts with notifyTake.
 *   task 3 "reporter": every second reads its accumulated flags with a non-blocking
 *                 notifyWait and prints statistics.
 *
 * Expected per second: blinks +4, isrNotifies +500, burst flags alternate 1 and 2 with
 * two bursts per second.
 */

#include <avr/io.h>
#include "rtos.h"
#include "uart.h"
#include "board.h"

#define TASK_LED        1u
#define TASK_COUNTER    2u
#define TASK_REPORTER   3u

#define APPL_IRQ_HZ     500ul
#if !BOARD_TIMER2_HZ_IS_EXACT(APPL_IRQ_HZ)
# error APPL_IRQ_HZ is not an exact rate of timer 2
#endif

#ifndef TC03_STOP_AFTER_MS
# define TC03_STOP_AFTER_MS  3000ul
#endif

#define STACK_SIZE      192u

static uint8_t _stackLed[STACK_SIZE];
static uint8_t _stackCounter[STACK_SIZE];
static uint8_t _stackReporter[STACK_SIZE];

static rtos_timer_t _tmrBlink;
static rtos_timer_t _tmrBurst;

static volatile uint16_t _blinks = 0;
static volatile uint16_t _isrNotifies = 0;
static volatile uint16_t _bursts = 0;


RTOS_ISR(TIMER2_COMPA_vect)
{
    rtos_notifyFromIsr(TASK_COUNTER, 0, RTOS_NOTIFY_INC);
}


static void startApplInterrupt(void)
{
    board_timer2StartCtc((uint8_t)BOARD_TIMER2_TOP(APPL_IRQ_HZ));
}


void tc03_idleHook(void)
{
}


static void cbBlink(void *arg)
{
    (void)arg;
    rtos_notifyFromIsr(TASK_LED, 0, RTOS_NOTIFY_INC);
}


static void cbBurst(void *arg)
{
    uint8_t * const pPhase = arg;

    ++ _bursts;
    rtos_notifyFromIsr(TASK_REPORTER, (uint16_t)(1u << *pPhase), RTOS_NOTIFY_OR);
    *pPhase ^= 1u;
    rtos_timerStart(&_tmrBurst, *pPhase ? RTOS_MS(100) : RTOS_MS(400), 0);
}


static void taskLed(void *arg)
{
    (void)arg;
    DDRB |= _BV(PB7);
    for(;;)
    {
        if(rtos_notifyTake(RTOS_MS(1000)))
        {
            PINB = _BV(PB7);
            ++ _blinks;
        }
        else
            uart_putsP(PSTR("led: no notification for 1 s, blink timer dead!\r\n"));
    }
}


static void taskCounter(void *arg)
{
    (void)arg;
    for(;;)
    {
        if(rtos_notifyTake(RTOS_MS(100)))
            ++ _isrNotifies;
        else
            uart_putsP(PSTR("counter: timeout, ISR not running!\r\n"));
    }
}


static void taskReporter(void *arg)
{
    (void)arg;
    for(;;)
    {
        uint16_t flags = 0;

        (void)rtos_waitPeriodic(RTOS_MS(1000));
        (void)rtos_notifyWait(&flags, RTOS_NO_WAIT);

        uart_putsP(PSTR("t="));              uart_putU32(rtos_getTick());
        uart_putsP(PSTR(" blinks="));        uart_putU32(_blinks);
        uart_putsP(PSTR(" isrNotifies="));   uart_putU32(_isrNotifies);
        uart_putsP(PSTR(" bursts="));        uart_putU32(_bursts);
        uart_putsP(PSTR(" burstFlags=0x"));  uart_putHex8((uint8_t)flags);
        uart_putsP(PSTR(" blinkIn="));       uart_putU32(rtos_timerRemaining(&_tmrBlink));
        uart_putsP(PSTR(" stack="));         uart_putU32(rtos_getStackFree(TASK_LED));
        uart_putc('/');                      uart_putU32(rtos_getStackFree(TASK_COUNTER));
        uart_putc('/');                      uart_putU32(rtos_getStackFree(TASK_REPORTER));
        uart_putsP(PSTR("\r\n"));

#if TC03_STOP_AFTER_MS > 0
        if(rtos_getTick() >= RTOS_MS(TC03_STOP_AFTER_MS))
        {
            uart_putsP(PSTR("tc03: done\r\n"));
            rtos_port_halt();
        }
#endif
    }
}


void rtos_assertFailed(const char *file, uint16_t line)
{
    cli();
    uart_putsP(PSTR("\r\nASSERTION FAILED: "));
    uart_putsP(file);
    uart_putsP(PSTR(", line "));
    uart_putU32(line);
    uart_putsP(PSTR("\r\n"));
    rtos_port_halt();
}


int main(void)
{
    static uint8_t burstPhase = 0;

    uart_init(115200);
    uart_putsP(PSTR("\r\nnornOS " RTOS_VERSION " tc03_notifyTimer\r\n"));

    rtos_init();
    rtos_createTask(TASK_LED,      taskLed,      NULL, _stackLed,      sizeof(_stackLed));
    rtos_createTask(TASK_COUNTER,  taskCounter,  NULL, _stackCounter,  sizeof(_stackCounter));
    rtos_createTask(TASK_REPORTER, taskReporter, NULL, _stackReporter, sizeof(_stackReporter));

    rtos_timerInit(&_tmrBlink, cbBlink, NULL);
    rtos_timerInit(&_tmrBurst, cbBurst, &burstPhase);
    rtos_timerStart(&_tmrBlink, RTOS_MS(250), RTOS_MS(250));
    rtos_timerStart(&_tmrBurst, RTOS_MS(100), 0);

    startApplInterrupt();
    rtos_start();
    return 0;
}
