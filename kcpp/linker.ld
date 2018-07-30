ENTRY(start32_mboot)
OUTPUT_FORMAT(binary)

high_org = 0xffffffffc0000000;

MEMORY {
    ram : org = 0x100000, l = 20K
    high : org = 0xffffffffc0100000, l = 20K
}

SECTIONS {
    . = 0x100000;

    .text32 : {
        start32.o(.text*);
    } >ram AT>ram

    .text64 (. + high_org) : {
        *(.text*);
    } >high AT>ram

    .data32 (. - high_org) : {
        start32.o(.data*);
        start32.o(.rodata*);
    } >ram AT>ram

    .data64 (. + high_org) : {
        *(.rodata*);
        *(.data*);
        __CTOR_LIST__ = .;
        KEEP(*(.ctors));
        __CTOR_END__ = .;
        PROVIDE(section.data.end = . - 0xffffffffc0000000);
    } >high AT>ram

    .bss32 (. - high_org) : {
        start32.o(.bss*);
    } >ram AT>ram

    .bss64 (. + high_org) : {
        *(.bss*);
    } >high AT>ram

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.rel.eh_frame)
    }
}
