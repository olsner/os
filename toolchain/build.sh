#!/bin/bash

set -e

. ../build/buildfuncs.sh

BINUTILSVER=binutils-2.30
GCCVERNUM=8.1.0
GCCVER=gcc-${GCCVERNUM}
PREFIX=`pwd`/cross-$GCCVERNUM
LOGDIR=`pwd`/logs
PATH="$PATH:$PREFIX/bin"

mkdir -p src "$PREFIX" "$LOGDIR"
cd src

GET ftp://ftp.nluug.nl/mirror/gnu/binutils "${BINUTILSVER}.tar.xz" 6e46b8aeae2f727a36f0bd9505e405768a72218f1796f0d09757d45209871ae6
GET ftp://ftp.nluug.nl/mirror/languages/gcc/releases/$GCCVER "${GCCVER}.tar.xz" "1d1866f992626e61349a1ccd0b8d5253816222cdc13390dcfaa74b093aa2b153"

unpack "$BINUTILSVER"
unpack "$GCCVER"
#unpack "$MPFRVER" "$GCCVER/mpfr"
#unpack "$GMPVER" "$GCCVER/gmp"
#unpack "$MPCVER" "$GCCVER/mpc"

mkdir -p build-$BINUTILSVER
cd build-$BINUTILSVER
setlog binutils_configure
CONFIGURE "$BINUTILSVER" --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
setlog binutils_build
r $MAKE
r $MAKE install
clearlog
cd ..

# Check that binutils installed successfully and is in path
which -- $TARGET-as >/dev/null || echo $TARGET-as is not in the PATH

mkdir -p build-$GCCVER
cd build-$GCCVER
setlog gcc_configure
CONFIGURE "$GCCVER" --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers --disable-libstdcxx
setlog gcc_build
r $MAKE all-gcc all-target-libgcc
r $MAKE install-strip-gcc install-strip-target-libgcc
clearlog
cd ..
