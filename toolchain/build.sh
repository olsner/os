#!/bin/bash

set -e

. ../build/buildfuncs.sh

BINUTILSVER=binutils-2.28
GCCVERNUM=7.1.0
GCCVER=gcc-${GCCVERNUM}
PREFIX=`pwd`/cross-$GCCVERNUM
LOGDIR=`pwd`/logs
PATH="$PATH:$PREFIX/bin"

mkdir -p src "$PREFIX" "$LOGDIR"
cd src

# The keys used to sign binutils and gcc (in this release)
recv_keys 4AE55E93 C3C45C06
# TODO Get the .xz versions instead :)
GET ftp://ftp.nluug.nl/mirror/gnu/binutils "${BINUTILSVER}.tar.bz2" 6297433ee120b11b4b0a1c8f3512d7d73501753142ab9e2daa13c5a3edd32a72
GET ftp://ftp.nluug.nl/mirror/languages/gcc/releases/$GCCVER "${GCCVER}.tar.bz2" 8a8136c235f64c6fef69cac0d73a46a1a09bb250776a050aec8f9fc880bebc17

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
