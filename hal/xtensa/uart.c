/**
 * @file uart.c
 * Polling driver for UART0 of the ESP32. The ROM has already configured the pins and the
 * UART (8N1, 115200 from the crystal), so uart_init only sets the divider. The UART is
 * clocked from the APB clock, which equals the crystal until a PLL is configured.
 */

#include "uart.h"

#define UART_FIFO       PORT_REG(DR_REG_UART_BASE + 0x00)
#define UART_CLKDIV     PORT_REG(DR_REG_UART_BASE + 0x14)
#define UART_STATUS     PORT_REG(DR_REG_UART_BASE + 0x1C)
#define UART_CONF0      PORT_REG(DR_REG_UART_BASE + 0x20)

#define UART_TXFIFO_CNT(status)   (((status) >> 16) & 0xFFu)
#define UART_RXFIFO_CNT(status)   ((status) & 0xFFu)
#define UART_TICK_REF_ALWAYS_ON   (1ul << 27)
#define UART_FIFO_DEPTH           128u

#ifndef HAL_APB_HZ
# define HAL_APB_HZ     F_CPU
#endif

void uart_init(uint32_t baud)
{
    /* Divider in 1/16 steps: integer part bits 0..19, fraction bits 20..23. The ROM left
       8N1 at roughly 115200 (divider 341.5) and its last log line may still be in the
       FIFO, so let that drain before the divider changes. */
    const uint32_t div16 = (HAL_APB_HZ * 16ul + baud / 2ul) / baud;

    uart_flush();
    UART_CONF0 |= UART_TICK_REF_ALWAYS_ON;
    UART_CLKDIV = (div16 >> 4) | ((div16 & 0x0Fu) << 20);
}

void uart_putc(char c)
{
    while(UART_TXFIFO_CNT(UART_STATUS) >= UART_FIFO_DEPTH - 1u)
        ;
    UART_FIFO = (uint8_t)c;
}

int16_t uart_getc(void)
{
    if(UART_RXFIFO_CNT(UART_STATUS) != 0)
        return (int16_t)(UART_FIFO & 0xFFu);
    return -1;
}

/** Wait until every queued byte has left the FIFO, e.g. before changing the baud rate. */
void uart_flush(void)
{
    while(UART_TXFIFO_CNT(UART_STATUS) != 0)
        ;
}
