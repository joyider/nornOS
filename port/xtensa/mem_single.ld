/* One core: all of the usable IRAM and DRAM. */
MEMORY
{
    iram (RWX) : ORIGIN = 0x40080400, LENGTH = 0x1FC00
    dram (RW)  : ORIGIN = 0x3FFB0000, LENGTH = 0x2C000
}
_shared_start = 0;
_shared_size = 0;
