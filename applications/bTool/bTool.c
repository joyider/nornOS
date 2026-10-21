/**
 * @file bTool.c
 * bTool brewing controller on the v2 kernel. Port of applications/bTool, which was a
 * sketch of tasks on RTuinOS: temperature sensing with a DHT11, an LCD, a control task and
 * a serial task. The empty HERMS and fermentation tasks of the old version are not carried
 * over; add them when they get a function.
 *
 * Hardware (Arduino pin numbers, mapped by hal/board.h for the Mega and the UNO):
 *   DHT11 data        pin 2
 *   LCD RS, E         pin 12, pin 11
 *   LCD D4..D7        pin 10, 9, 8, 7
 *   relay (heater)    pin 4
 *   LED               pin 13, mirrors the relay
 *   UART0 115200 8N1  console
 *
 * Tasks (priority):
 *   4 console   every 20 ms polls the UART for commands, every 5 s prints a status line.
 *   3 control   every 250 ms: thermostat with hysteresis on the latest sensor reading.
 *   2 sensor    every 2 s reads the DHT11 and publishes the reading.
 *   1 lcd       redraws when a new reading arrives, at least every second.
 *
 * Console commands: '+' / '-' change the setpoint by 0.5 C, 's' print status now,
 * 'r' reset statistics, '?' help.
 */

#include <avr/io.h>
#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "hd44780.h"
#include "dht11.h"


/*
 * Defines
 */

#define TASK_LCD        1u
#define TASK_SENSOR     2u
#define TASK_CONTROL    3u
#define TASK_CONSOLE    4u

#define SENSOR_PERIOD_MS    2000u
#define CONTROL_PERIOD_MS   250u
#define CONSOLE_PERIOD_MS   20u
#define STATUS_PERIOD_MS    5000u

#define SETPOINT_DEFAULT_DECIC  250     /* 25.0 C */
#define SETPOINT_MIN_DECIC      0
#define SETPOINT_MAX_DECIC      1000
#define HYSTERESIS_DECIC        5       /* 0.5 C */

#if defined(BOARD_IS_UNO)
# define STACK_SIZE     160u    /* 2 KB of RAM: four tasks plus idle must fit */
#else
# define STACK_SIZE     256u
#endif


/*
 * Local type definitions
 */

typedef struct state
{
    dht11_reading_t reading;    /**< Latest sensor reading, valid if sensorOk. */
    dht11_status_t sensorStatus;
    bool sensorOk;
    int16_t setpointDeciC;
    bool relayOn;
    uint16_t sensorErrors;
    uint16_t sensorReads;
} state_t;


/*
 * Data definitions
 */

static uint8_t _stackLcd[STACK_SIZE];
static uint8_t _stackSensor[STACK_SIZE];
static uint8_t _stackControl[STACK_SIZE];
static uint8_t _stackConsole[STACK_SIZE];

static const gpio_pin_t _pinDht11 = BOARD_PIN_D2;
static const gpio_pin_t _pinRelay = BOARD_PIN_D4;
static const gpio_pin_t _pinLed   = BOARD_PIN_LED;

static hd44780_t _lcd =
{
    .rs = BOARD_PIN_D12, .e = BOARD_PIN_D11,
    .d4 = BOARD_PIN_D10, .d5 = BOARD_PIN_D9, .d6 = BOARD_PIN_D8, .d7 = BOARD_PIN_D7,
};

/** Shared state. Guarded by _mutexState; ceiling = highest user, the console. */
static state_t _state =
{
    .sensorStatus = DHT11_ERR_NO_RESPONSE,
    .sensorOk = false,
    .setpointDeciC = SETPOINT_DEFAULT_DECIC,
};
static rtos_mutex_t _mutexState = RTOS_MUTEX_INIT(TASK_CONSOLE);

/** Latest reading for the LCD task. */
static uint8_t _mbLcdStorage[sizeof(state_t)];
static rtos_mbox_t _mbLcd = RTOS_MBOX_INIT(_mbLcdStorage, sizeof(state_t));


/*
 * Function implementation
 */

static void printDeciC(int16_t deciC)
{
    if(deciC < 0)
    {
        uart_putc('-');
        deciC = (int16_t)-deciC;
    }
    uart_putU32((uint32_t)(deciC / 10));
    uart_putc('.');
    uart_putU32((uint32_t)(deciC % 10));
}


static void lcdPutDeciC(int16_t deciC)
{
    if(deciC < 0)
    {
        lcd_putc(&_lcd, '-');
        deciC = (int16_t)-deciC;
    }
    lcd_putU16(&_lcd, (uint16_t)(deciC / 10), 2);
    lcd_putc(&_lcd, '.');
    lcd_putU16(&_lcd, (uint16_t)(deciC % 10), 1);
}


static void taskSensor(void *arg)
{
    (void)arg;
    for(;;)
    {
        dht11_reading_t reading;
        dht11_status_t status;

        (void)rtos_waitPeriodic(RTOS_MS(SENSOR_PERIOD_MS));

        dht11_startRequest(&_pinDht11);
        rtos_delay(RTOS_MS(20));
        status = dht11_readResponse(&_pinDht11, &reading);

        rtos_mutexLock(&_mutexState);
        _state.sensorStatus = status;
        ++ _state.sensorReads;
        if(status == DHT11_OK)
        {
            _state.reading = reading;
            _state.sensorOk = true;
        }
        else
        {
            ++ _state.sensorErrors;
            _state.sensorOk = false;
        }
        (void)rtos_mboxPost(&_mbLcd, &_state);
        rtos_mutexUnlock(&_mutexState);
    }
}


static void taskControl(void *arg)
{
    (void)arg;
    gpio_output(&_pinRelay);
    gpio_output(&_pinLed);
    for(;;)
    {
        bool on;

        (void)rtos_waitPeriodic(RTOS_MS(CONTROL_PERIOD_MS));

        rtos_mutexLock(&_mutexState);
        on = _state.relayOn;
        if(!_state.sensorOk)
            on = false;                             /* Fail safe: no heating blind. */
        else if(_state.reading.tempDeciC < _state.setpointDeciC - HYSTERESIS_DECIC)
            on = true;
        else if(_state.reading.tempDeciC > _state.setpointDeciC + HYSTERESIS_DECIC)
            on = false;
        _state.relayOn = on;
        rtos_mutexUnlock(&_mutexState);

        gpio_write(&_pinRelay, on);
        gpio_write(&_pinLed, on);
    }
}


static void taskLcd(void *arg)
{
    (void)arg;
    lcd_init(&_lcd, 20, 4);
    lcd_setCursor(&_lcd, 0, 0);
    lcd_putsP(&_lcd, PSTR("bTool " RTOS_VERSION));

    for(;;)
    {
        state_t s;

        if(!rtos_mboxFetch(&_mbLcd, &s, RTOS_MS(1000)))
        {
            rtos_mutexLock(&_mutexState);
            s = _state;
            rtos_mutexUnlock(&_mutexState);
        }

        lcd_setCursor(&_lcd, 0, 1);
        lcd_putsP(&_lcd, PSTR("Temp "));
        if(s.sensorOk)
        {
            lcdPutDeciC(s.reading.tempDeciC);
            lcd_putsP(&_lcd, PSTR("C  Hum "));
            lcd_putU16(&_lcd, s.reading.humidityPct, 2);
            lcd_putc(&_lcd, '%');
        }
        else
            lcd_putsP(&_lcd, PSTR("-- sensor err  "));

        lcd_setCursor(&_lcd, 0, 2);
        lcd_putsP(&_lcd, PSTR("Set  "));
        lcdPutDeciC(s.setpointDeciC);
        lcd_putsP(&_lcd, s.relayOn ? PSTR("C  Heat ON ") : PSTR("C  Heat off"));

        lcd_setCursor(&_lcd, 0, 3);
        lcd_putsP(&_lcd, PSTR("Up "));
        lcd_putU16(&_lcd, (uint16_t)(rtos_getTick() / (60ul * RTOS_CFG_TICK_HZ)), 5);
        lcd_putsP(&_lcd, PSTR(" min  CPU "));
        lcd_putU16(&_lcd, rtos_getCpuLoad(), 3);
        lcd_putc(&_lcd, '%');
    }
}


static void printStatus(void)
{
    state_t s;
    uint8_t i;

    rtos_mutexLock(&_mutexState);
    s = _state;
    rtos_mutexUnlock(&_mutexState);

    uart_putsP(PSTR("t="));          uart_putU32(rtos_getTick() / RTOS_CFG_TICK_HZ);
    uart_putsP(PSTR("s temp="));
    if(s.sensorOk)
        printDeciC(s.reading.tempDeciC);
    else
    {
        uart_putsP(PSTR("err"));
        uart_putU32(s.sensorStatus);
    }
    uart_putsP(PSTR(" hum="));       uart_putU32(s.sensorOk ? s.reading.humidityPct : 0);
    uart_putsP(PSTR(" set="));       printDeciC(s.setpointDeciC);
    uart_putsP(PSTR(" relay="));     uart_putsP(s.relayOn ? PSTR("on") : PSTR("off"));
    uart_putsP(PSTR(" reads="));     uart_putU32(s.sensorReads);
    uart_putsP(PSTR(" errors="));    uart_putU32(s.sensorErrors);
    uart_putsP(PSTR(" cpu="));       uart_putU32(rtos_getCpuLoad());
    uart_putsP(PSTR("% run="));
    for(i=0; i<RTOS_NUM_TASKS; ++i)
    {
        if(i > 0)
            uart_putc('/');
        uart_putU32(rtos_getTaskRunTime(i) / (1000ul / RTOS_RUNTIME_UNIT_US));
    }
    uart_putsP(PSTR("ms stack="));
    for(i=1; i<RTOS_NUM_TASKS; ++i)
    {
        if(i > 1)
            uart_putc('/');
        uart_putU32(rtos_getStackFree(i));
    }
    uart_putsP(PSTR(" ovr="));
    for(i=1; i<RTOS_NUM_TASKS; ++i)
    {
        if(i > 1)
            uart_putc('/');
        uart_putU32(rtos_getOverrunCount(i, false));
    }
    uart_putsP(PSTR("\r\n"));
}


static void adjustSetpoint(int16_t deltaDeciC)
{
    rtos_mutexLock(&_mutexState);
    _state.setpointDeciC = (int16_t)(_state.setpointDeciC + deltaDeciC);
    if(_state.setpointDeciC < SETPOINT_MIN_DECIC)
        _state.setpointDeciC = SETPOINT_MIN_DECIC;
    if(_state.setpointDeciC > SETPOINT_MAX_DECIC)
        _state.setpointDeciC = SETPOINT_MAX_DECIC;
    (void)rtos_mboxPost(&_mbLcd, &_state);
    rtos_mutexUnlock(&_mutexState);
}


static void taskConsole(void *arg)
{
    rtos_tick_t nextStatus = RTOS_MS(STATUS_PERIOD_MS);
    (void)arg;

    for(;;)
    {
        int16_t c;

        (void)rtos_waitPeriodic(RTOS_MS(CONSOLE_PERIOD_MS));

        while((c = uart_getc()) >= 0)
        {
            switch((char)c)
            {
            case '+': adjustSetpoint(HYSTERESIS_DECIC); printStatus(); break;
            case '-': adjustSetpoint((int16_t)-HYSTERESIS_DECIC); printStatus(); break;
            case 's': printStatus(); break;
            case 'r':
                rtos_mutexLock(&_mutexState);
                _state.sensorReads = 0;
                _state.sensorErrors = 0;
                rtos_mutexUnlock(&_mutexState);
                break;
            case '?':
                uart_putsP(PSTR("bTool: + - setpoint, s status, r reset, ? help\r\n"));
                break;
            default:
                break;
            }
        }

        if((rtos_tickdiff_t)(rtos_getTick() - nextStatus) >= 0)
        {
            nextStatus += RTOS_MS(STATUS_PERIOD_MS);
            printStatus();
        }
    }
}


void rtos_assertFailed(const char *file, uint16_t line)
{
    cli();
    gpio_output(&_pinRelay);
    gpio_write(&_pinRelay, false);      /* Heater off before anything else. */
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
    uart_putsP(PSTR("\r\nbTool on nornOS " RTOS_VERSION "\r\n"));

    rtos_init();
    rtos_createTask(TASK_LCD,     taskLcd,     NULL, _stackLcd,     sizeof(_stackLcd));
    rtos_createTask(TASK_SENSOR,  taskSensor,  NULL, _stackSensor,  sizeof(_stackSensor));
    rtos_createTask(TASK_CONTROL, taskControl, NULL, _stackControl, sizeof(_stackControl));
    rtos_createTask(TASK_CONSOLE, taskConsole, NULL, _stackConsole, sizeof(_stackConsole));
    rtos_start();
    return 0;
}
