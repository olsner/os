#!/bin/bash
# Get binutils

BINUTILSVER=binutils-2.28
GCCVERNUM=7.1.0
GCCVER=gcc-${GCCVERNUM}
TARGET=x86_64-elf
PREFIX=`pwd`/cross-$GCCVERNUM
LOGDIR=`pwd`/logs
PATH="$PATH:$PREFIX/bin"
MAKE="$(which make) -j$(nproc)"

set -e

GET() {
	wget -nc -c "$1/$2"
	gpg --verify ../${2}.sig $2
	sha256sum -c <<<"$3  $2"
}

unpack() {
	if [ ! -d "$1" ]; then
		echo "Unpacking $1..."
		if ! tar -xf "${1}.tar.bz2"; then
			rm -fr "$1"
			return 1
		fi
	fi
	if [ -n "$2" ]; then
		ln -sfT "$1" "$2"
	fi
}

log() {
	echo "$@"
	test -n "$logging" && eval 'echo "$@" '"$logging"
}

r() {
	log "RUNNING: $@ [in `pwd`]"
	local res=0
	eval '"$@" '"$logging" || res=$?
	if [ $res -ne 0 ]; then
		log "FAILED: $@"
		if [ -n "$logfile" ]; then
			log "[ logs in $logfile ]"
		fi
	fi
	return $res
}

setlog() {
	logfile="$LOGDIR/${1}.log"
	:> "$logfile"
	logging=">>$logfile 2>&1"
}
clearlog() {
	logging=
	logfile=
}

mkdir -p src "$PREFIX" "$LOGDIR"
cd src

# The keys used to sign binutils and gcc (in this release)
(gpg --list-key 4AE55E93 && gpg --list-key C3C45C06) &>/dev/null || gpg --recv-keys 4AE55E93 C3C45C06
GET ftp://ftp.nluug.nl/mirror/gnu/binutils "${BINUTILSVER}.tar.bz2" 6297433ee120b11b4b0a1c8f3512d7d73501753142ab9e2daa13c5a3edd32a72
GET ftp://ftp.nluug.nl/mirror/languages/gcc/releases/$GCCVER "${GCCVER}.tar.bz2" 8a8136c235f64c6fef69cac0d73a46a1a09bb250776a050aec8f9fc880bebc17

unpack "$BINUTILSVER"
unpack "$GCCVER"
#unpack "$MPFRVER" "$GCCVER/mpfr"
#unpack "$GMPVER" "$GCCVER/gmp"
#unpack "$MPCVER" "$GCCVER/mpc"

CONFIGURE() {
	local dir="$1"
	shift
	echo "$@" > config_args.tmp
	if [ -f Makefile ] && ! cmp -s config_args config_args.tmp; then
		echo "Configure arguments changed! Rerunning configure."
		rm -f Makefile
	fi
	mv config_args.tmp config_args
	if [ ! -f Makefile ]; then
		r "../$dir/configure" "$@"
	fi
}

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
