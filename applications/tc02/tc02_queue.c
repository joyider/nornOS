/**
 * @file tc02_queue.c
 * Test case 2 for the v2 kernel: message queue and mailbox.
 *
 *   ISR (timer 2 at 500 Hz):   sends a running 16 bit sequence number to a queue of 8.
 *   task 2 "consumer":         receives from the queue, checks the sequence for gaps,
 *                              deliberately stalls every 500 ms for 30 ms so the queue
 *                              fills up and the ISR has to drop a few items.
 *   task 1 "sensor":           every 50 ms posts a fake temperature to a mailbox.
 *   task 3 "reporter":         once a second fetches the mailbox and prints statistics.
 *
 * Expected: received + dropped == sequence number sent, gaps == drop episodes, mailbox
 * overwrites 19 per second (20 posts, 1 fetch).
 */

#include <avr/io.h>
#include "rtos.h"
#include "uart.h"
#include "board.h"

#define TASK_SENSOR     1u
#define TASK_CONSUMER   2u
#define TASK_REPORTER   3u

#define APPL_IRQ_HZ     500ul
#if !BOARD_TIMER2_HZ_IS_EXACT(APPL_IRQ_HZ)
# error APPL_IRQ_HZ is not an exact rate of timer 2
#endif
#define QUEUE_CAPACITY  8u

#ifndef TC02_STOP_AFTER_MS
# define TC02_STOP_AFTER_MS  3000ul
#endif

#define STACK_SIZE      192u

static uint8_t _stackSensor[STACK_SIZE];
static uint8_t _stackConsumer[STACK_SIZE];
static uint8_t _stackReporter[STACK_SIZE];

static uint8_t _qStorage[QUEUE_CAPACITY * sizeof(uint16_t)];
static rtos_queue_t _qSeq = RTOS_QUEUE_INIT(_qStorage, sizeof(uint16_t), QUEUE_CAPACITY);

static uint8_t _mbStorage[sizeof(int16_t)];
static rtos_mbox_t _mbTemp = RTOS_MBOX_INIT(_mbStorage, sizeof(int16_t));

static rtos_mutex_t _mutexStats = RTOS_MUTEX_INIT(TASK_REPORTER);

static struct
{
    uint16_t sent;          /* Written by the ISR only. */
    uint16_t dropped;       /* Written by the ISR only. */
    uint16_t received;
    uint16_t gaps;
    uint16_t stalls;
    uint16_t mboxOverwrites;
    uint8_t  maxQueueFill;
} _stats;


RTOS_ISR(TIMER2_COMPA_vect)
{
    const uint16_t seq = ++_stats.sent;
    if(!rtos_queueSendFromIsr(&_qSeq, &seq))
        ++ _stats.dropped;
}


static void startApplInterrupt(void)
{
    board_timer2StartCtc((uint8_t)BOARD_TIMER2_TOP(APPL_IRQ_HZ));
}


void tc02_idleHook(void)
{
}


static void taskConsumer(void *arg)
{
    uint16_t expected = 1;
    rtos_tick_t nextStall = RTOS_MS(500);
    (void)arg;

    for(;;)
    {
        uint16_t seq;
        uint8_t fill;

        if(!rtos_queueReceive(&_qSeq, &seq, RTOS_MS(100)))
        {
            uart_putsP(PSTR("consumer: queue timeout, ISR not running!\r\n"));
            continue;
        }
        fill = rtos_queueCount(&_qSeq);

        rtos_mutexLock(&_mutexStats);
        ++ _stats.received;
        if(seq != expected)
            ++ _stats.gaps;
        if(fill > _stats.maxQueueFill)
            _stats.maxQueueFill = fill;
        rtos_mutexUnlock(&_mutexStats);
        expected = (uint16_t)(seq + 1u);

        if((rtos_tickdiff_t)(rtos_getTick() - nextStall) >= 0)
        {
            /* Stall: 30 ms without receiving, 12 interrupts arrive, 8 fit in the queue. */
            nextStall += RTOS_MS(500);
            rtos_mutexLock(&_mutexStats);
            ++ _stats.stalls;
            rtos_mutexUnlock(&_mutexStats);
            rtos_delay(RTOS_MS(30));
        }
    }
}


static void taskSensor(void *arg)
{
    int16_t temp = 200;     /* 20.0 degrees */
    int8_t dir = 1;
    (void)arg;

    for(;;)
    {
        (void)rtos_waitPeriodic(RTOS_MS(50));
        temp = (int16_t)(temp + dir);
        if(temp >= 250  ||  temp <= 150)
            dir = (int8_t)-dir;
        if(rtos_mboxPost(&_mbTemp, &temp))
        {
            rtos_mutexLock(&_mutexStats);
            ++ _stats.mboxOverwrites;
            rtos_mutexUnlock(&_mutexStats);
        }
    }
}


static void taskReporter(void *arg)
{
    (void)arg;
    for(;;)
    {
        int16_t temp = 0;
        uint16_t sent, dropped;
        bool gotTemp;

        (void)rtos_waitPeriodic(RTOS_MS(1000));

        gotTemp = rtos_mboxFetch(&_mbTemp, &temp, RTOS_NO_WAIT);

        rtos_mutexLock(&_mutexStats);
        cli();
        sent = _stats.sent;
        dropped = _stats.dropped;
        sei();
        uart_putsP(PSTR("t="));          uart_putU32(rtos_getTick());
        uart_putsP(PSTR(" sent="));      uart_putU32(sent);
        uart_putsP(PSTR(" received=")); uart_putU32(_stats.received);
        uart_putsP(PSTR(" dropped="));   uart_putU32(dropped);
        uart_putsP(PSTR(" gaps="));      uart_putU32(_stats.gaps);
        uart_putsP(PSTR(" stalls="));    uart_putU32(_stats.stalls);
        uart_putsP(PSTR(" maxFill="));   uart_putU32(_stats.maxQueueFill);
        uart_putsP(PSTR(" temp="));
        if(gotTemp)
            uart_putI32(temp);
        else
            uart_putsP(PSTR("none"));
        uart_putsP(PSTR(" mboxOverwrites=")); uart_putU32(_stats.mboxOverwrites);
        uart_putsP(PSTR(" stack="));     uart_putU32(rtos_getStackFree(TASK_SENSOR));
        uart_putc('/');                  uart_putU32(rtos_getStackFree(TASK_CONSUMER));
        uart_putc('/');                  uart_putU32(rtos_getStackFree(TASK_REPORTER));
        uart_putsP(PSTR("\r\n"));
        rtos_mutexUnlock(&_mutexStats);

#if TC02_STOP_AFTER_MS > 0
        if(rtos_getTick() >= RTOS_MS(TC02_STOP_AFTER_MS))
        {
            uart_putsP(PSTR("tc02: done\r\n"));
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
    uart_init(115200);
    uart_putsP(PSTR("\r\nnornOS " RTOS_VERSION " tc02_queue\r\n"));

    rtos_init();
    rtos_createTask(TASK_SENSOR,   taskSensor,   NULL, _stackSensor,   sizeof(_stackSensor));
    rtos_createTask(TASK_CONSUMER, taskConsumer, NULL, _stackConsumer, sizeof(_stackConsumer));
    rtos_createTask(TASK_REPORTER, taskReporter, NULL, _stackReporter, sizeof(_stackReporter));

    startApplInterrupt();
    rtos_start();
    return 0;
}
