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

set -e

SYSTEM=`uname -s`
if [ "$SYSTEM" = Darwin ]; then
    NPROC=`sysctl -n hw.activecpu`
else
    NPROC=`nproc`
fi

cd ../grub
export PATH="$PATH":"$(realpath ../toolchain/cross-7.1.0/bin)"

mkdir -p prefix
absPrefix="$(realpath prefix)"
target=x86_64-elf

mkdir -p build
cd build
../src/configure --disable-werror TARGET_CC=$target-gcc TARGET_OBJCOPY=$target-objcopy TARGET_STRIP=$target-strip TARGET_NM=$target-nm TARGET_RANLIB=$target-ranlib --target=$target --prefix="$absPrefix" -C
make -j$NPROC
make install

if ! command -v xorriso >/dev/null; then
    echo >&2 "Warning: xorriso is not installed - will be required for grub-mkrescue"
fi
