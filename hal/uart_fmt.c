/**
 * @file uart_fmt.c
 * The architecture independent half of the console driver: string and number output on
 * top of uart_putc from hal/<arch>/uart.c.
 */

#include "uart.h"

void uart_puts(const char *s)
{
    while(*s != '\0')
        uart_putc(*s++);
}

void uart_putsP(const char *s)
{
    char c;
    while((c = hal_flashReadByte(s++)) != '\0')
        uart_putc(c);
}

void uart_putU32(uint32_t n)
{
    char buf[11];
    uint8_t i = sizeof(buf) - 1;

    buf[i] = '\0';
    do
    {
        buf[--i] = (char)('0' + n % 10u);
        n /= 10u;
    }
    while(n != 0);
    uart_puts(&buf[i]);
}

void uart_putI32(int32_t n)
{
    if(n < 0)
    {
        uart_putc('-');
        n = -n;
    }
    uart_putU32((uint32_t)n);
}

void uart_putHex8(uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]);
    uart_putc(hex[b & 0x0Fu]);
}
