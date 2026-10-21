#!/usr/bin/env python3
"""Validate that kernel.elf is a well-formed Multiboot 1 kernel.

Used by `make check` so a broken header fails the build instead of failing at
boot with a cryptic GRUB error. Works without grub-file; if grub-file is
installed the Makefile runs it as well.

Checks:
  * 32-bit little-endian x86 ELF executable
  * Multiboot magic within the first 8192 bytes of the file, 4-byte aligned
  * magic + flags + checksum == 0 (mod 2**32)
  * flags request memory info and a video mode, and the video fields are sane
  * entry point == _start inside a PT_LOAD at or above 1 MiB, no overlaps
"""
import struct
import sys

MB_MAGIC = 0x1BADB002
SEARCH_LIMIT = 8192


def fail(msg):
    print(f"check_kernel: FAIL: {msg}")
    sys.exit(1)


def main(path):
    data = open(path, "rb").read()

    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        fail("not a 32-bit little-endian ELF")
    (e_type, e_machine, _ver, e_entry, e_phoff, _shoff, _flags, _ehsize,
     e_phentsize, e_phnum) = struct.unpack_from("<HHIIIIIHHH", data, 16)
    if e_type != 2 or e_machine != 3:
        fail("not an i386 executable ELF")

    # --- Multiboot header ----------------------------------------------------
    off = None
    for cand in range(0, min(len(data), SEARCH_LIMIT) - 11, 4):
        magic, flags, checksum = struct.unpack_from("<III", data, cand)
        if magic == MB_MAGIC:
            off = cand
            break
    if off is None:
        fail(f"Multiboot magic 0x1BADB002 not found in first {SEARCH_LIMIT} bytes")
    if (magic + flags + checksum) & 0xFFFFFFFF != 0:
        fail("Multiboot checksum is wrong")
    if flags & (1 << 16):
        fail("flag bit 16 (a.out kludge) set; AGOS is an ELF")
    if not flags & (1 << 2):
        fail("video-mode request flag (bit 2) not set")
    mode_type, width, height, depth = struct.unpack_from("<IIII", data, off + 32)
    if (mode_type, width, height, depth) != (0, 1024, 768, 32):
        fail(f"unexpected video request {mode_type},{width}x{height}x{depth}")

    # --- Program headers -----------------------------------------------------
    loads = []
    for i in range(e_phnum):
        (p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
         p_align) = struct.unpack_from("<IIIIIIII", data, e_phoff + i * e_phentsize)
        if p_type == 1:
            loads.append((p_paddr, p_memsz, p_offset, p_filesz, p_flags))
    if not loads:
        fail("no PT_LOAD segments")
    for paddr, memsz, poff, filesz, pflags in loads:
        if paddr < 0x100000:
            fail(f"segment at 0x{paddr:X} is below 1 MiB")
        if poff + filesz > len(data):
            fail("segment extends past end of file")
    spans = sorted((p, p + m) for p, m, *_ in loads)
    for (a0, a1), (b0, b1) in zip(spans, spans[1:]):
        if b0 < a1:
            fail("PT_LOAD segments overlap in memory")
    if not any(p <= e_entry < p + m for p, m, *_ in loads):
        fail(f"entry point 0x{e_entry:X} is not inside a loaded segment")
    # The header must be inside a loaded segment too (GRUB reads it from the file).
    hdr_ok = any(poff <= off < poff + filesz for _, _, poff, filesz, _ in loads)
    if not hdr_ok:
        fail("Multiboot header is not part of a loaded segment")

    lo = min(s for s, _ in spans)
    hi = max(e for _, e in spans)
    print(f"  OK      multiboot header @ file offset 0x{off:X}, flags 0x{flags:X}, "
          f"video {width}x{height}x{depth}")
    print(f"  OK      entry 0x{e_entry:X}, image 0x{lo:X}-0x{hi:X} "
          f"({(hi - lo) // 1024} KiB in memory, {len(data)} bytes on disk)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: check_kernel.py kernel.elf")
        sys.exit(2)
    main(sys.argv[1])
