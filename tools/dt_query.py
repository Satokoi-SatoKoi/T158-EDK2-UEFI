#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dt_query.py — 按需查询基设备树属性 (默认 vendor_boot_a @0x4CF0040)"""
import os
import sys
import struct
from find_dtb import parse_fdt, fmt

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
OFF = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x4CF0040
SIZE = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x25C7C

with open(os.path.join(ROOT, "vendor_boot_a"), "rb") as f:
    f.seek(OFF)
    buf = f.read(SIZE)
fdt = parse_fdt(buf)


def u32s(v):
    return [struct.unpack_from(">I", v, i)[0] for i in range(0, len(v) - 3, 4)]


def show(patterns, limit=400):
    for k, v in sorted(fdt["props"].items()):
        if any(p in k for p in patterns):
            print("  %-70s = %s" % (k[:70], fmt(v)[:150]))


print("=== aliases ===")
show(["/aliases:"])
print("\n=== /soc / bus ===")
for n in ["/soc", "/soc/ap-apb", "/soc/ap-ahb", "/soc/aon", "/soc/pub"]:
    for k, v in sorted(fdt["props"].items()):
        if k.startswith(n + ":") and (k.endswith(":reg") or k.endswith(":ranges") or
                                      k.endswith("address-cells") or k.endswith("size-cells") or
                                      k.endswith(":compatible")):
            print("  %-58s = %s" % (k[:58], fmt(v)[:170]))

print("\n=== serial/uart 全部属性 ===")
for n in fdt["nodes"]:
    if "/serial@" in n or "/uart" in n:
        for k, v in sorted(fdt["props"].items()):
            if k.startswith(n + ":"):
                print("  %-70s = %s" % (k[:70], fmt(v)[:150]))
        print("  ---")

print("\n=== ufs / sdio 全部属性 ===")
for n in fdt["nodes"]:
    if "/ufs@" in n or "/sdio@" in n:
        for k, v in sorted(fdt["props"].items()):
            if k.startswith(n + ":"):
                print("  %-70s = %s" % (k[:70], fmt(v)[:150]))
        print("  ---")

print("\n=== reserved-memory reg ===")
for n in fdt["nodes"]:
    if n.startswith("/reserved-memory"):
        reg = fdt["props"].get(n + ":reg")
        if reg:
            c = u32s(reg)
            if len(c) >= 4:
                base = (c[0] << 32) | c[1]
                size = (c[2] << 32) | c[3]
                print("  %-58s 0x%010X .. 0x%010X (%d MB)" % (n[:58], base, base + size, size >> 20 if size else 0))

print("\n=== chosen bootargs (原文) ===")
ba = fdt["props"].get("/chosen:bootargs", b"")
print("   ", ba.rstrip(b"\0").decode("ascii", "replace"))
