/**
 * @file hd44780.c
 * Character LCD driver in 4 bit mode.
 */

#include "delay.h"
#include "hd44780.h"

#define CMD_CLEAR           0x01u
#define CMD_ENTRY_MODE      0x06u   /* Increment, no shift. */
#define CMD_DISPLAY_ON      0x0Cu   /* Display on, cursor off, blink off. */
#define CMD_FUNCTION_SET    0x28u   /* 4 bit, 2 lines, 5x8 font. */
#define CMD_SET_DDRAM       0x80u

static void pulseEnable(const hd44780_t *pLcd)
{
    gpio_write(&pLcd->e, true);
    hal_delayUs(1);
    gpio_write(&pLcd->e, false);
    hal_delayUs(40);
}

static void writeNibble(const hd44780_t *pLcd, uint8_t nibble)
{
    gpio_write(&pLcd->d4, (nibble & 0x01u) != 0);
    gpio_write(&pLcd->d5, (nibble & 0x02u) != 0);
    gpio_write(&pLcd->d6, (nibble & 0x04u) != 0);
    gpio_write(&pLcd->d7, (nibble & 0x08u) != 0);
    pulseEnable(pLcd);
}

static void writeByte(const hd44780_t *pLcd, uint8_t b, bool isData)
{
    gpio_write(&pLcd->rs, isData);
    writeNibble(pLcd, b >> 4);
    writeNibble(pLcd, b & 0x0Fu);
}

static void command(const hd44780_t *pLcd, uint8_t cmd)
{
    writeByte(pLcd, cmd, false);
}

void lcd_init(hd44780_t *pLcd, uint8_t cols, uint8_t rows)
{
    pLcd->cols = cols;
    pLcd->rows = rows;
    gpio_output(&pLcd->rs);
    gpio_output(&pLcd->e);
    gpio_output(&pLcd->d4);
    gpio_output(&pLcd->d5);
    gpio_output(&pLcd->d6);
    gpio_output(&pLcd->d7);
    gpio_write(&pLcd->rs, false);
    gpio_write(&pLcd->e, false);

    /* Power-on reset sequence from the data sheet, figure 24. */
    hal_delayMs(50);
    writeNibble(pLcd, 0x03);
    hal_delayMs(5);
    writeNibble(pLcd, 0x03);
    hal_delayMs(5);
    writeNibble(pLcd, 0x03);
    hal_delayUs(150);
    writeNibble(pLcd, 0x02);        /* Now in 4 bit mode. */

    command(pLcd, CMD_FUNCTION_SET);
    command(pLcd, CMD_DISPLAY_ON);
    lcd_clear(pLcd);
    command(pLcd, CMD_ENTRY_MODE);
}

void lcd_clear(const hd44780_t *pLcd)
{
    command(pLcd, CMD_CLEAR);
    hal_delayMs(2);
}

void lcd_setCursor(const hd44780_t *pLcd, uint8_t col, uint8_t row)
{
    static const uint8_t rowOffset[4] = {0x00, 0x40, 0x14, 0x54};

    if(row >= pLcd->rows)
        row = (uint8_t)(pLcd->rows - 1u);
    command(pLcd, (uint8_t)(CMD_SET_DDRAM | (col + rowOffset[row & 3u])));
}

void lcd_putc(const hd44780_t *pLcd, char c)
{
    writeByte(pLcd, (uint8_t)c, true);
}

void lcd_puts(const hd44780_t *pLcd, const char *s)
{
    while(*s != '\0')
        lcd_putc(pLcd, *s++);
}

void lcd_putsP(const hd44780_t *pLcd, const char *s)
{
    char c;
    while((c = (char)hal_flashReadByte(s++)) != '\0')
        lcd_putc(pLcd, c);
}

void lcd_putU16(const hd44780_t *pLcd, uint16_t n, uint8_t width)
{
    char buf[6];
    uint8_t i = sizeof(buf) - 1;

    buf[i] = '\0';
    do
    {
        buf[--i] = (char)('0' + n % 10u);
        n /= 10u;
    }
    while(n != 0  &&  i > 0);
    while((uint8_t)(sizeof(buf) - 1 - i) < width  &&  i > 0)
        buf[--i] = ' ';
    lcd_puts(pLcd, &buf[i]);
}
