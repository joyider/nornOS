#ifndef HD44780_INCLUDED
#define HD44780_INCLUDED
/**
 * @file hd44780.h
 * Character LCD driver (HD44780 compatible) in 4 bit mode, write only. Replaces the
 * Arduino LiquidCrystal library. Delays are busy waits, so call it from a low priority
 * task.
 */

#include <stdint.h>
#include <avr/pgmspace.h>
#include "gpio.h"

typedef struct hd44780
{
    gpio_pin_t rs, e, d4, d5, d6, d7;
    uint8_t cols, rows;
} hd44780_t;

/** Initialize the display. Takes about 60 ms. */
void lcd_init(hd44780_t *pLcd, uint8_t cols, uint8_t rows);
void lcd_clear(const hd44780_t *pLcd);
void lcd_setCursor(const hd44780_t *pLcd, uint8_t col, uint8_t row);
void lcd_putc(const hd44780_t *pLcd, char c);
void lcd_puts(const hd44780_t *pLcd, const char *s);
void lcd_putsP(const hd44780_t *pLcd, const char *s);

/** Write an unsigned number right aligned in \a width characters. */
void lcd_putU16(const hd44780_t *pLcd, uint16_t n, uint8_t width);

#endif  /* HD44780_INCLUDED */
