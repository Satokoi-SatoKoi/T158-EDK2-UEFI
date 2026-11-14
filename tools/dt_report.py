#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dt_report.py — 从 vendor_boot_a 中提取的基设备树生成平台报告(供 EDK2 用)

用法: py -3 tools/dt_report.py <镜像目录> [偏移] [大小]
默认: vendor_boot_a @ 0x4CF0040 size 0x25C7C
"""
import os
import sys
import struct
from find_dtb import parse_fdt, fmt, FDT_MAGIC

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
OFF = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x4CF0040
SIZE = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x25C7C

with open(os.path.join(ROOT, "vendor_boot_a"), "rb") as f:
    f.seek(OFF)
    buf = f.read(SIZE)

fdt = parse_fdt(buf)
if not fdt:
    print("解析失败")
    sys.exit(1)

print("FDT: size=0x%X ver=%d nodes=%d props=%d" % (fdt["size"], fdt["version"], len(fdt["nodes"]), len(fdt["props"])))
print("\n--- memreserve ---")
for a, b in fdt["rsv"]:
    print("  0x%016X .. +0x%X" % (a, b))


def reg_cells(v):
    v = v + b"\0" * ((-len(v)) % 4)
    q = [struct.unpack_from(">Q", v, i)[0] for i in range(0, len(v) - 7, 8)]
    return q


def u32s(v):
    return [struct.unpack_from(">I", v, i)[0] for i in range(0, len(v) - 3, 4)]


print("\n--- memory / chosen ---")
for k, v in sorted(fdt["props"].items()):
    if k.startswith("/memory") or k.startswith("/chosen"):
        print("  %-64s = %s" % (k[:64], fmt(v)))
        if k.endswith(":reg") and k.startswith("/memory"):
            q = reg_cells(v)
            print("     => DRAM 区间:", ["0x%X .. +0x%X" % (q[i], q[i + 1]) for i in range(0, len(q) - 1, 2)])

print("\n--- 串口 / GIC / timer / ufs / mmc / sdhci / pmu / clock 节点 ---")
KEY = ["serial", "uart", "intc", "gic", "timer", "ufs", "mmc", "sdhci", "sdio",
       "pmic", "syscon", "clock", "gpio", "watchdog", "pcie", "usb", "dmc"]
seen = set()
for node in fdt["nodes"]:
    if any(t in node.lower() for t in KEY):
        comp = fdt["props"].get(node + ":compatible", b"")
        reg = fdt["props"].get(node + ":reg", b"")
        irq = fdt["props"].get(node + ":interrupts", b"")
        st = fdt["props"].get(node + ":status", b"")
        if node in seen:
            continue
        seen.add(node)
        line = "  %-62s %s" % (node[:62], comp.rstrip(b"\0").decode("ascii", "replace")[:44])
        print(line)
        if reg:
            cells = u32s(reg)
            if len(cells) >= 4:
                print("        reg = 0x%X (len 0x%X)" % ((cells[0] << 32) | cells[1] if cells[0] else cells[1 if len(cells) > 2 else 0], cells[3] if len(cells) > 3 else cells[-1]))
            else:
                print("        reg = %s" % " ".join(hex(c) for c in cells))
        if irq:
            print("        interrupts = %s" % " ".join(hex(c) for c in u32s(irq)[:6]))
        if st:
            print("        status = %s" % st.rstrip(b"\0").decode("ascii", "replace"))

print("\n--- 顶层 compatible 与 model ---")
for k in ["/:compatible", "/:model", "/:serial-number", "/:interrupt-parent"]:
    if k in fdt["props"]:
        print("  %-20s = %s" % (k, fmt(fdt["props"][k])))

print("\n--- 所有含 sprd 的 compatible (去重, 前 80) ---")
comps = set()
for k, v in fdt["props"].items():
    if k.endswith(":compatible"):
        for c in v.split(b"\0"):
            c = c.decode("ascii", "replace")
            if c and ("sprd" in c or "sharkl" in c or "ums" in c):
                comps.add(c)
for c in sorted(comps)[:80]:
    print("   ", c)
