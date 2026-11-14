#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_partitions.py — 展讯/紫光展锐 T158 分区镜像分析器

用法:
    py -3 tools/analyze_partitions.py <镜像目录>                # 总览
    py -3 tools/analyze_partitions.py <镜像目录> --uboot        # u-boot/spl 字符串取证
    py -3 tools/analyze_partitions.py <镜像目录> --dump dtb_a 0 256
"""

import os
import re
import sys
import math
import struct
import argparse

PRINTABLE = re.compile(rb"[ -~]{5,}")


def readn(path, off, n):
    with open(path, "rb") as f:
        f.seek(off)
        return f.read(n)


def hexdump(b, base=0, width=16):
    out = []
    for i in range(0, len(b), width):
        chunk = b[i:i + width]
        h = " ".join("%02X" % c for c in chunk)
        a = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
        out.append("  %08X  %-*s  |%s|" % (base + i, width * 3, h, a))
    return "\n".join(out)


def entropy(b):
    if not b:
        return 0.0
    cnt = [0] * 256
    for c in b:
        cnt[c] += 1
    n = len(b)
    return -sum((c / n) * math.log2(c / n) for c in cnt if c)


def find_strings(path, minlen=6, limit=6000):
    with open(path, "rb") as f:
        data = f.read()
    res = []
    for m in PRINTABLE.finditer(data):
        s = m.group().decode("ascii", "replace")
        if len(s) >= minlen:
            res.append((m.start(), s))
        if len(res) >= limit:
            break
    return res


def detect_magic(head):
    tags = []
    checks = [
        (0, b"DHTB", "DHTB"),
        (0, b"ANDROID!", "AndroidBootImg"),
        (0, b"AVB0", "AVB"),
        (0, b"VBMT", "vbmeta"),
        (0, b"MZ", "MZ/PE"),
        (0, b"EFI PART", "GPT"),
        (0, b"\x7fELF", "ELF"),
        (0, b"\xd0\x0d\xfe\xed", "FDT"),
        (0, b"SPL", "SPL?"),
        (0, b"DTBH", "DTBH"),
        (0, b"\x1f\x8b", "gzip"),
        (0, b"\x28\xb5\x2f\xfd", "zstd"),
        (0, b"PK\x03\x04", "zip"),
        (0, b"VNDRBOOT", "vendor_boot"),
    ]
    for off, magic, desc in checks:
        if head[off:off + len(magic)] == magic:
            tags.append(desc)
    return tags


def scan_dir(root):
    rows = []
    for name in sorted(os.listdir(root)):
        p = os.path.join(root, name)
        if not os.path.isfile(p) or os.path.splitext(name)[1]:
            continue
        size = os.path.getsize(p)
        head = readn(p, 0, 64)
        rows.append((name, size, detect_magic(head), entropy(head), head))
    return rows


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def u64(d, o):
    return struct.unpack_from("<Q", d, o)[0]


def parse_android_boot(path):
    d = readn(path, 0, 4096)
    if d[:8] != b"ANDROID!":
        return None
    info = {
        "kernel_size": u32(d, 8), "kernel_addr": u32(d, 12),
        "ramdisk_size": u32(d, 16), "ramdisk_addr": u32(d, 20),
        "second_size": u32(d, 24), "second_addr": u32(d, 28),
        "tags_addr": u32(d, 32), "page_size": u32(d, 36),
        "unused0": u32(d, 40), "os_version": u32(d, 44),
        "name": d[48:64].rstrip(b"\0").decode("ascii", "replace"),
        "cmdline": d[64:576].rstrip(b"\0").decode("ascii", "replace"),
        "extra_cmdline": d[608:1632].rstrip(b"\0").decode("ascii", "replace"),
        "v1_recovery_dtbo_size": u32(d, 1632),
        "v1_header_size": u32(d, 1644),
        "v2_dtb_size": u32(d, 1648), "v2_dtb_addr": u32(d, 1652),
        "v3_header_version": u32(d, 1656),
    }
    return info


def parse_fdt_at(path, base=0):
    d = readn(path, base, min(os.path.getsize(path) - base, 1024 * 1024))
    if len(d) < 40 or d[:4] != b"\xd0\x0d\xfe\xed":
        return None
    tot = struct.unpack(">I", d[4:8])[0]
    off_st = struct.unpack(">I", d[8:12])[0]
    off_stt = struct.unpack(">I", d[12:16])[0]
    if tot > len(d) or off_st > len(d):
        return None
    strings = d[off_st:]
    out = {"size": tot, "nodes": [], "props": {}}

    def getstr(off):
        end = strings.find(b"\0", off)
        return strings[off:end].decode("ascii", "replace")

    stack = []
    pos = off_stt if off_stt else 0
    # 大多数 dtb 的结构块紧跟头部; 直接从头部后 40 字节开始扫描
    pos = 40
    while pos + 4 <= len(d):
        tok = struct.unpack(">I", d[pos:pos + 4])[0]
        try:
            if tok == 0x1:
                end = d.index(b"\0", pos + 4)
                nm = d[pos + 4:end].decode("ascii", "replace")
                stack.append(nm)
                out["nodes"].append("/" + "/".join(x for x in stack if x))
                pos = ((end + 4) + 3) & ~3
            elif tok == 0x2:
                if stack:
                    stack.pop()
                pos += 4
            elif tok == 0x3:
                ln, no = struct.unpack(">II", d[pos + 4:pos + 12])
                nm = getstr(no)
                out["props"]["/" + "/".join(x for x in stack if x) + ":" + nm] = d[pos + 12:pos + 12 + ln]
                pos = ((pos + 12 + ln) + 3) & ~3
            elif tok == 0x9:
                break
            else:
                break
        except Exception:
            break
    return out


def fdt_int(val):
    return [struct.unpack(">I", val[i:i + 4])[0] for i in range(0, len(val) - 3, 4)]


def report_uboot(path, label):
    print("=" * 78)
    print("[%s] %s (%d bytes)" % (label, os.path.basename(path), os.path.getsize(path)))
    print("=" * 78)
    strs = find_strings(path, 6, 6000)
    keys = ["U-Boot", "u-boot", "bootefi", "efi_loader", "fastboot", "avb", "libavb",
            "vbmeta", "ums", "sprd", "SPRD", "sc27", "sharkl", "DHTB", "T158", "9621",
            "8541", "sl8541", "armv8", "aarch64", "Linaro", "gcc", "clang", "DRAM",
            "spl", "SPL", "atf", "trust", "rsa", "sha256", "bootm", "booti", "kernel_addr",
            "board", "Board", "mmc", "ufs", "ufs-sprd", "sdhci", "serial", "uart", "UART"]
    for k in keys:
        hits = [s for (o, s) in strs if k in s]
        if hits:
            print("  %-12s -> %3d 命中 | %s" % (k, len(hits), " ;; ".join(hits[:4])[:200]))
    print("\n  --- 前 60 条字符串 ---")
    for o, s in strs[:60]:
        print("    %08X  %s" % (o, s[:110]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default=".")
    ap.add_argument("--uboot", action="store_true")
    ap.add_argument("--dump", nargs=3, metavar=("FILE", "OFF", "LEN"))
    a = ap.parse_args()
    root = a.root

    if a.dump:
        name, off, ln = a.dump[0], int(a.dump[1], 0), int(a.dump[2], 0)
        p = os.path.join(root, name)
        print(hexdump(readn(p, off, ln), off))
        return

    print("#" * 78)
    print("# 分区镜像总览")
    print("#" * 78)
    for name, size, tags, ent, head in scan_dir(root):
        print("%-22s %12d  %-18s H=%.2f  %s" %
              (name, size, ",".join(tags) or "-", ent, " ".join("%02X" % c for c in head[:16])))

    print("\n" + "-" * 78)
    for f in ["splloader", "uboot_a", "dtb_a", "dtbo_a", "trustos", "sml", "teecfg_a"]:
        p = os.path.join(root, f)
        if os.path.exists(p):
            print("[%s] 头部 64B:" % f)
            print(hexdump(readn(p, 0, 64)))

    print("\n" + "-" * 78)
    for f in ["boot_a", "vendor_boot_a", "init_boot_a"]:
        p = os.path.join(root, f)
        if not os.path.exists(p):
            continue
        info = parse_android_boot(p)
        if not info:
            print("[%s] 非 boot.img: %s" % (f, hexdump(readn(p, 0, 48))))
            continue
        print("[%s]" % f)
        for k in ["kernel_size", "kernel_addr", "ramdisk_size", "ramdisk_addr", "second_size",
                  "second_addr", "tags_addr", "page_size", "unused0", "os_version",
                  "v1_recovery_dtbo_size", "v1_header_size", "v2_dtb_size", "v2_dtb_addr",
                  "v3_header_version", "name"]:
            v = info[k]
            print("    %-22s = %s" % (k, hex(v) if isinstance(v, int) and v > 0x10000 else v))
        print("    cmdline       = %s" % info["cmdline"])
        print("    extra_cmdline = %s" % info["extra_cmdline"][:200])
        print("    header 0x640-0x6A0:")
        print(hexdump(readn(p, 0x640, 0x60), 0x640))

    print("\n" + "-" * 78)
    for f in ["dtb_a", "dtbo_a"]:
        p = os.path.join(root, f)
        if not os.path.exists(p):
            continue
        fdt = parse_fdt_at(p, 0)
        if not fdt:
            print("[%s] 偏移 0 处无 FDT, 尝试搜索 magic..." % f)
            with open(p, "rb") as fh:
                data = fh.read()
            idx = data.find(b"\xd0\x0d\xfe\xed")
            print("    首个 FDT magic 位于 0x%X" % idx if idx >= 0 else "    未找到 FDT magic")
            if idx >= 0:
                fdt = parse_fdt_at(p, idx)
        if not fdt:
            continue
        print("[%s] FDT size=%d 节点=%d" % (f, fdt["size"], len(fdt["nodes"])))
        for key in fdt["props"]:
            if (key.startswith("/memory") and (":reg" in key or ":device_type" in key)) or \
               key.endswith(":stdout-path") or key.endswith(":bootargs") or \
               (key.endswith(":compatible") and any(t in fdt["props"][key] for t in
                                                    [b"sprd", b"ufs", b"mmc", b"uart", b"serial", b"sharkl", b"ums"])):
                v = fdt["props"][key]
                try:
                    txt = v.rstrip(b"\0").decode("ascii")
                    if all(32 <= c < 127 or c == 0 for c in v):
                        disp = txt
                    else:
                        raise ValueError
                except Exception:
                    disp = " ".join(hex(x) for x in fdt_int(v)[:10])
                print("    %-56s = %s" % (key[:56], disp[:120]))

    if a.uboot:
        print()
        for f in ["uboot_a", "splloader", "fdl2-dl.bin"]:
            p = os.path.join(root, f)
            if os.path.exists(p):
                report_uboot(p, f)


if __name__ == "__main__":
    main()
