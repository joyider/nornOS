/**
 * @file tc05_panic.c
 * Test case 5: panic, reset and recovery. The test provokes four failures in turn, each
 * one ending in a reset, and checks after every reset that the panic record and the
 * reset cause say what happened. The stage number lives in memory that survives the
 * reset (RTOS_PORT_NOINIT), so the test continues where it was.
 *
 *   stage 1  rtos_panic from the application         -> record APPLICATION, software reset
 *   stage 2  stack overflow by recursion in task 1   -> record STACK_OVERFLOW task 1
 *   stage 3  task 1 stops checking in to the watchdog -> record WATCHDOG task 1
 *   stage 4  task 2 hogs the CPU, idle never runs     -> hardware watchdog, no record
 *
 * Finally RESULT tc05 PASS or FAIL. A fresh power-up starts at stage 1.
 */

#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "testresult.h"


/*
 * Defines
 */

#define TASK_HOG        2
#define TASK_VICTIM     1

#if RTOS_PORT_MIN_STACK > 192u
# define STACK_SIZE     (2u * RTOS_PORT_MIN_STACK)
#else
# define STACK_SIZE     192u
#endif

/* Stage 4 needs a reset through the hardware watchdog. On an AVR whose bootloader does
   not survive one (the original Mega 2560 bootloader), the port is configured to reset by
   jumping to the reset vector instead, and then stage 4 would brick the board until the
   next power cycle, so it is skipped. */
#if defined(__AVR__)  &&  !RTOS_CFG_AVR_RESET_VIA_WDT
# define STAGES         3
#else
# define STAGES         4
#endif


/*
 * Data definitions
 */

static uint8_t _stackVictim[STACK_SIZE];
static uint8_t _stackHog[STACK_SIZE];

/** Stage that is running, and its complement as validity check; both survive resets. */
static uint8_t _stage RTOS_PORT_NOINIT;
static uint8_t _stageCheck RTOS_PORT_NOINIT;
static uint8_t _failures RTOS_PORT_NOINIT;


/*
 * Function implementation
 */

static void putReason(uint8_t reason)
{
    switch(reason)
    {
    case RTOS_PANIC_ASSERT:         uart_puts("ASSERT"); break;
    case RTOS_PANIC_STACK_OVERFLOW: uart_puts("STACK_OVERFLOW"); break;
    case RTOS_PANIC_WATCHDOG:       uart_puts("WATCHDOG"); break;
    case RTOS_PANIC_DEADLOCK:       uart_puts("DEADLOCK"); break;
    case RTOS_PANIC_FAULT:          uart_puts("FAULT"); break;
    case RTOS_PANIC_APPLICATION:    uart_puts("APPLICATION"); break;
    default:                        uart_puts("NONE"); break;
    }
}

static void putCause(uint8_t cause)
{
    switch(cause)
    {
    case RTOS_RESET_POWER_ON:  uart_puts("power-on"); break;
    case RTOS_RESET_EXTERNAL:  uart_puts("external"); break;
    case RTOS_RESET_BROWN_OUT: uart_puts("brown-out"); break;
    case RTOS_RESET_WATCHDOG:  uart_puts("watchdog"); break;
    case RTOS_RESET_SOFTWARE:  uart_puts("software"); break;
    default:                   uart_puts("unknown"); break;
    }
}


/** Report before the reset, with the UART drained so the line gets out. */
void rtos_panicHook(const rtos_panicInfo_t *pInfo)
{
    uart_puts("PANIC ");
    putReason(pInfo->reason);
    uart_puts(" task=");    uart_putU32(pInfo->task);
    uart_puts(" arg1=");    uart_putU32(pInfo->arg1);
    uart_puts(" arg2=");    uart_putU32(pInfo->arg2);
    uart_puts(" tick=");    uart_putU32(pInfo->tick);
    uart_puts(" -> reset\r\n");
    uart_flush();
}


/* ---- the provocations ---------------------------------------------------------------- */

/* Not a tail call and not inlinable, so every level really takes stack. */
static uint8_t recurse(uint8_t depth) __attribute__((noinline));
static uint8_t recurse(uint8_t depth)
{
    volatile uint8_t pad[16];
    uint8_t i, sum;
    for(i=0; i<sizeof(pad); ++i)
        pad[i] = depth;
    rtos_delay(RTOS_MS(1));             /* one level per tick: the switch check catches it */
    sum = recurse((uint8_t)(depth + 1u));
    for(i=0; i<sizeof(pad); ++i)
        sum = (uint8_t)(sum + pad[i]);
    return sum;
}

static void taskVictim(void *arg)
{
    (void)arg;
    switch(_stage)
    {
    case 1:
        rtos_delay(RTOS_MS(50));
        rtos_panic(RTOS_PANIC_APPLICATION, 0xAB, 0xCD);
        break;
    case 2:
        rtos_delay(RTOS_MS(50));
        (void)recurse(1);
        break;
    case 3:
        rtos_wdtInit(1000);
        rtos_wdtMonitor(TASK_VICTIM, RTOS_MS(100));
        rtos_delay(RTOS_MS(60)); rtos_wdtCheckIn();
        rtos_delay(RTOS_MS(60)); rtos_wdtCheckIn();
        rtos_delay(RTOS_MS(60)); rtos_wdtCheckIn();
        for(;;)
            rtos_delay(RTOS_MS(1000));  /* no more check-ins */
    case 4:
        rtos_wdtInit(250);
        for(;;)
            rtos_delay(RTOS_MS(1000));
    default:
        break;
    }
    for(;;)
        rtos_delay(RTOS_MS(1000));
}

static void taskHog(void *arg)
{
    (void)arg;
    if(_stage == 4)
    {
        rtos_delay(RTOS_MS(50));
        for(;;)
            ;                           /* idle starves: the hardware watchdog bites */
    }
    for(;;)
        rtos_delay(RTOS_MS(1000));
}


/* ---- verdict of the previous stage ---------------------------------------------------- */

static void checkPrevious(uint8_t stage, bool havePanic, const rtos_panicInfo_t *p, uint8_t cause)
{
    tc_failures = _failures;
    switch(stage)
    {
    case 1:
        tc_expect("1: record present", havePanic);
        tc_expect("1: reason APPLICATION", p->reason == RTOS_PANIC_APPLICATION);
        tc_expect("1: args", p->arg1 == 0xAB && p->arg2 == 0xCD && p->task == TASK_VICTIM);
        tc_expect("1: software reset", cause == RTOS_RESET_SOFTWARE);
        break;
    case 2:
        tc_expect("2: record present", havePanic);
        tc_expect("2: reason STACK_OVERFLOW", p->reason == RTOS_PANIC_STACK_OVERFLOW);
        tc_expect("2: task 1", p->arg1 == TASK_VICTIM);
        tc_expect("2: software reset", cause == RTOS_RESET_SOFTWARE);
        break;
    case 3:
        tc_expect("3: record present", havePanic);
        tc_expect("3: reason WATCHDOG", p->reason == RTOS_PANIC_WATCHDOG);
        tc_expect("3: task 1 late", p->arg1 == TASK_VICTIM && p->task == RTOS_IDLE_TASK);
        tc_expect("3: software reset", cause == RTOS_RESET_SOFTWARE);
        break;
    case 4:
        tc_expect("4: no record", !havePanic);
        tc_expect("4: watchdog reset", cause == RTOS_RESET_WATCHDOG);
        break;
    default:
        break;
    }
    _failures = tc_failures;
}


int main(void)
{
    rtos_panicInfo_t panic;
    bool havePanic;
    uint8_t cause, previous;

    uart_init(115200);
    rtos_init();                        /* latches the panic record */
    havePanic = rtos_getLastPanic(&panic);
    cause = rtos_getResetCause();

    if(_stageCheck != (uint8_t)~_stage  ||  _stage > STAGES
       ||  cause == RTOS_RESET_POWER_ON  ||  cause == RTOS_RESET_EXTERNAL
      )
    {
        /* First boot after flashing or power-up, or the operator pressed reset (the
           serial monitor does that when it opens the port): start over. */
        _stage = 0;
        _failures = 0;
    }
    previous = _stage;

    uart_puts("\r\nnornOS " RTOS_VERSION " tc05_panic boot after stage ");
    uart_putU32(previous);
    uart_puts(" cause=");               putCause(cause);
    uart_puts(" record=");
    if(havePanic)
        putReason(panic.reason);
    else
        uart_puts("none");
    uart_puts("\r\n");

    checkPrevious(previous, havePanic, &panic, cause);
    if(previous == STAGES)
    {
#if STAGES < 4
        uart_puts("stage 4 skipped: this port resets without the watchdog\r\n");
#endif
        _stage = 0;
        _stageCheck = (uint8_t)~_stage;
        tc_failures = _failures;
        tc_finish("tc05");
    }

    _stage = (uint8_t)(previous + 1u);
    _stageCheck = (uint8_t)~_stage;
    uart_puts("stage ");
    uart_putU32(_stage);
    uart_puts("\r\n");

    rtos_createTask(TASK_VICTIM, taskVictim, NULL, _stackVictim, sizeof(_stackVictim));
    rtos_createTask(TASK_HOG,    taskHog,    NULL, _stackHog,    sizeof(_stackHog));
    rtos_start();
    return 0;
}
