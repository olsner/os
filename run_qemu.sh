make grub.iso &&
kvm -m 32M "$@" -cdrom grub.iso
