#ifndef UART_INCLUDED
#define UART_INCLUDED
/**
 * @file uart.h
 * Minimal polling driver for the console UART (UART0), for logging from applications and
 * test cases. Implemented per architecture in hal/<arch>/uart.c. Not interrupt driven: a call blocks until the byte is in the transmit
 * register, so it is cheap for short messages and simple to use from any task. Shared use
 * from several tasks needs a mutex around whole messages.
 */

#include <stdint.h>
#include "hal_arch.h"

/** Initialize UART0 for 8N1 at \a baud. */
void uart_init(uint32_t baud);

void uart_putc(char c);

/** Write a RAM string. */
void uart_puts(const char *s);

/** Write a string in program memory, e.g. uart_putsP(HAL_FSTR("hello")). */
void uart_putsP(const char *s);

/** Write an unsigned number in decimal. */
void uart_putU32(uint32_t n);

/** Write a signed number in decimal. */
void uart_putI32(int32_t n);

/** Write a byte as two hex digits. */
void uart_putHex8(uint8_t b);

/** Read a byte if one is available, else return -1. */
int16_t uart_getc(void);

/** Block until every queued byte has been sent. */
void uart_flush(void);

#endif  /* UART_INCLUDED */
