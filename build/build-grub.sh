#!/bin/sh
# Prerequisites:
# - grub unpacked/checked out in grub/src
# - toolchain built in toolchain/cross-X/bin
# - (on Mac) objconv needs to be in path

# objconv:
# $ git clone https://github.com/vertis/objconv
# $ cd objconv
# $ g++ -o objconv -O2 src/*.cpp
# Then put the compiled objconv in path

die() {
    echo >&2 "$@"
    exit 1
}

set -e

SYSTEM=`uname -s`
if [ "$SYSTEM" = Darwin ]; then
    NPROC=`sysctl -n hw.activecpu`
else
    NPROC=`nproc`
fi

cd ../grub
toolchainBin=../toolchain/cross-7.1.0/bin
target=x86_64-elf

[ -d "$toolchainBin" -a -x "$toolchainBin/$target-gcc" ] ||
    die "$toolchainBin/$target-gcc not found or not executable - build cross toolchain first?"

export PATH="$PATH:$(realpath "$toolchainBin")"

mkdir -p prefix
absPrefix=$(realpath prefix)

configureArgs=(
    -C
    --disable-werror
    --target=$target
    --prefix="$absPrefix"
)

mkdir -p build
cd build
../src/configure "${configureArgs[@]}"
make -j$NPROC
make install

if ! command -v xorriso >/dev/null; then
    echo >&2 "Warning: xorriso is not installed - will be required for grub-mkrescue"
fi
