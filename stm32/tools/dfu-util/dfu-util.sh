#!/bin/sh -
#
# Picks a dfu-util that can talk DfuSe.
#
# Burning the bootloader over the STM32 ROM bootloader needs -s (--dfuse-address)
# to say where the image goes. STM32Tools bundles dfu-util, but not the same one
# everywhere:
#
#   macOS          0.11       universal, x86_64 + arm64      -s works
#   Linux x86_64   0.8                                       -s works
#   Linux aarch64  0.11-dev                                  -s works
#   Windows        0.1+svn    (C) 2007-2008 OpenMoko         -s DOES NOT EXIST
#
# The Windows one predates DfuSe entirely and exits 2 with "invalid option -- s".
# So this ships its own build for Windows and uses STM32Tools everywhere else -
# on macOS theirs is the better choice anyway, being universal where the upstream
# release is Intel only.
#
# All four were read out of the actual binaries, not assumed. Should ST ever
# update the Windows build, drop win/ and this file loses its reason to exist.
#
#   usage: dfu-util.sh <STM32Tools path> [dfu-util arguments...]

DIR=$(cd "$(dirname "$0")" && pwd)

ST_TOOLS="$1"
if [ -z "${ST_TOOLS}" ]; then
  echo "$0: error: STM32Tools path is required as the first argument" >&2
  exit 2
fi
shift

UNAME_OS="$(uname -s)"
case "${UNAME_OS}" in
  Windows*|MINGW*|MSYS*|CYGWIN*)
    # Statically linked, so no libusb DLL travels with it.
    DFU_UTIL="${DIR}/win/dfu-util.exe"
    ;;
  Darwin*)
    DFU_UTIL="${ST_TOOLS}/macosx/dfu-util"
    ;;
  Linux*)
    UNAME_ARCH="$(uname -m)"
    case "${UNAME_ARCH}" in
      x86_64)       DFU_UTIL="${ST_TOOLS}/linux/x86_64/dfu-util" ;;
      aarch64|arm64) DFU_UTIL="${ST_TOOLS}/linux/aarch64/dfu-util" ;;
      *)
        echo "$0: error: unsupported Linux architecture ${UNAME_ARCH}" >&2
        exit 2
        ;;
    esac
    ;;
  *)
    echo "$0: error: unknown host OS ${UNAME_OS}" >&2
    exit 2
    ;;
esac

if [ ! -x "${DFU_UTIL}" ]; then
  echo "$0: error: cannot find ${DFU_UTIL}" >&2
  exit 2
fi

exec "${DFU_UTIL}" "$@"
