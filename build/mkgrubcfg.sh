#!/bin/bash

mkgrubcfg() {
    local kernel="$1"
    local menuname="$2"
    shift
    shift
    echo "submenu \"$menuname\" {"
        mkgrubcfg1 "$kernel" "$@"
    echo "}"
}

mkgrubcfg1() {
    local kernel="$1"
    shift

cat <<EOF
menuentry "shell" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/apic.mod
    module /cuser/ioapic.mod
    module /cuser/acpica.mod
    module /user/shell.mod
    boot
}

menuentry "lwIP" {
    multiboot /$kernel
    module /kern/irq.mod irq
    module /kern/pic.mod pic
    module /kern/console.mod
    module /cuser/apic.mod
    module /cuser/ioapic.mod
    module /cuser/acpica.mod
    module /cuser/e1000.mod e1000
    module /cuser/lwip.mod lwip
    boot
}

menuentry "fbtest" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/apic.mod
    module /cuser/ioapic.mod
    module /cuser/acpica.mod
    module /cuser/bochsvga.mod bochs
    module /cuser/fbtest.mod fbtest
    boot
}

menuentry "timer_test" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/apic.mod
    module /cuser/ioapic.mod
    module /cuser/acpica.mod acpica
    module /cuser/timer_test.mod
    boot
}

menuentry "ACPICA debugger" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/apic.mod
    module /cuser/ioapic.mod
    module /cuser/acpica.mod
    module /cuser/acpi_debugger.mod
    boot
}

menuentry "MP test" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/apic.mod
    module /cuser/ioapic.mod
    module /cuser/acpica.mod
    module /cuser/start_cpu.mod
    boot
}

menuentry "puts+xmm" {
    multiboot /$kernel
    module /user/test_puts.mod
    module /user/test_xmm.mod
    boot
}

EOF

while [ $# -gt 0 ]; do
    echo "menuentry \"${1#user/}\" {"
    echo "    multiboot /$kernel"
    echo "    module /$1.mod"
    echo "    boot"
    echo "}"
    shift
done

cat <<EOF
menuentry "idle" {
    multiboot /$kernel
    boot
}
EOF
}

if [ -f out/grub/kernel ]; then
    mkgrubcfg kernel "Rust kernel" "$@"
fi
mkgrubcfg kcpp "C++ kernel" "$@"
mkgrubcfg kstart.b "Assembly kernel" "$@"
