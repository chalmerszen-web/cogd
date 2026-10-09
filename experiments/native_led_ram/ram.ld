/* IRAM and DRAM share SRAM; physical regions below do not overlap.
 * ESP32-C3 TRM: System and Memory. ROM uses the high end of DRAM.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
    iram (rx) : ORIGIN = 0x40380000, LENGTH = 16K
    dram (rw) : ORIGIN = 0x3fc84000, LENGTH = 8K
    stack (rw) : ORIGIN = 0x3fc86000, LENGTH = 8K
}
SECTIONS {
    .text : ALIGN(4) {
        KEEP(*(.text.start))
        *(.text .text.*)
        . = ALIGN(4);
    } > iram
    /* Constants need data-port byte access, so keep them in DRAM. */
    .data : ALIGN(4) {
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
    } > dram
    __global_pointer$ = ORIGIN(dram) + 0x800;
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > dram
    __stack_bottom = ORIGIN(stack);
    __stack_top = ORIGIN(stack) + LENGTH(stack);
    /DISCARD/ : { *(.eh_frame .eh_frame.* .comment) }
}
/* Espressif esp32c3.rom.ld: immutable on-chip ROM helper, no IDF library. */
disable_default_watchdog = 0x400000a4;
ASSERT(SIZEOF(.text) <= LENGTH(iram), "IRAM overflow")
ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= LENGTH(dram), "DRAM overflow")

