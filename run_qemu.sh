ISO=${ISO-out/grub.iso}
make -j4 $ISO &&
kvm -m 32M "$@" -cdrom $ISO -netdev tap,id=tap0,script=no,ifname=tap0,downscript=no -device e1000,netdev=tap0
