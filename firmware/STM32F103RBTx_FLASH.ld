/* STM32F103RB medium density. Startup symbols match the STM32CubeF1 CMSIS startup. */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

PHDRS
{
    flash PT_LOAD FLAGS(5);
    ram   PT_LOAD FLAGS(6);
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack_size = 0x400;

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH :flash

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);
    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH :ram

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM :ram

    .stack_reserve (NOLOAD) : ALIGN(8)
    {
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . += _min_stack_size;
    } > RAM :ram

    /DISCARD/ : { *(.note.GNU-stack) }
}
