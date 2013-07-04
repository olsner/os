make out/grub.iso &&
kvm -m 32M "$@" -cdrom out/grub.iso -net nic,model=e1000 -net user
