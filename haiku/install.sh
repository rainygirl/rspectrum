#!/bin/sh
#
# Builds R Spectrum and installs it. Run this on the Haiku machine itself.
#
# Usage:
#   ./install.sh              build and install
#   ./install.sh --build-only build in place, install nothing
#   ./install.sh --uninstall  remove it again

set -e

APP="R Spectrum"
SRC="RSpectrum.cpp ../common/FFT.cpp ../common/Analyzer.cpp"
RDEF="RSpectrum.rdef"
LIBS="-lbe -lmedia -llocalestub -lroot -lm"

APPS_DIR="$HOME/config/non-packaged/apps"
MENU_DIR="$HOME/config/non-packaged/data/deskbar/menu/Applications"
DESKTOP_DIR="$HOME/Desktop"

cd "$(dirname "$0")"

if [ "$(uname -s)" != "Haiku" ]; then
	echo "This builds a Haiku application; run it on Haiku." >&2
	exit 1
fi

if [ "$1" = "--uninstall" ]; then
	rm -f "$APPS_DIR/$APP" "$MENU_DIR/$APP" "$DESKTOP_DIR/$APP"
	echo "Removed $APP."
	exit 0
fi

echo "Compiling..."
g++ -O2 -o "$APP" $SRC $LIBS

# The resource file is not optional: B_SINGLE_LAUNCH and the icon both live
# there, and an app signature attached with addattr is cleared by mimeset -F.
echo "Attaching resources..."
rc -o RSpectrum.rsrc "$RDEF"
xres -o "$APP" RSpectrum.rsrc
mimeset -f "$APP"
rm -f RSpectrum.rsrc

if [ "$1" = "--build-only" ]; then
	echo "Built ./$APP (not installed)."
	exit 0
fi

echo "Installing..."
mkdir -p "$APPS_DIR" "$MENU_DIR" "$DESKTOP_DIR"

# Never overwrite a copy that is still running: replacing the file under a
# live process invalidates its code pages and it dies in unrelated places.
quit application/x-vnd.RSpectrum >/dev/null 2>&1 || true
sleep 2
# The bracket keeps this grep from matching its own command line.
if ps | grep -q "[R] Spectrum"; then
	echo "$APP is still running -- close it and run this again." >&2
	exit 1
fi

cp -f "$APP" "$APPS_DIR/$APP"

# Tracker draws a blank document icon unless two attributes are right, and cp
# sets neither: recent Haiku no longer sniffs ELF files, so the copy inherits
# BEOS:TYPE "application/octet-stream" and carries no BEOS:ICON at all. resattr
# copies the icon out of the binary's own resources, and BEOS:TYPE has to say
# this is an executable.
resattr -O -o "$APPS_DIR/$APP" "$APPS_DIR/$APP" 2>/dev/null \
	|| echo "install.sh: resattr failed; the icon may show as a blank document" >&2
addattr -t mime BEOS:TYPE application/x-vnd.be-elfexecutable "$APPS_DIR/$APP" 2>/dev/null || true
ln -sf "$APPS_DIR/$APP" "$MENU_DIR/$APP"
ln -sf "$APPS_DIR/$APP" "$DESKTOP_DIR/$APP"
rm -f "$APP"

echo "Installed to $APPS_DIR/$APP"
echo "Find it in Deskbar -> Applications -> $APP, or on the Desktop."
