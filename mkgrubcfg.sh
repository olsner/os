#!/bin/bash

cat <<EOF
menuentry "irq+pic+console+shell" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/user/shell.mod
    boot
}

menuentry "lwIP" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/acpica.mod
    module (cd)/cuser/e1000.mod
    module (cd)/cuser/lwip.mod
    boot
}

menuentry "user-apic" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/apic.mod
    module (cd)/cuser/timer_test.mod
    boot
}

menuentry "zeropage+test_maps" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/zeropage.mod
    module (cd)/cuser/test_maps.mod
    boot
}

menuentry "ACPICA" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/acpica.mod
    boot
}

menuentry "puts+xmm" {
    multiboot (cd)/kstart.b
    module (cd)/user/test_puts.mod
    module (cd)/user/test_xmm.mod
    boot
}

EOF

while [ $# -gt 0 ]; do
	echo "menuentry \"${1#user/}\" {"
	echo "    multiboot (cd)/kstart.b"
	echo "    module (cd)/$1.mod"
	echo "    boot"
	echo "}"
	shift
done

cat <<EOF
menuentry "idle" {
    multiboot (cd)/kstart.b
    boot
}
EOF
