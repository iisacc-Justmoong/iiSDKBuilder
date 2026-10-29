ENTRY(Reset_Handler)
MEMORY { FLASH (rx) : ORIGIN = 0, LENGTH = 256K
         RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K }
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
 .text : { KEEP(*(.isr_vector)) *(.text*) *(.rodata*) } > FLASH
 .ARM.exidx : { *(.ARM.exidx*) } > FLASH
 .data : { _data_start = .; *(.data*) _data_end = .; } > RAM AT > FLASH
 _data_load = LOADADDR(.data);
 .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(COMMON) _bss_end = .; } > RAM
 /DISCARD/ : { *(.comment*) *(.note*) }
}
