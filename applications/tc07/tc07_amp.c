/**
 * @file tc07_amp.c
 * Test case 7: two nornOS instances, one per ESP32 core. Core 0 (this image) loads and
 * starts core 1 (core1/), then sends it 200 requests per second over the shared memory
 * link; core 1 answers each one. Core 0 checks every answer, the order, and that core 1's
 * own tick advances, and measures the round trip.
 */

#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "amp.h"
#include "testresult.h"

#define TASK_SENDER     2
#define TASK_RECEIVER   1

#ifndef TC07_STOP_AFTER_MS
# define TC07_STOP_AFTER_MS  3000ul
#endif

#define STACK_SIZE      (2u * RTOS_PORT_MIN_STACK)

static uint8_t _stackSender[STACK_SIZE];
static uint8_t _stackReceiver[STACK_SIZE];

static volatile uint32_t _sent = 0, _received = 0, _errors = 0, _sendFull = 0;
static volatile uint32_t _core1Beats = 0, _core1Tick = 0;
static volatile uint32_t _rttSum = 0, _rttMax = 0;
static volatile uint32_t _sentAt[256];


static void taskSender(void *arg)
{
    uint32_t seq = 0;
    (void)arg;
    for(;;)
    {
        amp_msg_t m;
        (void)rtos_waitPeriodic(RTOS_MS(5));
        ++ seq;
        m.w[0] = seq;
        m.w[1] = seq % 1000u;
        m.w[2] = 7u + seq % 13u;
        m.w[3] = 0;
        _sentAt[seq % 256u] = rtos_port_ccount();
        if(amp_send(&m))
            ++ _sent;
        else
            ++ _sendFull;
    }
}


static void taskReceiver(void *arg)
{
    uint32_t expectSeq = 1;
    (void)arg;
    for(;;)
    {
        amp_msg_t r;
        if(amp_receive(&r, RTOS_MS(100)))
        {
            const uint32_t seq = r.w[0];
            const uint32_t rtt = rtos_port_ccount() - _sentAt[seq % 256u];
            ++ _received;
            if(seq != expectSeq  ||  r.w[1] != (seq % 1000u) * (7u + seq % 13u) + seq)
                ++ _errors;
            expectSeq = seq + 1;
            _core1Beats = r.w[2];
            _core1Tick = r.w[3];
            _rttSum += rtt;
            if(rtt > _rttMax)
                _rttMax = rtt;
        }

#if TC07_STOP_AFTER_MS > 0
        if(rtos_getTick() >= RTOS_MS(TC07_STOP_AFTER_MS))
        {
            const uint32_t n = _received ? _received : 1;
            uart_puts("tc07 sent=");          uart_putU32(_sent);
            uart_puts(" received=");          uart_putU32(_received);
            uart_puts(" errors=");            uart_putU32(_errors);
            uart_puts(" full=");              uart_putU32(_sendFull);
            uart_puts(" core1Tick=");         uart_putU32(_core1Tick);
            uart_puts(" core1Beats=");        uart_putU32(_core1Beats);
            uart_puts(" rtt avg=");           uart_putU32(_rttSum / n / (uint32_t)(F_CPU / 1000000ul));
            uart_puts(" us max=");            uart_putU32(_rttMax / (uint32_t)(F_CPU / 1000000ul));
            uart_puts(" us\r\n");
            tc_expectNear("sent", _sent, TC07_STOP_AFTER_MS / 5);
            tc_expectRange("received == sent (-1 in flight)", _sent - _received, 0, 1);
            tc_expectRange("errors", _errors, 0, 0);
            tc_expectRange("full", _sendFull, 0, 0);
            tc_expectNear("core1Tick", _core1Tick, TC07_STOP_AFTER_MS);
            tc_expectNear("core1Beats", _core1Beats, TC07_STOP_AFTER_MS / 10);
            tc_finish("tc07");
        }
#endif
    }
}


void rtos_assertFailed(const char *file, uint16_t line)
{
    (void)rtos_port_enterCritical();
    uart_puts("\r\nASSERTION FAILED: ");
    uart_putsP(file);
    uart_puts(", line ");
    uart_putU32(line);
    uart_puts("\r\n");
    rtos_port_halt();
}


int main(void)
{
    uart_init(115200);
    uart_puts("\r\nnornOS " RTOS_VERSION " tc07_amp: core 1 image ");
    uart_putU32(amp_core1_image_size);
    uart_puts(" bytes\r\n");

    rtos_init();
    rtos_createTask(TASK_SENDER,   taskSender,   NULL, _stackSender,   sizeof(_stackSender));
    rtos_createTask(TASK_RECEIVER, taskReceiver, NULL, _stackReceiver, sizeof(_stackReceiver));
    amp_init();                         /* starts core 1 */
    rtos_start();
    return 0;
}
