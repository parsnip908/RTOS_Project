/* linker file for the TM4C Launchpad */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

_stack_size = 4K;

SECTIONS
{
    .text :
    {
        _text = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        KEEP (*(.init))
        KEEP (*(.fini))
        /*_etext = .;*/
    } > FLASH

    /*
    * ARM.ex{tab,idx} :
    * Information to unwindle the stack
    * See -funwind-tables or -fexceptions flag in ld
    *
    */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    _exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    _exidx_end = .;

    _etext   = .;



    .data : AT (_etext) /*AT(ADDR(.text) + SIZEOF(.text))*/
    {
        _data = .;
        _ldata = LOADADDR (.data);
        *(vtable)
        *(.data*)
        _edata = .;
    } > SRAM

    .bss :
    {
        _bss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > SRAM


    __stack_bottom = ALIGN(8);
    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
}