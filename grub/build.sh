#!/bin/bash
# Requires that you've already built the toolchain in toolchain/cross-X/bin

. ../build/buildfuncs.sh

CROSSVER=8.1.0
GRUBVER=grub-2.02
PREFIX=`pwd`/prefix-${GRUBVER}-${CROSSVER}
LOGDIR=`pwd`/logs
PATH="$PATH:$PREFIX/bin"
toolchainBin=`pwd`/../toolchain/cross-$CROSSVER/bin
[ -d "$toolchainBin" -a -x "$toolchainBin/$TARGET-gcc" ] ||
    die "$toolchainBin/$TARGET-gcc not found or not executable - build cross toolchain first?"
export PATH="$PATH:$toolchainBin"

set -e

mkdir -p src "$PREFIX" "$LOGDIR"

cd src

GET ftp://ftp.nluug.nl/mirror/gnu/grub/ "${GRUBVER}.tar.xz" 810b3798d316394f94096ec2797909dbf23c858e48f7b3830826b8daa06b7b0f
unpack "${GRUBVER}"

mkdir -p build-$GRUBVER-$CROSSVER
cd build-$GRUBVER-$CROSSVER
setlog grub_configure
# efiemu is disabled because it has -Werror flags not fixed by --disable-werror
CONFIGURE "$GRUBVER" -C --disable-werror --disable-efiemu --target=$TARGET --prefix="$PREFIX" TARGET_CC="ccache $TARGET-gcc" HOST_CC="ccache gcc"
setlog grub_build
MAKE
MAKE install
clearlog
cd ..

if ! command -v xorriso >/dev/null; then
    echo >&2 "Warning: xorriso is not installed - will be required for grub-mkrescue"
fi
