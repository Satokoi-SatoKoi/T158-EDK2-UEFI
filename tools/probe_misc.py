#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
probe_misc.py — 分析 misc / miscdata 分区结构, 取证 A/B 槽位与 recovery 标志布局

背景 (来自 uboot_a 字符串取证):
  "%s:Message in misc indicate the RECOVERY MODE"
  "Detect the recovery message in the misc partition"
  "ANDROID: Attempting slot %c, tries_remaining %d"      <- libavb_ab 的标准输出
  "%s: ab_slot_flag is %d"

=> 展锐这版 U-Boot 走标准 Android 机制:
   - misc 偏移 0    : struct bootloader_message (BCB), command="boot-recovery" 进 TWRP
   - misc 偏移 2048 : AvbABData (libavb_ab), magic "\0AB0", 含两槽 priority/tries/successful

用法: py -3 tools/probe_misc.py <misc镜像> [miscdata镜像]
"""
import sys
import struct
import re

MISC = sys.argv[1] if len(sys.argv) > 1 else "images/misc"
MISCDATA = sys.argv[2] if len(sys.argv) > 2 else None


def hexdump(data, base, limit=256):
    out = []
    for i in range(0, min(len(data), limit), 16):
        chunk = data[i:i + 16]
        out.append("    %08X  %s  |%s|" % (
            base + i,
            " ".join("%02X" % b for b in chunk).ljust(48),
            "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)))
    return "\n".join(out)


def show(path, title):
    print("\n" + "=" * 70)
    print(" %s : %s" % (title, path))
    print("=" * 70)
    try:
        d = open(path, "rb").read()
    except Exception as e:
        print("  读取失败: %s" % e)
        return

    print("  大小: %d (0x%X) 字节" % (len(d), len(d)))

    print("\n-- struct bootloader_message @0 --")
    print("  command [0:32]   = %r" % d[0:32].split(b"\0")[0])
    print("  status  [32:64]  = %r" % d[32:64].split(b"\0")[0])
    rec = d[64:64 + 768].split(b"\0")[0]
    print("  recovery[64:832] = %r" % rec[:120])
    print("  stage   [832:864]= %r" % d[832:864].split(b"\0")[0])

    print("\n-- @2048: AvbABData 候选 (libavb_ab, magic = 00 41 42 30) --")
    magic = d[2048:2052]
    print("  magic = %r  %s" % (magic, "  <== 命中!" if magic == b"\x00AB0" else "  (非标准 magic)"))
    print(hexdump(d[2048:2048 + 128], 2048, 128))

    # AvbABData 结构 (libavb_ab): magic[4] version_major version_minor[2] reserved[2]
    # 接着 AvbABSlotData slots[2]: priority(1) tries_remaining(1) successful_boot(1) reserved(1)
    if magic == b"\x00AB0":
        ver = d[2052:2056]
        print("\n  version = %d.%d" % (ver[0], struct.unpack_from("<H", ver, 2)[0]))
        for i in range(2):
            off = 2056 + i * 4
            p, t, s = d[off], d[off + 1], d[off + 2]
            suffix = "_a" if i == 0 else "_b"
            print("  slot %s: priority=%d tries_remaining=%d successful_boot=%d"
                  % (suffix, p, t, s))

    print("\n-- 前 8KB 中的可打印字符串 --")
    for m in re.finditer(rb"[ -~]{4,}", d[:8192]):
        s = m.group().decode("ascii", "replace")
        if s.strip("\0"):
            print("  @%-6d %s" % (m.start(), s))

    print("\n-- 非全 0 / 非全 FF 的区域 (前 64KB, 按 4KB 块) --")
    for i in range(0, min(len(d), 65536), 4096):
        blk = d[i:i + 4096]
        if blk != b"\0" * len(blk) and blk != b"\xff" * len(blk):
            nz = sum(1 for b in blk if b)
            print("  @0x%06X  非零字节 %d/%d" % (i, nz, len(blk)))


show(MISC, "misc 分区")
if MISCDATA:
    show(MISCDATA, "miscdata 分区")
