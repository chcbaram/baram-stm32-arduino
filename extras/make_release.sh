#!/bin/bash
#
# Package the platform and register it in the board manager index.
#
#   extras/make_release.sh <version> [--dry-run]
#
# Builds baram-stm32-<version>.tar.bz2 from stm32/, records its size and
# checksum in package_baram_stm32_index.json, and uploads the archive as a
# GitHub release asset. The index itself is served from the main branch, so
# commit and push it afterwards.
#
# --dry-run does everything except the upload, so the archive and the index
# entry can be inspected first.

set -euo pipefail

REPO_SLUG="chcbaram/baram-stm32-arduino"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INDEX="$ROOT/package_baram_stm32_index.json"
PLATFORM_DIR="$ROOT/stm32"

usage() {
  echo "usage: $(basename "$0") <version> [--dry-run]" >&2
  echo "  e.g. $(basename "$0") 0.1.0" >&2
  exit "${1:-1}"
}

[ $# -ge 1 ] || usage
VERSION="$1"; shift
DRY_RUN=0
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    *) echo "unknown option: $arg" >&2; usage ;;
  esac
done

# Arduino compares these as versions, so keep them strictly numeric.
if ! echo "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  echo "error: version must look like 1.2.3, got '$VERSION'" >&2
  exit 1
fi

[ -f "$PLATFORM_DIR/platform.txt" ] || { echo "error: $PLATFORM_DIR/platform.txt not found" >&2; exit 1; }
[ -f "$INDEX" ] || { echo "error: $INDEX not found" >&2; exit 1; }

# The version in platform.txt and the one in the index must agree. NU40DK let
# them drift (platform.txt 1.7.0 vs index 0.0.3); don't repeat that.
echo "==> setting platform.txt version to $VERSION"
if [ "$(uname -s)" = "Darwin" ]; then
  sed -i '' "s/^version=.*/version=$VERSION/" "$PLATFORM_DIR/platform.txt"
else
  sed -i "s/^version=.*/version=$VERSION/" "$PLATFORM_DIR/platform.txt"
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

NAME="baram-stm32-$VERSION"
ARCHIVE="$ROOT/$NAME.tar.bz2"

echo "==> staging $PLATFORM_DIR -> $NAME/"
mkdir -p "$STAGE/$NAME"
# The archive must contain exactly one top-level directory holding platform.txt.
( cd "$PLATFORM_DIR" && tar -cf - --exclude '.DS_Store' --exclude '.git' . ) \
  | ( cd "$STAGE/$NAME" && tar -xf - )

echo "==> creating $(basename "$ARCHIVE")"
rm -f "$ARCHIVE"
( cd "$STAGE" && tar -cjf "$ARCHIVE" "$NAME" )

# wc -c rather than stat, whose flags differ between macOS and GNU.
SIZE="$(wc -c < "$ARCHIVE" | tr -d ' ')"
SHA256="$(shasum -a 256 "$ARCHIVE" | awk '{print toupper($1)}')"
URL="https://github.com/$REPO_SLUG/releases/download/$VERSION/$NAME.tar.bz2"

echo "    size     $SIZE"
echo "    sha256   $SHA256"
echo "    url      $URL"

echo "==> updating $(basename "$INDEX")"
VERSION="$VERSION" NAME="$NAME" URL="$URL" SIZE="$SIZE" SHA256="$SHA256" \
python3 - "$INDEX" <<'PY'
import json, os, sys

path = sys.argv[1]
with open(path) as f:
    index = json.load(f)

pkg = index["packages"][0]

entry = {
    "name": "BARAM STM32 Boards",
    "architecture": "stm32",
    "version": os.environ["VERSION"],
    "category": "Contributed",
    "url": os.environ["URL"],
    "archiveFileName": os.environ["NAME"] + ".tar.bz2",
    "checksum": "SHA-256:" + os.environ["SHA256"],
    "size": os.environ["SIZE"],
    "help": {"online": "https://github.com/chcbaram/baram-stm32-arduino"},
    "boards": [{"name": "WEACT-H750-MINI"}],
    # No tools are hosted here. These all come from the STMicroelectronics
    # package, so its index URL has to be in Board Manager too - see README.
    # The versions must match what this platform.txt refers to by path.
    "toolsDependencies": [
        {"packager": "STMicroelectronics", "name": "xpack-arm-none-eabi-gcc", "version": "14.2.1-1.1"},
        {"packager": "STMicroelectronics", "name": "xpack-openocd", "version": "0.12.0-6"},
        {"packager": "STMicroelectronics", "name": "STM32Tools", "version": "2.4.0"},
        {"packager": "STMicroelectronics", "name": "CMSIS", "version": "6.2.0"},
        {"packager": "STMicroelectronics", "name": "CMSIS_DSP", "version": "1.16.2"},
        {"packager": "STMicroelectronics", "name": "CMSIS_NN", "version": "7.0.0"},
        {"packager": "STMicroelectronics", "name": "STM32_SVD", "version": "1.20.0"},
    ],
}

platforms = pkg.setdefault("platforms", [])
# Re-releasing the same version replaces it rather than adding a duplicate.
platforms = [p for p in platforms if p.get("version") != entry["version"]]
platforms.append(entry)
platforms.sort(key=lambda p: [int(n) for n in p["version"].split(".")])
pkg["platforms"] = platforms

with open(path, "w") as f:
    json.dump(index, f, indent=2)
    f.write("\n")

print("    %d platform entr%s in index" % (len(platforms), "y" if len(platforms) == 1 else "ies"))
PY

if [ "$DRY_RUN" -eq 1 ]; then
  echo "==> dry run, not uploading"
  echo
  echo "Archive left at $ARCHIVE"
  exit 0
fi

command -v gh >/dev/null 2>&1 || { echo "error: gh CLI not found" >&2; exit 1; }

echo "==> uploading to GitHub release $VERSION"
if gh release view "$VERSION" --repo "$REPO_SLUG" >/dev/null 2>&1; then
  gh release upload "$VERSION" "$ARCHIVE" --repo "$REPO_SLUG" --clobber
else
  gh release create "$VERSION" "$ARCHIVE" --repo "$REPO_SLUG" \
    --title "$VERSION" --notes "BARAM STM32 Boards $VERSION"
fi

echo
echo "Done. Now commit and push package_baram_stm32_index.json and"
echo "stm32/platform.txt so Board Manager picks up $VERSION."
