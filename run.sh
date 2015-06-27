#!/bin/sh

testmod=out/grub/test.mod
exec qemu-system-x86_64 -m 32M -kernel out/kernel -initrd "$testmod asdf,$testmod jkl,$testmod 123" "$@"
