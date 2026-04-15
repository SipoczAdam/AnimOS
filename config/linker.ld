ENTRY(_start)

SECTIONS {
    . = 1M;

    .multiboot_header : {
        KEEP(*(.multiboot_header))
    }

    /* Kód szekció */
    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(COMMON)
        *(.bss)
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}