#ifndef HAL_ARCH_INCLUDED
#define HAL_ARCH_INCLUDED
/**
 * @file hal_arch.h
 * AVR specifics behind the generic HAL headers: strings in flash and busy-wait delays.
 * Every architecture provides a hal_arch.h with the same names.
 *
 *   HAL_FSTR("text")        string literal in program memory; the AVR idiom PSTR works too
 *   hal_flashReadByte(p)    read one byte of such a string
 *   hal_delayUs(us)         busy wait, constant argument, interrupts stay as they are
 *   hal_delayMs(ms)         busy wait in milliseconds; prefer rtos_delay from a task
 */

#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#define HAL_FSTR(s)         PSTR(s)

static inline char hal_flashReadByte(const char *p)
{
    return (char)pgm_read_byte(p);
}

#define hal_delayUs(us)     _delay_us(us)
#define hal_delayMs(ms)     _delay_ms(ms)

#endif  /* HAL_ARCH_INCLUDED */
