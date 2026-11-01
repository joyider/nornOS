/**
 * @file hello.c
 * Board bring-up without the scheduler: prints a banner and a line per second on UART0,
 * blinks the LED and echoes received characters. The per second cadence, compared with a
 * wall clock, confirms F_CPU; the echo confirms the receive path.
 *
 * On the ESP32 this is also the first image loaded by the ROM without a second stage
 * bootloader, so it proves the linker script, the vector table and the startup code.
 */

#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "delay.h"

static const gpio_pin_t _pinLed = BOARD_PIN_LED;

/** A few nested calls so that the register windows overflow and underflow at least once;
    a wrong vector table shows up here as a crash rather than later in the kernel. */
static uint32_t nest(uint32_t depth, uint32_t acc)
{
    volatile uint32_t keep = acc + depth;
    if(depth == 0)
        return keep;
    return nest(depth - 1, keep) + 1u;
}

int main(void)
{
    uint32_t seconds = 0;

    uart_init(115200);
    uart_puts("\r\nnornOS " RTOS_VERSION " hello on " BOARD_NAME "\r\n");
    uart_puts("F_CPU=");            uart_putU32((uint32_t)F_CPU);
    uart_puts(" nest=");            uart_putU32(nest(40, 0));
    uart_puts("\r\n");

    gpio_output(&_pinLed);
    for(;;)
    {
        uint32_t i;
        int16_t c;

        gpio_write(&_pinLed, true);
        for(i=0; i<500; ++i)
        {
            hal_delayMs(1);
            while((c = uart_getc()) >= 0)
            {
                uart_puts("echo ");
                uart_putc((char)c);
                uart_puts("\r\n");
            }
        }
        gpio_write(&_pinLed, false);
        hal_delayMs(500);

        ++ seconds;
        uart_puts("t=");            uart_putU32(seconds);
        uart_puts("s us=");         uart_putU32(rtos_getTimeUs());
        uart_puts("\r\n");
    }
    return 0;
}
