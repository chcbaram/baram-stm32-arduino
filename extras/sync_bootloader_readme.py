#!/usr/bin/env python3
"""Rewrite the bundled bootloader's README table from the binary itself.

The table drifted once: the .bin was updated three times while the README still
named an older version, because the update was a string replacement that found
nothing and said nothing. Reading the values out of the file removes the chance
to be wrong, and the assertions below turn a missed row into an error.

    extras/sync_bootloader_readme.py
"""
import hashlib, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIR = ROOT / "stm32" / "bootloaders" / "weact_h750_mini"
BIN = DIR / "weact_h750_mini.bin"
README = DIR / "README.md"

data = BIN.read_bytes()
# firm_ver_t sits at 0x400: magic, then version and name as NUL-terminated text.
version = data[0x404:0x424].split(b"\0")[0].decode("ascii")
name = data[0x424:0x444].split(b"\0")[0].decode("ascii")
sha = hashlib.sha256(data).hexdigest()

rows = {
    "version": f"`{version}`",
    "name": f"`{name}`",
    "size": f"{len(data):,} bytes",
    "sha256": f"`{sha}`",
}

text = README.read_text()
for key, value in rows.items():
    text, n = re.subn(rf"^\| {key} \| .* \|$", f"| {key} | {value} |", text, count=1, flags=re.M)
    if n != 1:
        sys.exit(f"error: no '{key}' row in {README}")

README.write_text(text)
print(f"{BIN.name}: {version} {name} {len(data)} bytes")
print(f"  sha256 {sha}")
