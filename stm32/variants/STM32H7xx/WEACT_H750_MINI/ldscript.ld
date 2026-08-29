/*
 * WEACT-H750-MINI application linker script.
 *
 * The application does NOT live in internal flash. The STM32H750's 128 KB of
 * internal flash is a single erase sector and belongs entirely to the
 * bootloader, so the application is linked for, and executes in place (XiP)
 * from, the external QSPI flash (W25Q64, 8 MB, memory mapped at 0x90000000).
 *
 *   0x90000000  4K       TAG sector    written by the bootloader after it has
 *                                      verified the image. NOT part of this
 *                                      image - it is a separate 4 KB erase
 *                                      sector so that updating the tag cannot
 *                                      destroy the vector table.
 *   0x90001000  1K       app vectors   _fw_flash_begin, and what VTOR points at
 *   0x90001400  1K       firm_ver_t    the .version section
 *   0x90001800  8M - 6K  app code
 *
 * Because the addresses are fixed by the bootloader's memory map, this script
 * hardcodes them instead of using the LD_FLASH_OFFSET / LD_MAX_SIZE defsyms
 * that platform.txt passes in. upload.maximum_size in boards.txt still bounds
 * the sketch size.
 *
 * The bootloader reads firm_ver_t to learn the image size, computes the CRC
 * itself and promotes the image to a verified TAG on first boot, so no
 * post-build tagging tool is needed. _fw_flash_begin / _fw_flash_end below are
 * what feed that.
 */

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM_D1) + LENGTH(RAM_D1);    /* end of RAM_D1: 0x24080000 */
/* Generate a link error if heap and stack don't fit into RAM_D1 */
_Min_Heap_Size = 0x200;  /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Specify the memory areas */
MEMORY
{
  VECTOR (rx)    : ORIGIN = 0x90001000, LENGTH = 1K
  VER (rx)       : ORIGIN = 0x90001400, LENGTH = 1K
  FLASH (rx)     : ORIGIN = 0x90001800, LENGTH = 8M - 6K
  RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1 (xrw)   : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2 (xrw)   : ORIGIN = 0x30000000, LENGTH = 288K
  RAM_D3 (xrw)   : ORIGIN = 0x38000000, LENGTH = 64K
  ITCMRAM (xrw)  : ORIGIN = 0x00000000, LENGTH = 64K
}

_fw_flash_begin = ORIGIN(VECTOR);

/* Define output sections */
SECTIONS
{
  /* The vector table goes first, at the address the bootloader jumps to */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >VECTOR

  /* firm_ver_t, read by the bootloader to identify and size the image */
  .version :
  {
    . = ALIGN(4);
    KEEP(*(.version))
    . = ALIGN(4);
  } >VER

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM (READONLY) : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array (READONLY) :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array (READONLY) :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array (READONLY) :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM_D1, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >RAM_D1 AT> FLASH

  /* End of the loadable image. firm_ver_t.firm_size is derived from this, and
   * the bootloader CRCs exactly this range. */
  _fw_flash_end = LOADADDR(.data) + SIZEOF(.data);

  /* The size is exported as its own absolute symbol because a static C
   * initializer cannot hold the difference of two symbol addresses - that
   * needs two relocations, and the linker quietly folds it to zero. Taking the
   * address of this one symbol is a single relocation and does work. */
  _fw_flash_size = _fw_flash_end - _fw_flash_begin;

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM_D1

  /* User_heap_stack section, used to check that there is enough RAM_D1 left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM_D1

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
