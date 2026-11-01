/**
 * @file port_clock.c
 * CPU clock of the ESP32. The ROM leaves the CPU on the 40 MHz crystal; this switches
 * it to the PLL for 80, 160 or 240 MHz, following the sequence of Espressif's rtc_clk.c
 * (Apache-2.0): power up the PLL, program it through the internal I2C bus with the ROM's
 * rom_i2c_writeReg, select the divider in DPORT and the PLL as system clock source. The
 * APB bus then runs at 80 MHz regardless of the CPU frequency.
 *
 * F_CPU selects the frequency at compile time (port/xtensa/arch.mk, CPU_MHZ). 80 and
 * 160 MHz (320 MHz PLL) are verified stable on the board. 240 MHz (480 MHz PLL) runs
 * and tc04 measures 1.9 us per context switch there, but every application crashes at
 * random within seconds with corrupted registers or wild loads; something in the
 * 480 MHz sequence or the supply settings is still missing, so 240 is experimental.
 */

#include "rtos.h"


/*
 * Defines
 */

#define RTC_CNTL_OPTIONS0       PORT_REG(DR_REG_RTCCNTL_BASE + 0x00)
#define RTC_CNTL_CLK_CONF       PORT_REG(DR_REG_RTCCNTL_BASE + 0x70)
#define RTC_CNTL_REG            PORT_REG(DR_REG_RTCCNTL_BASE + 0x7C)
#define RTC_CNTL_STORE5         PORT_REG(DR_REG_RTCCNTL_BASE + 0xB4)
#define DPORT_CPU_PER_CONF      PORT_REG(DR_REG_DPORT_BASE + 0x3C)

#define RTC_CNTL_BIAS_I2C_FORCE_PD  (1ul << 18)
#define RTC_CNTL_BBPLL_FORCE_PD     (1ul << 10)
#define RTC_CNTL_BBPLL_I2C_FORCE_PD (1ul << 8)
#define RTC_CNTL_BB_I2C_FORCE_PD    (1ul << 6)

#define RTC_CNTL_SOC_CLK_SEL_SHIFT  27          /* 0 XTAL, 1 PLL, 2 8M, 3 APLL */
#define RTC_CNTL_SOC_CLK_SEL_MASK   (3ul << 27)
#define RTC_CNTL_DIG_DBIAS_WAK_SHIFT 11         /* 3 bits */
#define RTC_CNTL_DBIAS_1V10         4ul
#define RTC_CNTL_DBIAS_1V25         7ul

#define UART_STATUS             PORT_REG(DR_REG_UART_BASE + 0x1C)

/* The PLL registers behind the internal I2C bus, block 0x66, host 4. */
#define I2C_BBPLL               0x66u
#define I2C_BBPLL_HOSTID        4u
#define I2C_BBPLL_IR_CAL_DELAY  0u
#define I2C_BBPLL_IR_CAL_EXT_CAP 1u
#define I2C_BBPLL_OC_LREF       2u
#define I2C_BBPLL_OC_DIV_7_0    3u
#define I2C_BBPLL_OC_ENB_FCAL   4u
#define I2C_BBPLL_OC_DCUR       5u
#define I2C_BBPLL_OC_ENB_VCON   6u
#define I2C_BBPLL_BBADC_DSM     9u
#define I2C_BBPLL_ENDIV5        11u
#define I2C_BBPLL_BBADC_CAL_7_0 12u

/** ROM function: write one register of an analog block over the internal I2C bus. */
typedef void (*rom_i2c_writeReg_t)(uint8_t block, uint8_t hostId, uint8_t reg, uint8_t data);
#define rom_i2c_writeReg        ((rom_i2c_writeReg_t)0x400041a4)


/*
 * Function implementation
 */

static void delayXtalUs(uint32_t us)
{
    /* Still on the 40 MHz crystal here. */
    const uint32_t start = rtos_port_ccount();
    while((uint32_t)(rtos_port_ccount() - start) < us * 40ul)
        ;
}


void rtos_port_clockInit(void)
{
#if F_CPU == 40000000ul
    /* Crystal: nothing to do. */
#else
    const bool pll480 = (F_CPU == 240000000ul);
    uint32_t lref, div, dcur, sel;

    /* Let the ROM's last log line out before the UART clock changes. */
    while(((UART_STATUS >> 16) & 0xFFu) != 0)
        ;

    /* Digital supply for 240 MHz, PLL and its I2C interface powered. */
    RTC_CNTL_REG = (RTC_CNTL_REG & ~(7ul << RTC_CNTL_DIG_DBIAS_WAK_SHIFT))
                   | ((pll480 ? RTC_CNTL_DBIAS_1V25 : RTC_CNTL_DBIAS_1V10) << RTC_CNTL_DIG_DBIAS_WAK_SHIFT);
    RTC_CNTL_OPTIONS0 &= ~(RTC_CNTL_BIAS_I2C_FORCE_PD | RTC_CNTL_BB_I2C_FORCE_PD
                           | RTC_CNTL_BBPLL_FORCE_PD | RTC_CNTL_BBPLL_I2C_FORCE_PD);

    /* Calibration and PLL mode (320 MHz for 80/160, 480 MHz for 240), 40 MHz crystal. */
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_IR_CAL_DELAY, 0x18);
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_IR_CAL_EXT_CAP, 0x20);
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_OC_ENB_FCAL, 0x9a);
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_OC_ENB_VCON, 0x00);
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_BBADC_CAL_7_0, 0x00);
    delayXtalUs(3);                     /* supply settled before the PLL is programmed */
    if(pll480)
    {
        rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_ENDIV5, 0xc3);
        rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_BBADC_DSM, 0x74);
        lref = (1u << 7) | 0u;          /* lref 1, div10_8 0, div_ref 0 */
        div = 28;
        dcur = (3u << 6) | 6u;          /* bw 3, dcur 6 */
    }
    else
    {
        rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_ENDIV5, 0x43);
        rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_BBADC_DSM, 0x4c);
        lref = 0u;
        div = 32;
        dcur = (3u << 6) | 6u;
    }
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_OC_LREF, (uint8_t)lref);
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_OC_DIV_7_0, (uint8_t)div);
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, I2C_BBPLL_OC_DCUR, (uint8_t)dcur);
    delayXtalUs(1000);                  /* PLL lock; Espressif waits 80 us, be generous */

    /* CPU divider from the PLL: 0 = 80, 1 = 160, 2 = 240 MHz; then PLL as source. */
    sel = (F_CPU == 80000000ul) ? 0u : (F_CPU == 160000000ul) ? 1u : 2u;
    DPORT_CPU_PER_CONF = (DPORT_CPU_PER_CONF & ~3ul) | sel;
    RTC_CNTL_CLK_CONF = (RTC_CNTL_CLK_CONF & ~RTC_CNTL_SOC_CLK_SEL_MASK) | (1ul << RTC_CNTL_SOC_CLK_SEL_SHIFT);
    RTC_CNTL_STORE5 = 80ul | (80ul << 16);  /* APB MHz for the ROM, as the IDF stores it */
#endif
}
