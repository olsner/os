ISO=${ISO-out/grub.iso}
make -j4 &&
#NETDEV=${NETDEV-tap,id=vmnet0,script=no,ifname=tap0,downscript=no}
NETDEV="user,id=vmnet0 -redir tcp:5555::80"
kvm -m 32M "$@" -cdrom $ISO -netdev $NETDEV -device e1000,netdev=vmnet0
