#!/bin/sh
# Runs inside the cross-compile container (see tools/cross-build.sh), in a
# directory holding haiku/ and common/. Leaves ./R Spectrum without resources;
# those are added on Haiku by the pkgman recipe.
set -e

CXXFLAGS="-O2 -Wall -Wno-multichar -Wno-unused-parameter"

case "$1" in
x86_64)
	CXX="x86_64-unknown-haiku-g++"
	;;
*)
	echo "usage: $0 x86_64" >&2
	exit 1
	;;
esac

$CXX $CXXFLAGS -o "R Spectrum" \
	haiku/RSpectrum.cpp common/FFT.cpp common/Analyzer.cpp \
	-lbe -lmedia -llocalestub -lroot -lm -Xlinker -soname=_APP_
