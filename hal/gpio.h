#ifndef GPIO_INCLUDED
#define GPIO_INCLUDED
/**
 * @file gpio.h
 * Tiny GPIO abstraction: a pin is a port triple plus a bit mask, defined at compile time
 * with GPIO_PIN(B, 7) for PB7 (Arduino Mega pin 13). Writes are atomic with respect to
 * interrupts and other tasks, because a port write on AVR is a read-modify-write.
 *
 * board.h maps Arduino pin numbers to GPIO_PIN for the supported boards.
 */

#include <stdint.h>
#include <stdbool.h>
#include <avr/io.h>
#include <avr/interrupt.h>

typedef struct gpio_pin
{
    volatile uint8_t *pPort;
    volatile uint8_t *pDdr;
    volatile uint8_t *pPin;
    uint8_t mask;
} gpio_pin_t;

#define GPIO_PIN(port, bit)  { &PORT##port, &DDR##port, &PIN##port, (uint8_t)(1u << (bit)) }

static inline void gpio_atomicSet(volatile uint8_t *pReg, uint8_t mask, bool set)
{
    const uint8_t s = SREG;
    cli();
    if(set)
        *pReg |= mask;
    else
        *pReg &= (uint8_t)~mask;
    SREG = s;
}

static inline void gpio_output(const gpio_pin_t *p)
{
    gpio_atomicSet(p->pDdr, p->mask, true);
}

static inline void gpio_input(const gpio_pin_t *p, bool pullup)
{
    gpio_atomicSet(p->pDdr, p->mask, false);
    gpio_atomicSet(p->pPort, p->mask, pullup);
}

static inline void gpio_write(const gpio_pin_t *p, bool high)
{
    gpio_atomicSet(p->pPort, p->mask, high);
}

static inline bool gpio_read(const gpio_pin_t *p)
{
    return (*p->pPin & p->mask) != 0;
}

/** Toggle by writing the PIN register, which is atomic in hardware. */
static inline void gpio_toggle(const gpio_pin_t *p)
{
    *p->pPin = p->mask;
}

#endif  /* GPIO_INCLUDED */
