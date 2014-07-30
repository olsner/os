#!/bin/bash

mkgrubcfg() {
    local kernel="$1"
    local namesuffix="$2"
    shift
    shift
    mkgrubcfg1 "$kernel" "$@" |
    sed 's/^\(menuentry.*\)\(" {\)/\1'"$namesuffix"'\2/'
}

mkgrubcfg1() {
    local kernel="$1"
    shift

cat <<EOF
menuentry "irq+pic+console+shell" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /user/shell.mod
    boot
}

menuentry "lwIP" {
    multiboot /$kernel
    module /kern/irq.mod irq
    module /kern/pic.mod pic
    module /kern/console.mod console
    module /cuser/acpica.mod acpica
    module /cuser/e1000.mod e1000
    module /cuser/apic.mod apic
    module /cuser/lwip.mod lwip
    boot
}

menuentry "fbtest" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/acpica.mod acpica
    module /cuser/bochsvga.mod bochs
    module /cuser/apic.mod
    module /cuser/fbtest.mod fbtest
    boot
}

menuentry "timer_test" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/apic.mod
    module /cuser/timer_test.mod
    boot
}

menuentry "zeropage+test_maps" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/zeropage.mod
    module /cuser/test_maps.mod
    boot
}

menuentry "ACPICA" {
    multiboot /$kernel
    module /kern/irq.mod
    module /kern/pic.mod
    module /kern/console.mod
    module /cuser/acpica.mod
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
    mkgrubcfg kernel " (rust)" "$@"
fi
mkgrubcfg kstart.b "" "$@"
