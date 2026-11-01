/*
 * nornOS on the ESP32 without a second stage bootloader: the ROM loader reads the image at
 * flash offset 0x1000 and copies every segment straight into IRAM and DRAM, then jumps to
 * _start. Nothing runs from flash, so no MMU or cache setup is needed.
 *
 * IRAM starts at 0x40080400: the first kilobyte is left to the ROM loader, and 0x40080400
 * is 1 KB aligned as VECBASE requires. DRAM ends well below 0x3FFE0000, where the ROM keeps
 * its own data and the stack it runs the loader on. With two cores (AMP) each core gets
 * its own image and half of the memory; see mem_core0.ld and mem_core1.ld.
 */

/* The MEMORY block comes from mem_single.ld, mem_core0.ld or mem_core1.ld: arch.mk
   concatenates the chosen one with this file into the build directory. */

ENTRY(_start)

SECTIONS
{
    .vectors :
    {
        KEEP(*(.vectors))
    } > iram

    .text :
    {
        *(.entry.literal)
        *(.entry)
        *(.literal .text .literal.* .text.*)
    } > iram

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > dram

    .data :
    {
        . = ALIGN(4);
        *(.data .data.*)
        . = ALIGN(4);
    } > dram

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > dram

    /* Kept across a reset: not cleared by _start. */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit .noinit.*)
        . = ALIGN(4);
    } > dram

    /* The idle task (main) runs on the top of DRAM. */
    _heap_start = .;
    _stack_top = ORIGIN(dram) + LENGTH(dram);

    /DISCARD/ :
    {
        *(.xt.lit .xt.lit.*)
        *(.xt.prop .xt.prop.*)
        *(.comment)
        *(.eh_frame)
    }
}
