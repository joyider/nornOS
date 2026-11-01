#ifndef HAL_ARCH_INCLUDED
#define HAL_ARCH_INCLUDED
/**
 * @file hal_arch.h
 * ESP32 specifics behind the generic HAL headers. Strings are ordinary DRAM strings, so
 * HAL_FSTR and PSTR are identity macros; delays spin on the CPU cycle counter.
 */

#include <stdint.h>
#include "port_regs.h"

#define HAL_FSTR(s)         (s)
#ifndef PSTR
# define PSTR(s)            (s)
#endif

static inline char hal_flashReadByte(const char *p)
{
    return *p;
}

static inline uint32_t hal_ccount(void)
{
    uint32_t c;
    __asm__ __volatile__ ("rsr %0, ccount" : "=a"(c));
    return c;
}

/** Busy wait. Interrupts stay enabled; the wait is at least \a us long. */
static inline void hal_delayUs(uint32_t us)
{
    const uint32_t start = hal_ccount();
    const uint32_t cycles = us * (uint32_t)(F_CPU / 1000000ul);
    while((uint32_t)(hal_ccount() - start) < cycles)
        ;
}

static inline void hal_delayMs(uint32_t ms)
{
    while(ms-- > 0)
        hal_delayUs(1000);
}

#endif  /* HAL_ARCH_INCLUDED */
