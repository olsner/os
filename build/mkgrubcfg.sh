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
menuentry "lwIP" {
    multiboot /$kernel
    module /kern/irq.mod irq
    module /kern/pic.mod pic
    module /kern/console.mod console
    module /cuser/apic.mod APIC
    module /cuser/ioapic.mod IOAPIC
    module /cuser/acpica.mod ACPICA
    module /cuser/e1000.mod e1000
    module /cuser/lwip.mod lwip
    boot
}

menuentry "shell" {
    multiboot /$kernel
    module /kern/irq.mod irq
    module /kern/pic.mod pic
    module /kern/console.mod console
    module /cuser/apic.mod APIC
    module /cuser/ioapic.mod IOAPIC
    module /cuser/acpica.mod ACPICA
    module /user/shell.mod
    boot
}

menuentry "fbtest" {
    multiboot /$kernel
    module /kern/irq.mod irq
    module /kern/pic.mod pic
    module /kern/console.mod console
    module /cuser/apic.mod APIC
    module /cuser/ioapic.mod IOAPIC
    module /cuser/acpica.mod ACPICA
    module /cuser/bochsvga.mod bochs
    module /cuser/fbtest.mod fbtest
    boot
}

menuentry "timer_test" {
    multiboot /$kernel
    module /kern/irq.mod irq
    module /kern/pic.mod pic
    module /kern/console.mod console
    module /cuser/apic.mod APIC
    module /cuser/ioapic.mod IOAPIC
    module /cuser/acpica.mod ACPICA
    module /cuser/timer_test.mod
    boot
}

menuentry "ACPICA debugger" {
    multiboot /$kernel
    module /kern/irq.mod irq
    module /kern/pic.mod pic
    module /kern/console.mod console
    module /cuser/apic.mod APIC
    module /cuser/ioapic.mod IOAPIC
    module /cuser/acpica.mod ACPICA
    module /cuser/acpi_debugger.mod
    boot
}

menuentry "puts+xmm" {
    multiboot /$kernel
    module /user/test_puts.mod test_puts
    module /user/test_xmm.mod test_xmm
    boot
}

EOF

while [ $# -gt 0 ]; do
    echo "menuentry \"${1#user/}\" {"
    echo "    multiboot /$kernel"
    echo "    module /$1.mod $1"
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
