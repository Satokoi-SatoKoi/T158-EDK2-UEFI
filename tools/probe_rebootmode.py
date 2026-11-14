#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
probe_rebootmode.py — 从 vendor_boot_a 的基设备树中取证 reboot-mode 机制

用途: 找到展锐 U-Boot 判定"进 recovery / fastboot / normal"的寄存器位置,
      供 T158Pkg 的菜单项 "Boot Recovery (TWRP)" 写 reboot-mode 使用。

用法: py -3 tools/probe_rebootmode.py <镜像目录> [偏移] [大小]
默认: vendor_boot_a @ 0x4CF0040 size 0x25C7C
       镜像目录默认为当前目录 (".")
"""
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
if not fdt:
    print("解析失败: 偏移 0x%X 处不是有效 FDT" % OFF)
    sys.exit(1)


def u32s(v):
    return [struct.unpack_from(">I", v, i)[0] for i in range(0, len(v) - 3, 4)]


def dump_node(n):
    print("  [%s]" % n)
    for k, v in sorted(fdt["props"].items()):
        if k.startswith(n + ":"):
            print("      %-16s = %s" % (k[len(n) + 1:], fmt(v)[:200]))


KEYWORDS = ("reboot", "-mode", "bootmode", "boot_mode", "power", "key", "keypad", "pwr")

print("FDT: size=0x%X nodes=%d" % (fdt["size"], len(fdt["nodes"])))

print("\n===== 顶层节点一览 =====")
for n in sorted(fdt["nodes"]):
    depth = n.count("/")
    if depth <= 2:
        print("  " + n)

print("\n===== 命中 reboot/mode/key 关键字的节点 =====")
hit = False
for n in sorted(fdt["nodes"]):
    low = n.lower()
    if any(k in low for k in KEYWORDS):
        dump_node(n)
        hit = True
if not hit:
    print("  (无) —— 基 DT 里没有 reboot-mode 节点, 需检查 dtbo 或 U-Boot 二进制")

print("\n===== compatible 含 syscon/reboot/mode 的节点 =====")
hit = False
for n in sorted(fdt["nodes"]):
    comp = fdt["props"].get(n + ":compatible", b"").decode("ascii", "replace")
    if any(k in comp for k in ("syscon", "reboot", "mode", "sc27xx", "ump96")):
        print("  %-52s compatible = %s" % (n[:52], comp.replace("\0", " | ")))
        dump_node(n)
        hit = True
if not hit:
    print("  (无)")

print("\n===== /chosen 全部属性 =====")
for k, v in sorted(fdt["props"].items()):
    if k.startswith("/chosen:"):
        print("  %-24s = %s" % (k[:24], fmt(v)[:220]))
