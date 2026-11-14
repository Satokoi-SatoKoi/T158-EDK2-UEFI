#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
probe_t158.py — T158(UMS9621) 关键取证: boot.img 布局 / arm64 内核头 / DT / u-boot 字符串
"""
import os
import re
import sys
import struct

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
P = lambda n: os.path.join(ROOT, n)


def rd(p, o, n):
    with open(p, "rb") as f:
        f.seek(o)
        return f.read(n)


def hd(b, base=0, w=16):
    return "\n".join("  %08X  %-*s  |%s|" % (
        base + i, w * 3, " ".join("%02X" % c for c in b[i:i + w]),
        "".join(chr(c) if 32 <= c < 127 else "." for c in b[i:i + w])) for i in range(0, len(b), w))


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def le32(d, o):
    return struct.unpack_from("<I", d, o)[0]


print("=" * 78)
print("[1] boot_a: Android boot.img v4 头")
print("=" * 78)
d = rd(P("boot_a"), 0, 0x80)
print(hd(d))
# v4 布局候选
fields = {}
for i in range(0, 0x80, 4):
    fields[i] = le32(d, i)
print("  LE 逐字段:", {hex(k): hex(v) for k, v in fields.items() if v})

print()
print("=" * 78)
print("[2] boot_a: 搜索 ARM64 Linux 内核镜像头")
print("=" * 78)
with open(P("boot_a"), "rb") as f:
    data = f.read(64 * 1024 * 1024)
idx = data.find(b"ARM64")
while idx != -1 and idx < 0x200000:
    print("  'ARM64' 位于 0x%X" % idx)
    h = data[idx - 0x38:idx + 0x30]
    print(hd(h, idx - 0x38))
    # arm64 header: code0(0) code1(4) text_offset(8) image_size(16) flags(24) res2(32) res3(40) res4(48) magic(56=0x38)
    (code0, code1, text_offset, image_size, flags) = struct.unpack_from("<IIQQQ", data, idx - 0x38)
    print("  branch/code0=0x%08X text_offset=0x%X image_size=0x%X flags=0x%X" % (code0, text_offset, image_size, flags))
    idx = data.find(b"ARM64", idx + 1)

print()
print("=" * 78)
print("[3] dtbo_a: Android DTBO (dt_table_header, 大端)")
print("=" * 78)
d = rd(P("dtbo_a"), 0, 128)
print(hd(d))
magic, total, hdr_size, ent_size, ent_cnt, ent_off, page, ver = struct.unpack_from(">8I", d, 0)
print("  magic=0x%08X total=%d hdr_size=%d entry_size=%d count=%d entries_off=0x%X page=0x%X ver=%d"
      % (magic, total, hdr_size, ent_size, ent_cnt, ent_off, page, ver))
for i in range(ent_cnt):
    o = ent_off + i * ent_size
    dt_size, dt_offset, dt_id, dt_rev, dt_cust = struct.unpack_from(">5I", rd(P("dtbo_a"), o, 20))
    print("  entry[%d]: size=0x%X off=0x%X id=0x%X rev=0x%X" % (i, dt_size, dt_offset, dt_id, dt_rev))


def parse_fdt(buf):
    if buf[:4] != b"\xd0\x0d\xfe\xed":
        return None
    (tot, off_struct, off_strings, off_rsv, ver, last_comp, boot_cpu,
     size_str, size_struct) = struct.unpack_from(">9I", buf, 4)
    strings = buf[off_strings:tot]
    out = {"size": tot, "version": ver, "nodes": [], "props": {}}
    stack = []
    pos = off_struct

    def getstr(off):
        e = strings.find(b"\0", off)
        return strings[off:e].decode("ascii", "replace")

    while pos + 4 <= tot and pos + 4 <= len(buf):
        tok = struct.unpack_from(">I", buf, pos)[0]
        try:
            if tok == 1:
                e = buf.index(b"\0", pos + 4)
                stack.append(buf[pos + 4:e].decode("ascii", "replace"))
                out["nodes"].append("/" + "/".join(x for x in stack if x))
                pos = (e + 4 + 3) & ~3
            elif tok == 2:
                stack and stack.pop()
                pos += 4
            elif tok == 3:
                ln, no = struct.unpack_from(">II", buf, pos + 4)
                out["props"]["/" + "/".join(x for x in stack if x) + ":" + getstr(no)] = buf[pos + 12:pos + 12 + ln]
                pos = (pos + 12 + ln + 3) & ~3
            elif tok == 9:
                break
            else:
                break
        except Exception:
            break
    return out


print()
print("=" * 78)
print("[4] DT 关键节点 (memory / chosen / uart / ufs / mmc / timer / pmu)")
print("=" * 78)
for i in range(ent_cnt):
    o = ent_off + i * ent_size
    dt_size, dt_offset, _, _, _ = struct.unpack_from(">5I", rd(P("dtbo_a"), o, 20))
    fdt = parse_fdt(rd(P("dtbo_a"), dt_offset, dt_size))
    if not fdt:
        print("  entry[%d] 解析失败" % i)
        continue
    print("\n  ---- entry[%d] nodes=%d ----" % (i, len(fdt["nodes"])))
    for k, v in fdt["props"].items():
        if (k.startswith("/memory") and (":reg" in k or ":device_type" in k)) or \
           ":stdout-path" in k or ":bootargs" in k or \
           (k.endswith(":compatible")) or (k.endswith(":reg") and any(t in k for t in
                                                                     ["serial", "uart", "ufs", "mmc", "sdhci", "timer", "intc", "gic", "syscon"])):
            try:
                txt = v.rstrip(b"\0").decode("ascii")
                if all(c == 0 or 32 <= c < 127 for c in v):
                    disp = txt[:130]
                else:
                    raise ValueError
            except Exception:
                cells = [struct.unpack(">I", v[j:j + 4])[0] for j in range(0, min(len(v), 48) - 3, 4)]
                disp = " ".join("0x%08X" % c for c in cells[:12])
            if k.endswith(":compatible") and not any(t in v for t in [b"sprd", b"sharkl", b"ums", b"arm,", b"simple", b"fixed", b"regulator", b"gpio", b"sc2", b"ufs", b"mmc", b"sdhci", b"pwm", b"i2c", b"spi", b"usb", b"phy"]):
                continue
            print("    %-60s = %s" % (k[:60], disp))

print()
print()
print("=" * 78)
print("[4b] boot_a payload 起始 (0x1000) 与压缩格式探测")
print("=" * 78)
print(hd(rd(P("boot_a"), 0x1000, 64), 0x1000))
print(hd(rd(P("boot_a"), 0x1000 + 0x2C96A00 - 16, 48), 0x1000 + 0x2C96A00 - 16))
for off in (0x1000, 0x1000 + 0x2C96A00):
    h = rd(P("boot_a"), off, 16)
    if h[:2] == b"\x1f\x8b":
        print("  0x%X: gzip" % off)
    elif h[:4] == b"\x02\x21\x4c\x18":
        print("  0x%X: lz4 frame" % off)
    elif h[:4] == b"\x28\xb5\x2f\xfd":
        print("  0x%X: zstd" % off)
    elif h[:4] == b"\x04\x22\x4d\x18":
        print("  0x%X: lz4 legacy" % off)
    elif h[:5] == b"\xfd7zXZ":
        print("  0x%X: xz" % off)

print("=" * 78)
print("[5] u-boot / fdl2 关键字符串 grep")
print("=" * 78)
PRINTABLE = re.compile(rb"[ -~]{6,}")
for f in ["uboot_a", "splloader", "FDL_ums9621_T765_T8200/fdl2-dl.bin"]:
    p = P(f)
    if not os.path.exists(p):
        continue
    with open(p, "rb") as fh:
        data = fh.read()
    strs = [m.group().decode("ascii", "replace") for m in PRINTABLE.finditer(data)]
    print("\n--- %s (%d strings) ---" % (f, len(strs)))
    pats = ["U-Boot", "u-boot", "UBOOT", "bootloader", "2015.07", "2016", "2020", "2021",
            "GCC", "gcc", "Linaro", "0x80000000", "mem=", "initrd", "bootcmd", "bootdelay",
            "kernel_addr", "ramdisk_addr", "dtb_addr", "bootcmd", "bootargs", "console=ttyS",
            "0x80000000", "0x40000000", "DRAM", "dram_size", "gd->ram", "CONFIG_",
            "ums9621", "1h10", "sprd,ums9621", "fastboot", "bootdevice", "normal_boot",
            "bootimg", "kernel_offset", "dt_offset", "ramdisk_offset", "load addr", "entry",
            "secure", "vbmeta", "avb", "lock", "unlock", "download", "fdl"]
    for pat in pats:
        hits = [s for s in strs if pat.lower() in s.lower()]
        if hits:
            print("  %-16s -> %s" % (pat, " ;; ".join(hits[:5])[:230]))
