/**
 * @file board.c
 * ESP32 board support: the application timer on timer group 0, timer 0.
 */

#include "rtos.h"
#include "board.h"

#define TIMG0_T0CONFIG      PORT_REG(DR_REG_TIMG0_BASE + 0x00)
#define TIMG0_T0LO          PORT_REG(DR_REG_TIMG0_BASE + 0x04)
#define TIMG0_T0UPDATE      PORT_REG(DR_REG_TIMG0_BASE + 0x0C)
#define TIMG0_T0ALARMLO     PORT_REG(DR_REG_TIMG0_BASE + 0x10)
#define TIMG0_T0ALARMHI     PORT_REG(DR_REG_TIMG0_BASE + 0x14)
#define TIMG0_T0LOADLO      PORT_REG(DR_REG_TIMG0_BASE + 0x18)
#define TIMG0_T0LOADHI      PORT_REG(DR_REG_TIMG0_BASE + 0x1C)
#define TIMG0_T0LOAD        PORT_REG(DR_REG_TIMG0_BASE + 0x20)
#define TIMG0_INT_ENA       PORT_REG(DR_REG_TIMG0_BASE + 0x98)
#define TIMG0_INT_CLR       PORT_REG(DR_REG_TIMG0_BASE + 0xA4)

#define T0_EN               (1ul << 31)
#define T0_INCREASE         (1ul << 30)
#define T0_AUTORELOAD       (1ul << 29)
#define T0_DIVIDER_SHIFT    13
#define T0_LEVEL_INT_EN     (1ul << 11)
#define T0_ALARM_EN         (1ul << 10)
#define T0_INT              (1ul << 0)

#define SRC_TG0_T0_LEVEL    14u
#define CPU_INT_APP_TIMER   17u

void board_appTimer_rtosBody(void);

static void appTimerIsr(void)
{
    TIMG0_INT_CLR = T0_INT;
    TIMG0_T0CONFIG |= T0_ALARM_EN;      /* the alarm disarms itself when it fires */
    board_appTimer_rtosBody();
}

void board_appTimerStart(uint32_t hz)
{
    const uint32_t alarm = HAL_APB_HZ / BOARD_APP_TIMER_PRESCALER / hz;

    TIMG0_T0CONFIG = 0;
    TIMG0_T0LOADLO = 0;
    TIMG0_T0LOADHI = 0;
    TIMG0_T0LOAD = 1;
    TIMG0_T0ALARMLO = alarm;
    TIMG0_T0ALARMHI = 0;
    TIMG0_INT_CLR = T0_INT;
    TIMG0_INT_ENA |= T0_INT;
    rtos_port_attachInterrupt(CPU_INT_APP_TIMER, SRC_TG0_T0_LEVEL, appTimerIsr);
    TIMG0_T0CONFIG = T0_EN | T0_INCREASE | T0_AUTORELOAD
                     | (BOARD_APP_TIMER_PRESCALER << T0_DIVIDER_SHIFT)
                     | T0_LEVEL_INT_EN | T0_ALARM_EN;
}

uint32_t board_appTimerElapsed(void)
{
    TIMG0_T0UPDATE = 1;                 /* latch the counter into T0LO/T0HI */
    return TIMG0_T0LO;
}
