#!/bin/sh
# Runs inside the cross-compile container (see tools/cross-build.sh), in a
# directory holding haiku/ and common/. Leaves ./R Spectrum without resources;
# those are added on Haiku by the pkgman recipe.
set -e

CXXFLAGS="-O2 -Wall -Wno-multichar -Wno-unused-parameter"

LINKFLAGS=""

case "$1" in
x86_64)
	CXX="x86_64-unknown-haiku-g++"
	;;
arm64)
	# The Haiku arm64 build tree has no populated sysroot: point one at the
	# haiku_devel package contents, and link against the haiku package's
	# libraries and gcc_syslibs' libstdc++.
	CXX="$GEN/cross-tools-arm64/bin/aarch64-unknown-haiku-g++"
	P="$GEN/objects/haiku/arm64/packaging/packages_build/minimum"
	SYSLIBS="$(ls -d "$GEN"/build_packages/gcc_syslibs-*-arm64/lib | head -1)"
	mkdir -p sysroot/boot/system lib
	ln -sfn "$P/hpkg_-haiku_devel.hpkg/contents/develop" sysroot/boot/system/develop
	ln -sfn "$SYSLIBS/libstdc++.so.6" lib/libstdc++.so
	ln -sfn "$SYSLIBS/libgcc_s.so.1" lib/libgcc_s.so
	CXX="$CXX --sysroot=$PWD/sysroot"
	# The minimum image profile leaves libmedia out of the haiku package, but
	# the build tree still has it.
	LINKFLAGS="-L$P/hpkg_-haiku.hpkg/contents/lib -L$PWD/lib"
	LINKFLAGS="$LINKFLAGS -L$GEN/objects/haiku/arm64/release/kits/media"
	;;
*)
	echo "usage: $0 x86_64|arm64" >&2
	exit 1
	;;
esac

$CXX $CXXFLAGS -o "R Spectrum" \
	haiku/RSpectrum.cpp common/FFT.cpp common/Analyzer.cpp $LINKFLAGS \
	-lbe -lmedia -llocalestub -lroot -lm -Xlinker -soname=_APP_
