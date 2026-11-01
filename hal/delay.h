#ifndef DELAY_INCLUDED
#define DELAY_INCLUDED
/**
 * @file delay.h
 * Busy-wait delays for drivers that need microsecond timing: hal_delayUs(us) and
 * hal_delayMs(ms), provided by hal/<arch>/hal_arch.h. The argument must be a constant on
 * AVR. Tasks should use rtos_delay for anything longer than a few tens of microseconds.
 */

#include "hal_arch.h"

#endif  /* DELAY_INCLUDED */
