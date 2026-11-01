/**
 * @file uart.c
 * Polling driver for UART0 of the ATmega2560/328P: init, one byte out, one byte in. The
 * formatting functions are in hal/uart_fmt.c.
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

void uart_flush(void)
{
    while((UCSR0A & _BV(UDRE0)) == 0)
        ;
}

int16_t uart_getc(void)
{
    if(UCSR0A & _BV(RXC0))
        return (int16_t)UDR0;
    return -1;
}
