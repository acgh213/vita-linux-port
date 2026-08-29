#!/usr/bin/env python3
"""Extract the embedded initramfs from a Vita/PSTV vmlinux.

Method (mandatory, see VITA-PSTV-ROADMAP-2026-08-29.md governing rule 2 and
lab/pstv-recovery-2026-08-29/RESULT.md "Candidate artifacts and preflight"):

    resolve the __initramfs_start / __initramfs_size symbols via the ELF
    symbol table, find the ELF section that CONTAINS each symbol address,
    and use that section's (Addr, Off) pair to map the symbol's virtual
    address to a file offset.

This tool deliberately does NOT search for zstd magic bytes (0x28 0xB5 0x2F
0xFD). This rootfs contains internal zstd frames (the embedded rootfs is
itself built from a zstd-compressed cpio archive that may reference or sit
alongside other zstd-framed data in the kernel image), so a magic-byte scan
finds a truncated internal frame instead of the real embedded image. This
exact failure mode is recorded and explicitly forbidden in the roadmap and
the 2026-08-29 recovery record.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys

SECTION_RE = re.compile(
    r"^\s*\[\s*\d+\]\s+(?P<name>\S*)\s+(?P<type>\S+)\s+"
    r"(?P<addr>[0-9a-fA-F]+)\s+(?P<off>[0-9a-fA-F]+)\s+(?P<size>[0-9a-fA-F]+)\b"
)

NM_LINE_RE = re.compile(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$")


def run(cmd):
    proc = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return proc.stdout


def find_symbol_addr(vmlinux, name, nm_bin):
    out = run([nm_bin, vmlinux])
    for line in out.splitlines():
        m = NM_LINE_RE.match(line.strip())
        if m and m.group(2) == name:
            return int(m.group(1), 16)
    raise SystemExit("vita-extract-initramfs: symbol not found: %s" % name)


def list_sections(vmlinux, readelf_bin):
    out = run([readelf_bin, "-S", "-W", vmlinux])
    sections = []
    for line in out.splitlines():
        m = SECTION_RE.match(line)
        if not m:
            continue
        try:
            addr = int(m.group("addr"), 16)
            off = int(m.group("off"), 16)
            size = int(m.group("size"), 16)
        except ValueError:
            continue
        if m.group("type") == "NOBITS":
            continue  # not present in the file (e.g. .bss)
        sections.append(
            {"name": m.group("name"), "addr": addr, "off": off, "size": size}
        )
    return sections


def section_containing(sections, addr):
    for sec in sections:
        if sec["addr"] <= addr < sec["addr"] + sec["size"]:
            return sec
    return None


def check_endianness(readelf_bin, vmlinux):
    out = run([readelf_bin, "-h", vmlinux])
    if "little endian" in out.lower():
        return "little"
    if "big endian" in out.lower():
        return "big"
    raise SystemExit("vita-extract-initramfs: could not determine ELF endianness")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--vmlinux", required=True)
    ap.add_argument("--out", required=True, help="path to write the extracted blob")
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--readelf", default="readelf")
    args = ap.parse_args()

    endianness = check_endianness(args.readelf, args.vmlinux)
    byteorder = "little" if endianness == "little" else "big"

    start_addr = find_symbol_addr(args.vmlinux, "__initramfs_start", args.nm)
    size_addr = find_symbol_addr(args.vmlinux, "__initramfs_size", args.nm)

    sections = list_sections(args.vmlinux, args.readelf)

    start_sec = section_containing(sections, start_addr)
    if start_sec is None:
        raise SystemExit(
            "vita-extract-initramfs: no ELF section contains __initramfs_start "
            "(0x%x)" % start_addr
        )
    size_sec = section_containing(sections, size_addr)
    if size_sec is None:
        raise SystemExit(
            "vita-extract-initramfs: no ELF section contains __initramfs_size "
            "(0x%x)" % size_addr
        )

    with open(args.vmlinux, "rb") as f:
        data = f.read()

    size_file_off = size_sec["off"] + (size_addr - size_sec["addr"])
    size_bytes = data[size_file_off : size_file_off + 4]
    if len(size_bytes) != 4:
        raise SystemExit("vita-extract-initramfs: truncated read of __initramfs_size")
    size = int.from_bytes(size_bytes, byteorder=byteorder, signed=False)

    if size <= 0 or size > len(data):
        raise SystemExit(
            "vita-extract-initramfs: implausible __initramfs_size value: %d" % size
        )

    start_file_off = start_sec["off"] + (start_addr - start_sec["addr"])
    blob = data[start_file_off : start_file_off + size]
    if len(blob) != size:
        raise SystemExit("vita-extract-initramfs: truncated read of initramfs blob")

    with open(args.out, "wb") as f:
        f.write(blob)

    result = {
        "method": "elf-symbol-section-offset",
        "start_symbol": "__initramfs_start",
        "start_symbol_addr": "0x%08x" % start_addr,
        "start_section": start_sec["name"],
        "size_symbol": "__initramfs_size",
        "size_symbol_addr": "0x%08x" % size_addr,
        "size_section": size_sec["name"],
        "endianness": endianness,
        "size_bytes": size,
        "sha256": hashlib.sha256(blob).hexdigest(),
        "out_path": args.out,
    }
    json.dump(result, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
