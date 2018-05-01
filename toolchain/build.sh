#!/bin/bash

set -e

. ../build/buildfuncs.sh

BINUTILSVER=binutils-2.30
GCCVERNUM=7.3.0
GCCVER=gcc-${GCCVERNUM}
PREFIX=`pwd`/cross-$GCCVERNUM
LOGDIR=`pwd`/logs
PATH="$PATH:$PREFIX/bin"

mkdir -p src "$PREFIX" "$LOGDIR"
cd src

# binutils key
recv_keys DD9E3C4F
# GCC key (richard guenther)
recv_keys FC26A641
GET ftp://ftp.nluug.nl/mirror/gnu/binutils "${BINUTILSVER}.tar.xz" 6e46b8aeae2f727a36f0bd9505e405768a72218f1796f0d09757d45209871ae6
GET ftp://ftp.nluug.nl/mirror/languages/gcc/releases/$GCCVER "${GCCVER}.tar.xz" 832ca6ae04636adbb430e865a1451adf6979ab44ca1c8374f61fba65645ce15c

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
