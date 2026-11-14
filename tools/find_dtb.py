#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""find_dtb.py — 在分区镜像中搜索 FDT (device tree blob) 并解析关键属性"""
import os
import sys
import struct

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
FDT_MAGIC = b"\xd0\x0d\xfe\xed"


def parse_fdt(buf):
    if buf[:4] != FDT_MAGIC:
        return None
    (tot, off_struct, off_strings, off_rsv, ver, last_comp, boot_cpu,
     size_str, size_struct) = struct.unpack_from(">9I", buf, 4)
    if tot > len(buf) or off_struct == 0 or off_strings == 0:
        return None
    strings = buf[off_strings:min(tot, len(buf))]
    out = {"size": tot, "version": ver, "nodes": [], "props": {}, "rsv": []}

    def getstr(off):
        e = strings.find(b"\0", off)
        return strings[off:e].decode("ascii", "replace") if e >= 0 else ""

    pos = off_rsv
    while pos + 16 <= len(buf):
        a, b = struct.unpack_from(">QQ", buf, pos)
        if a == 0 and b == 0:
            break
        out["rsv"].append((a, b))
        pos += 16
    stack = []
    pos = off_struct
    while pos + 4 <= len(buf):
        tok = struct.unpack_from(">I", buf, pos)[0]
        if tok == 1:
            e = buf.find(b"\0", pos + 4)
            if e < 0:
                break
            stack.append(buf[pos + 4:e].decode("ascii", "replace"))
            out["nodes"].append("/" + "/".join(x for x in stack if x))
            pos = (e + 1 + 3) & ~3
        elif tok == 2:
            if stack:
                stack.pop()
            pos += 4
        elif tok == 3:
            ln, no = struct.unpack_from(">II", buf, pos + 4)
            nm = getstr(no)
            if nm:
                out["props"]["/" + "/".join(x for x in stack if x) + ":" + nm] = buf[pos + 12:pos + 12 + ln]
            pos = (pos + 12 + ln + 3) & ~3
        elif tok == 4:
            pos += 4
        elif tok == 9:
            break
        else:
            break
    return out


def fmt(v):
    try:
        if all(c == 0 or 32 <= c < 127 for c in v):
            t = v.rstrip(b"\0").decode("ascii")
            if t:
                return t[:120]
    except Exception:
        pass
    cells = [struct.unpack_from(">I", v, j)[0] for j in range(0, min(len(v), 64) - 3, 4)]
    return " ".join("0x%08X" % c for c in cells[:12])


def scan(fn):
    p = os.path.join(ROOT, fn)
    if not os.path.exists(p):
        return
    size = os.path.getsize(p)
    with open(p, "rb") as f:
        data = f.read()
    nz = sum(1 for c in data[:0x100000] if c)
    hits = []
    i = data.find(FDT_MAGIC)
    while i != -1:
        fdt = parse_fdt(data[i:i + 0x400000])
        if fdt and fdt["size"] > 0x200:
            hits.append((i, fdt))
        i = data.find(FDT_MAGIC, i + 4)
        if len(hits) > 8:
            break
    print("[%s] size=%d 首1MB非零字节=%d FDT命中=%d" % (fn, size, nz, len(hits)))
    for off, fdt in hits[:4]:
        print("   FDT @0x%X size=0x%X ver=%d nodes=%d rsv=%s" %
              (off, fdt["size"], fdt["version"], len(fdt["nodes"]), fdt["rsv"][:3]))
        for k in fdt["nodes"][:25]:
            print("      node: %s" % k)
        for k, v in fdt["props"].items():
            if (k.startswith("/memory") or ":stdout-path" in k or ":bootargs" in k or
                (k.endswith(":compatible") and any(t in v for t in [b"sprd", b"sharkl", b"ums", b"arm,gic", b"arm,armv8"])) or
                (k.endswith(":reg") and any(t in k for t in ["serial", "uart", "ufs", "mmc", "sdhci", "gic", "timer"])) or
                k.endswith(":device_type") or k.endswith(":clock-frequency") or k.endswith(":interrupts") and any(t in k for t in ["uart", "ufs"])):
                print("      %-56s = %s" % (k[:56], fmt(v)))


if __name__ == "__main__":
    files = sys.argv[2:] or ["dtb_a", "dtbo_a", "boot_a", "vendor_boot_a", "init_boot_a",
                             "splloader", "uboot_a", "trustos_a", "logo"]
    for fn in files:
        scan(fn)
