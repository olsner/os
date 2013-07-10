make -j4 out/grub.iso &&
kvm -m 32M "$@" -cdrom out/grub.iso -netdev tap,id=tap0,script=no,ifname=tap0,downscript=no -device e1000,netdev=tap0
