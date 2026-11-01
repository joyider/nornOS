/* AMP, PRO CPU image: first half of IRAM, lower DRAM. The 8 KB at the top of the DRAM
   window are shared with the APP CPU (hal/xtensa/amp.c). */
MEMORY
{
    iram (RWX) : ORIGIN = 0x40080400, LENGTH = 0x0FC00
    dram (RW)  : ORIGIN = 0x3FFB0000, LENGTH = 0x18000
}
_shared_start = 0x3FFDC000;
_shared_size = 0x2000;
