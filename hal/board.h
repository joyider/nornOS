#ifndef BOARD_INCLUDED
#define BOARD_INCLUDED
/**
 * @file board.h
 * Board support: maps Arduino pin numbers to AVR ports for the boards nornOS runs on. The
 * board is chosen by the MCU the compiler targets (-mmcu, set from BOARD in the Makefile):
 *
 *   BOARD=mega2560   ATmega2560 / ATmega1280, Arduino Mega
 *   BOARD=uno        ATmega328P / ATmega168,  Arduino UNO, Nano, Pro Mini
 *
 * Applications use BOARD_PIN_Dn in place of GPIO_PIN(port, bit) to stay portable. Timer 2
 * (8 bit, present on both) is offered as the application timer; the kernel tick uses a 16
 * bit timer (RTOS_CFG_TIMER, default 3 on the Mega and 1 on the UNO).
 */

#include "gpio.h"

#if defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
# define BOARD_NAME     "Mega"
# define BOARD_IS_MEGA  1
# define BOARD_PIN_D0   GPIO_PIN(E, 0)
# define BOARD_PIN_D1   GPIO_PIN(E, 1)
# define BOARD_PIN_D2   GPIO_PIN(E, 4)
# define BOARD_PIN_D3   GPIO_PIN(E, 5)
# define BOARD_PIN_D4   GPIO_PIN(G, 5)
# define BOARD_PIN_D5   GPIO_PIN(E, 3)
# define BOARD_PIN_D6   GPIO_PIN(H, 3)
# define BOARD_PIN_D7   GPIO_PIN(H, 4)
# define BOARD_PIN_D8   GPIO_PIN(H, 5)
# define BOARD_PIN_D9   GPIO_PIN(H, 6)
# define BOARD_PIN_D10  GPIO_PIN(B, 4)
# define BOARD_PIN_D11  GPIO_PIN(B, 5)
# define BOARD_PIN_D12  GPIO_PIN(B, 6)
# define BOARD_PIN_D13  GPIO_PIN(B, 7)
#elif defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__) || defined(__AVR_ATmega168P__)
# define BOARD_NAME     "UNO"
# define BOARD_IS_UNO   1
# define BOARD_PIN_D0   GPIO_PIN(D, 0)
# define BOARD_PIN_D1   GPIO_PIN(D, 1)
# define BOARD_PIN_D2   GPIO_PIN(D, 2)
# define BOARD_PIN_D3   GPIO_PIN(D, 3)
# define BOARD_PIN_D4   GPIO_PIN(D, 4)
# define BOARD_PIN_D5   GPIO_PIN(D, 5)
# define BOARD_PIN_D6   GPIO_PIN(D, 6)
# define BOARD_PIN_D7   GPIO_PIN(D, 7)
# define BOARD_PIN_D8   GPIO_PIN(B, 0)
# define BOARD_PIN_D9   GPIO_PIN(B, 1)
# define BOARD_PIN_D10  GPIO_PIN(B, 2)
# define BOARD_PIN_D11  GPIO_PIN(B, 3)
# define BOARD_PIN_D12  GPIO_PIN(B, 4)
# define BOARD_PIN_D13  GPIO_PIN(B, 5)
#else
# error Unsupported MCU: add its pin map to hal/board.h
#endif

/** The on-board LED, Arduino pin 13 on both boards. */
#define BOARD_PIN_LED   BOARD_PIN_D13

/** Timer 2 in CTC mode runs at F_CPU / BOARD_TIMER2_PRESCALER counts per second. */
#define BOARD_TIMER2_PRESCALER  128ul

/** Compare value for a timer 2 interrupt rate of hz; the application should check with
    BOARD_TIMER2_HZ_IS_EXACT that the division has no remainder. */
#define BOARD_TIMER2_TOP(hz)          (F_CPU / BOARD_TIMER2_PRESCALER / (hz) - 1ul)
#define BOARD_TIMER2_HZ_IS_EXACT(hz)  ((F_CPU / BOARD_TIMER2_PRESCALER) % (hz) == 0 \
                                       && BOARD_TIMER2_TOP(hz) <= 255ul)

/**
 * Start timer 2 in CTC mode with prescaler 128 and enable its compare A interrupt. The
 * application provides RTOS_ISR(TIMER2_COMPA_vect).
 *   @param top Compare value, normally BOARD_TIMER2_TOP(hz).
 */
static inline void board_timer2StartCtc(uint8_t top)
{
    TCCR2A = _BV(WGM21);
    TCCR2B = 0;
    TCNT2  = 0;
    OCR2A  = top;
    TIFR2  = _BV(OCF2A);
    TIMSK2 = _BV(OCIE2A);
    TCCR2B = _BV(CS22) | _BV(CS20);
}

#endif  /* BOARD_INCLUDED */
