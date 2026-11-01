#ifndef GPIO_INCLUDED
#define GPIO_INCLUDED
/**
 * @file gpio.h
 * GPIO API shared by all architectures. The pin type and the functions are defined by
 * hal/<arch>/gpio_arch.h; boards name their pins in hal/<arch>/board.h.
 *
 *   gpio_pin_t                   compile time description of one pin
 *   gpio_output(p)               make the pin an output
 *   gpio_input(p, pullup)        make the pin an input, optionally with pull-up
 *   gpio_write(p, high)          drive the output
 *   gpio_read(p)                 sample the input
 *   gpio_toggle(p)               invert the output
 * All functions are atomic with respect to interrupts and other tasks.
 */

#include <stdint.h>
#include <stdbool.h>
#include "gpio_arch.h"

#endif  /* GPIO_INCLUDED */
