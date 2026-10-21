/**
 * @file uart.c
 * Minimal polling driver for UART0 of the ATmega2560.
 */

#include <avr/io.h>
#include "uart.h"

void uart_init(uint32_t baud)
{
    const uint16_t ubrr = (uint16_t)((F_CPU + 8ul * baud) / (16ul * baud) - 1ul);

    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;
    UCSR0A = 0;
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

void uart_putc(char c)
{
    while((UCSR0A & _BV(UDRE0)) == 0)
        ;
    UDR0 = (uint8_t)c;
}

void uart_puts(const char *s)
{
    while(*s != '\0')
        uart_putc(*s++);
}

void uart_putsP(const char *s)
{
    char c;
    while((c = (char)pgm_read_byte(s++)) != '\0')
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

int16_t uart_getc(void)
{
    if(UCSR0A & _BV(RXC0))
        return (int16_t)UDR0;
    return -1;
}
