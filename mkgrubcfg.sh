#!/bin/bash

cat <<EOF
menuentry "test" {
    multiboot (cd)/kernel
    module (cd)/test.mod asdf
    module (cd)/test.mod jkl
    module (cd)/test.mod 123
    boot
}

EOF
