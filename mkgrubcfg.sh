#!/bin/bash

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

menuentry "irq+pic+console+newproc" {
    multiboot (cd)/kstart.b
    module (cd)/kern/irq.mod
    module (cd)/kern/pic.mod
    module (cd)/kern/console.mod
    module (cd)/user/newproc.mod
    boot
}
EOF
