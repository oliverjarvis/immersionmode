OUTPUT_ARCH(arm)
ENTRY(jpr_init)

MEMORY {
    payload (rwx) : ORIGIN = 0x0FE70000, LENGTH = 0x00090000
}

SECTIONS {
    .text : {
        KEEP(*(.text.jpr_init))
        KEEP(*(.text.tramp))
        *(.text .text.*)
    } > payload
    .rodata : { *(.rodata .rodata.*) } > payload
    .data : { *(.data .data.*) } > payload
    . = ALIGN(4);
    __jpr_image_end = .;
    .bss (NOLOAD) : {
        __jpr_bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __jpr_bss_end = .;
    } > payload
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) }
}

ASSERT(__jpr_bss_end <= ORIGIN(payload) + LENGTH(payload), "payload exceeds its 576 KB budget")
ASSERT(__jpr_image_end - ORIGIN(payload) <= 0x30000, "payload image exceeds the 192 KB stage")
