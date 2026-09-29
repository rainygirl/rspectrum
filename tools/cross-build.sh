#!/bin/sh
# Cross-compile R Spectrum for x86_64 Haiku on a workstation with Docker,
# producing dist/x86_64/"R Spectrum" without resources. Resources and
# attributes are added on Haiku by the pkgman recipe.
#
#   tools/cross-build.sh x86_64
#
# The sources go in and the binary comes out through tar over stdin: a bind
# mount from /private/tmp does not reach Docker on macOS.
set -e

ARCH="${1:-x86_64}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HERE/dist/$ARCH"
mkdir -p "$OUT"

case "$ARCH" in
x86_64)
	( cd "$HERE" && tar cf - haiku common tools/cross-compile.sh ) \
		| docker run --rm -i --platform linux/amd64 \
			haiku/cross-compiler:x86_64-r1beta4 \
			sh -c 'mkdir -p /work && cd /work && tar xf - && sh tools/cross-compile.sh x86_64 >&2 && tar cf - "R Spectrum"' \
		| tar xf - -C "$OUT"
	;;
*)
	echo "usage: $0 x86_64" >&2
	exit 1
	;;
esac

file "$OUT/R Spectrum"
