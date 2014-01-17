ISO=${ISO-out/grub.iso}
make -j4 &&
kvm -m 32M "$@" -cdrom $ISO -netdev tap,id=vmnet0,script=no,ifname=tap0,downscript=no -device e1000,netdev=vmnet0
#kvm -m 32M "$@" -cdrom $ISO -netdev user,id=vmnet0 -redir tcp:5555::80 -device e1000,netdev=vmnet0
