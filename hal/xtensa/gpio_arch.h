#ifndef GPIO_ARCH_INCLUDED
#define GPIO_ARCH_INCLUDED
/**
 * @file gpio_arch.h
 * ESP32 implementation of the GPIO API in hal/gpio.h. A pin is a GPIO number 0..39.
 * Output and direction use the W1TS/W1TC registers, which are atomic in hardware. The
 * IO_MUX of the pad is switched to the GPIO function, and the GPIO matrix is left at its
 * reset state where GPIO_OUT drives the pad directly.
 *
 * GPIO 34..39 are input only and have no pull-ups.
 */

#include <stdint.h>
#include <stdbool.h>
#include "port_regs.h"

typedef struct gpio_pin
{
    uint8_t num;
} gpio_pin_t;

#define GPIO_PIN(n)  { (n) }

#define GPIO_OUT_W1TS       PORT_REG(DR_REG_GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC       PORT_REG(DR_REG_GPIO_BASE + 0x0C)
#define GPIO_OUT1_W1TS      PORT_REG(DR_REG_GPIO_BASE + 0x14)
#define GPIO_OUT1_W1TC      PORT_REG(DR_REG_GPIO_BASE + 0x18)
#define GPIO_ENABLE_W1TS    PORT_REG(DR_REG_GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC    PORT_REG(DR_REG_GPIO_BASE + 0x28)
#define GPIO_ENABLE1_W1TS   PORT_REG(DR_REG_GPIO_BASE + 0x30)
#define GPIO_ENABLE1_W1TC   PORT_REG(DR_REG_GPIO_BASE + 0x34)
#define GPIO_OUT            PORT_REG(DR_REG_GPIO_BASE + 0x04)
#define GPIO_OUT1           PORT_REG(DR_REG_GPIO_BASE + 0x10)
#define GPIO_IN             PORT_REG(DR_REG_GPIO_BASE + 0x3C)
#define GPIO_IN1            PORT_REG(DR_REG_GPIO_BASE + 0x40)

#define IO_MUX_MCU_SEL_GPIO (2u << 12)
#define IO_MUX_FUN_IE       (1u << 9)
#define IO_MUX_FUN_WPU      (1u << 8)
#define IO_MUX_FUN_DRV_20MA (2u << 10)

/** IO_MUX register offset of each GPIO pad (0 = pad does not exist). */
static const uint8_t gpio_ioMuxOffset[40] =
{
    0x44, 0x88, 0x40, 0x84, 0x48, 0x6C, 0x60, 0x64, 0x68, 0x54,   /* 0..9 */
    0x58, 0x5C, 0x34, 0x38, 0x30, 0x3C, 0x4C, 0x50, 0x70, 0x74,   /* 10..19 */
    0x78, 0x7C, 0x80, 0x8C, 0x00, 0x24, 0x28, 0x2C, 0x00, 0x00,   /* 20..29 */
    0x00, 0x00, 0x1C, 0x20, 0x14, 0x18, 0x04, 0x08, 0x0C, 0x10,   /* 30..39 */
};

static inline void gpio_setIoMux(uint8_t num, uint32_t flags)
{
    PORT_REG(DR_REG_IO_MUX_BASE + gpio_ioMuxOffset[num]) = IO_MUX_MCU_SEL_GPIO | flags;
}

static inline void gpio_output(const gpio_pin_t *p)
{
    gpio_setIoMux(p->num, IO_MUX_FUN_DRV_20MA);
    if(p->num < 32)
        GPIO_ENABLE_W1TS = 1ul << p->num;
    else
        GPIO_ENABLE1_W1TS = 1ul << (p->num - 32);
}

static inline void gpio_input(const gpio_pin_t *p, bool pullup)
{
    gpio_setIoMux(p->num, IO_MUX_FUN_IE | (pullup ? IO_MUX_FUN_WPU : 0u));
    if(p->num < 32)
        GPIO_ENABLE_W1TC = 1ul << p->num;
    else
        GPIO_ENABLE1_W1TC = 1ul << (p->num - 32);
}

static inline void gpio_write(const gpio_pin_t *p, bool high)
{
    if(p->num < 32)
    {
        if(high) GPIO_OUT_W1TS = 1ul << p->num; else GPIO_OUT_W1TC = 1ul << p->num;
    }
    else
    {
        if(high) GPIO_OUT1_W1TS = 1ul << (p->num - 32); else GPIO_OUT1_W1TC = 1ul << (p->num - 32);
    }
}

static inline bool gpio_read(const gpio_pin_t *p)
{
    if(p->num < 32)
        return (GPIO_IN & (1ul << p->num)) != 0;
    return (GPIO_IN1 & (1ul << (p->num - 32))) != 0;
}

static inline bool gpio_readOutput(const gpio_pin_t *p)
{
    if(p->num < 32)
        return (GPIO_OUT & (1ul << p->num)) != 0;
    return (GPIO_OUT1 & (1ul << (p->num - 32))) != 0;
}

/** Read-modify-write on the output latch; the W1TS/W1TC write itself is atomic, but a
    concurrent toggle of the same pin from an interrupt would be lost. */
static inline void gpio_toggle(const gpio_pin_t *p)
{
    gpio_write(p, !gpio_readOutput(p));
}

#endif  /* GPIO_ARCH_INCLUDED */
