#!/bin/bash

cat <<EOF
menuentry "irq+pic+console" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    boot
}

menuentry "irq+pic+console+shell" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/user/shell.mod
    boot
}

menuentry "ACPICA+e1000" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/cuser/acpica.mod
    module (cd)/cuser/e1000.mod
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
