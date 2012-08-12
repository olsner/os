make out/grub.iso &&
kvm -m 32M "$@" -cdrom out/grub.iso
