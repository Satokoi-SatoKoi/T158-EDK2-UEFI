#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
probe_crc.py — 反推 misc 分区 @2048 boot-control 结构的校验算法

结构 (实测):
  0x800: slot_suffix[4]  = "_a\0\0"
  0x804: magic[4]        = "BCAB"
  0x808: version         = 01 02
  0x80C: 未知 (FF 00 0F 00 ...)
  0x81C: 疑似 CRC32      = 96 B1 8D 84 (小端 -> 0x848DB196)

目标: 找到 "哪个范围 + 哪个多项式" 能算出 0x848DB196, 以便 UEFI 改写
      slot_suffix 后能重算正确的校验值 (校验错则 U-Boot 可能拒绝并回退)。
"""
import sys
import zlib
import struct

PATH = sys.argv[1] if len(sys.argv) > 1 else "images/misc"
d = open(PATH, "rb").read()

target = struct.unpack_from("<I", d, 0x81C)[0]
print("misc @0x81C 校验字段 = 0x%08X" % target)
print("(若结构长度不是 0x1C, 下面会全部 miss, 需调整偏移)\n")

CANDIDATES = {
    "0x800..0x81C (28B, magic 起也在内)": d[0x800:0x81C],
    "0x804..0x81C (24B, 从 BCAB 起)": d[0x804:0x81C],
    "0x800..0x820 (含校验位全 0)": d[0x800:0x81C] + b"\0" * 4,
    "0x800..0x1000 (整 4KB)": d[0x800:0x1000],
    "0x800..0x81B (27B)": d[0x800:0x81B],
    "0x804..0x81B (23B)": d[0x804:0x81B],
}

for name, data in CANDIDATES.items():
    c = zlib.crc32(data) & 0xFFFFFFFF
    mark = "  <== 命中!" if c == target else ""
    print("  crc32(%s) = 0x%08X%s" % (name, c, mark))

print("\n附加: 逐字节打印 0x800..0x820")
print("  " + " ".join("%02X" % b for b in d[0x800:0x820]))
