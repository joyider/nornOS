#ifndef BOARD_INCLUDED
#define BOARD_INCLUDED
/**
 * @file board.h
 * Board support for the ESP32 boards nornOS runs on. BOARD=esp32 is a 38 pin
 * NodeMCU-32S style devkit with an ESP32-S module, CP2102 on UART0 and the LED on GPIO2.
 * Pins are plain GPIO numbers: GPIO_PIN(2).
 *
 * Application timer: timer group 0, timer 0, routed to CPU interrupt 17. The application
 * defines RTOS_ISR(BOARD_APP_TIMER_VECT) and calls board_appTimerStart(hz).
 */

#include "gpio.h"

#define BOARD_NAME      "ESP32"
#define BOARD_IS_ESP32  1

#define BOARD_PIN_LED   GPIO_PIN(2)
#define BOARD_PIN_BOOT  GPIO_PIN(0)     /* the IO0 button, active low */

#ifndef HAL_APB_HZ
# define HAL_APB_HZ     F_CPU
#endif

#define BOARD_APP_TIMER_VECT            board_appTimer
#define BOARD_APP_TIMER_PRESCALER       2ul
#define BOARD_APP_TIMER_HZ_IS_EXACT(hz) ((HAL_APB_HZ / BOARD_APP_TIMER_PRESCALER) % (hz) == 0)

void board_appTimerStart(uint32_t hz);

/** Counts of the application timer since its last interrupt, for latency measurements;
    BOARD_APP_TIMER_COUNT_HZ counts per second. */
#define BOARD_APP_TIMER_COUNT_HZ    (HAL_APB_HZ / BOARD_APP_TIMER_PRESCALER)
uint32_t board_appTimerElapsed(void);

#endif  /* BOARD_INCLUDED */
