/**
 * @file core1_main.c
 * Test case 7, the APP CPU side: a worker task that answers every request from core 0
 * with the product of its operands plus the sequence number, and a periodic task that
 * keeps its own tick count so core 0 can see that this kernel instance ticks on its own.
 * No UART here: core 0 owns the console.
 */

#include "rtos.h"
#include "amp.h"

#define TASK_WORKER     2
#define TASK_BEAT       1

static uint8_t _stackWorker[2u * RTOS_PORT_MIN_STACK];
static uint8_t _stackBeat[2u * RTOS_PORT_MIN_STACK];
static volatile uint32_t _beats = 0;

static void taskBeat(void *arg)
{
    (void)arg;
    for(;;)
    {
        (void)rtos_waitPeriodic(RTOS_MS(10));
        ++ _beats;
    }
}

static void taskWorker(void *arg)
{
    (void)arg;
    for(;;)
    {
        amp_msg_t m;
        if(amp_receive(&m, RTOS_FOREVER))
        {
            amp_msg_t r;
            r.w[0] = m.w[0];                    /* sequence */
            r.w[1] = m.w[1] * m.w[2] + m.w[0];  /* the answer */
            r.w[2] = _beats;                    /* core 1's own time */
            r.w[3] = rtos_getTick();
            while(!amp_send(&r))
                rtos_delay(1);
        }
    }
}

int main(void)
{
    rtos_init();
    rtos_createTask(TASK_WORKER, taskWorker, NULL, _stackWorker, sizeof(_stackWorker));
    rtos_createTask(TASK_BEAT,   taskBeat,   NULL, _stackBeat,   sizeof(_stackBeat));
    amp_init();
    rtos_start();
    return 0;
}
