# R Spectrum for Haiku OS

[한국어](README.ko.md)

A real-time audio spectrum analyzer. It listens to the system's audio input and
draws the spectrum as bars on a logarithmic frequency axis.

The analysis is shared with the Linux and macOS builds in this repository; only
audio capture and drawing differ per platform.

## Requirements

Haiku (x86, x86_64 or arm64). Nothing outside the Haiku kits is needed.

## Install with pkgman

| Haiku | Commands |
| --- | --- |
| 32-bit x86 (x86_gcc2) | `pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2`<br>`pkgman install rspectrum` |
| x86_64 | `pkgman add-repo https://pkgman.rainygirl.com/x86_64`<br>`pkgman install rspectrum` |
| arm64 | `pkgman add-repo http://pkgman.rainygirl.com/arm64`<br>`pkgman install rspectrum` |

Then start **R Spectrum** from Deskbar -> Applications.

If `pkgman add-repo` fails with `Operation not supported`, the network kit of
that image has no TLS; use `http://` instead of `https://` in the address.

## Install from source

Run this on the Haiku machine itself.

```sh
cd haiku
./install.sh
```

It compiles the program, puts the binary in `~/config/non-packaged/apps/`, adds
it to **Deskbar -> Applications**, and puts a link on the Desktop.

```sh
./install.sh --build-only   # compile in place, install nothing
./install.sh --uninstall    # remove it again
```

## Other platforms

`linux/` and `macos/` hold the ports for those systems, each with its own
Makefile.

## License

MIT

## AI disclosure

This program was written with Claude.
