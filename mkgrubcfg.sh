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
    multiboot (cd)/$kernel
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/user/shell.mod
    boot
}

menuentry "lwIP" {
    multiboot (cd)/$kernel
    module (cd)/kern/irq.mod irq
    module (cd)/kern/pic.mod pic
    module (cd)/kern/console.mod console
    module (cd)/cuser/acpica.mod acpica
    module (cd)/cuser/e1000.mod e1000
    module (cd)/cuser/apic.mod apic
    module (cd)/cuser/lwip.mod lwip
    boot
}

menuentry "user-apic" {
    multiboot (cd)/$kernel
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/apic.mod
    module (cd)/cuser/timer_test.mod
    boot
}

menuentry "zeropage+test_maps" {
    multiboot (cd)/$kernel
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/zeropage.mod
    module (cd)/cuser/test_maps.mod
    boot
}

menuentry "ACPICA" {
    multiboot (cd)/$kernel
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/acpica.mod
    boot
}

menuentry "puts+xmm" {
    multiboot (cd)/$kernel
    module (cd)/user/test_puts.mod
    module (cd)/user/test_xmm.mod
    boot
}

EOF

while [ $# -gt 0 ]; do
    echo "menuentry \"${1#user/}\" {"
    echo "    multiboot (cd)/$kernel"
    echo "    module (cd)/$1.mod"
    echo "    boot"
    echo "}"
    shift
done

cat <<EOF
menuentry "idle" {
    multiboot (cd)/$kernel
    boot
}
EOF
}

if [ -f out/grub/kernel ]; then
    mkgrubcfg kernel " (rust)" "$@"
fi
mkgrubcfg kstart.b "" "$@"
