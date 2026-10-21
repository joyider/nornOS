#ifndef UART_INCLUDED
#define UART_INCLUDED
/**
 * @file uart.h
 * Minimal polling driver for UART0 of the ATmega2560, for logging from applications and
 * test cases. Not interrupt driven: a call blocks until the byte is in the transmit
 * register, so it is cheap for short messages and simple to use from any task. Shared use
 * from several tasks needs a mutex around whole messages.
 */

#include <stdint.h>
#include <avr/pgmspace.h>

/** Initialize UART0 for 8N1 at \a baud. */
void uart_init(uint32_t baud);

void uart_putc(char c);

/** Write a RAM string. */
void uart_puts(const char *s);

/** Write a flash string, e.g. uart_putsP(PSTR("hello")). */
void uart_putsP(const char *s);

/** Write an unsigned number in decimal. */
void uart_putU32(uint32_t n);

/** Write a signed number in decimal. */
void uart_putI32(int32_t n);

/** Write a byte as two hex digits. */
void uart_putHex8(uint8_t b);

/** Read a byte if one is available, else return -1. */
int16_t uart_getc(void);

#endif  /* UART_INCLUDED */
