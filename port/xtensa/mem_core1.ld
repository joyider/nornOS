/* AMP, APP CPU image: second half of IRAM, upper DRAM below the shared 8 KB. */
MEMORY
{
    iram (RWX) : ORIGIN = 0x40090000, LENGTH = 0x10000
    dram (RW)  : ORIGIN = 0x3FFC8000, LENGTH = 0x14000
}
_shared_start = 0x3FFDC000;
_shared_size = 0x2000;
