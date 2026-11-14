#!/usr/bin/env python3
"""
dhtb_tool.py — 展锐 UNISOC DHTB 下载镜像 解析 / 校验 / 抽取 / 重打包 工具

针对固件格式（基于对 sl8541e_1h10 原厂 uboot.bin 的实测逆向）：

    偏移 0x00  "DHTB"  魔数
    偏移 0x04  4B      版本（实测 = 1）
    偏移 0x30  4B      载荷大小（little-endian，实测 0x00076150）
    偏移 0x200        载荷起始（ARM64 代码，b +0x28 跳过 SPL 头）
    载荷之后           尾部数据（签名/证书区）
    剩余               0x00 填充，总尺寸按下载分区对齐（实测 0x100000）

用法：
    python dhtb_tool.py info    <uboot.bin>
    python dhtb_tool.py verify  <uboot.bin>
    python dhtb_tool.py extract  <uboot.bin> -o payload.bin
    python dhtb_tool.py repack  <uboot.bin> --replace <new_payload.bin> -o out.bin

repack 规则：
    - 保留原 DHTB 头，仅刷新偏移 0x30 的载荷大小字段；
    - 尾部签名区（若存在）不随载荷复制，需按安全启动状态另行处理
      （未启用 secureboot 的量产机通常可直接 FDL 强刷）；
    - 以 0x00 填充到 --pad 指定的总尺寸，默认 0x100000。
"""

import argparse
import struct
import sys

MAGIC = b"DHTB"
HEADER_SIZE_DEFAULT = 0x200
SIZE_OFFSET = 0x30


def load(path):
    with open(path, "rb") as f:
        return f.read()


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def fmt_size(n):
    return "0x%X (%d B)" % (n, n)


def detect_header_size(data):
    """DHTB 头以首个非零载荷字节为结束。实测为 0x200，自动探测以兼容变体。"""
    for off in range(SIZE_OFFSET + 4, len(data)):
        if data[off] != 0:
            return off
    raise ValueError("文件中未发现载荷（头部之后全为 0）")


def describe_first_insns(data, hdr):
    first, second = struct.unpack_from("<II", data, hdr)
    lines = ["载荷首指令  : 0x%08X" % first, "次指令      : 0x%08X" % second]
    if (first & 0xFC000000) == 0x14000000:  # ARM64 B: 000101 imm26
        off = (first & 0x03FFFFFF) << 2
        lines[0] += "   <- ARM64 'b'，跳过 0x%X 字节头部（SPL 风格）" % off
        if (second & 0xFFFFFFFF) == 0xD503201F:
            lines[1] += "   <- NOP"
        arch = "AArch64 (ARM64)"
    else:
        arch = "未知（非标准入口）"
    lines.append("架构判断    : %s" % arch)
    return "\n".join(lines)


def cmd_info(args):
    data = load(args.bin)
    print("文件        : %s" % args.bin)
    print("总尺寸      : %s" % fmt_size(len(data)))
    if data[:4] != MAGIC:
        print("魔数        : %r  <- 不是 DHTB 镜像！" % data[:4])
        return 1
    print("魔数        : DHTB")
    print("头部版本    : %d" % u32(data, 0x04))
    hdr = detect_header_size(data)
    print("头部尺寸    : %s (自动探测)" % fmt_size(hdr))
    size = u32(data, SIZE_OFFSET)
    print("载荷大小    : %s  (头部 0x30 字段)" % fmt_size(size))
    payload_end = hdr + size
    print("载荷范围    : [0x%X .. 0x%X)" % (hdr, payload_end))
    if payload_end > len(data):
        print("载荷大小越界  <- 头部字段与文件矛盾！")
        return 1
    print(describe_first_insns(data, hdr))
    tail_end = payload_end
    for i in range(len(data) - 1, payload_end - 1, -1):
        if data[i] != 0:
            tail_end = i + 1
            break
    if tail_end > payload_end:
        print("尾部数据区  : [0x%X .. 0x%X)  %d B  (签名/证书/附加段)"
              % (payload_end, tail_end, tail_end - payload_end))
    print("零填充      : %d B" % (len(data) - max(tail_end, payload_end)))
    return 0


def cmd_verify(args):
    data = load(args.bin)
    ok = True
    if data[:4] != MAGIC:
        print("[FAIL] 魔数不是 DHTB")
        ok = False
    else:
        print("[OK]   魔数 DHTB")
    try:
        hdr = detect_header_size(data)
    except ValueError as e:
        print("[FAIL] %s" % e)
        return 1
    size = u32(data, SIZE_OFFSET)
    if hdr + size > len(data):
        print("[FAIL] 载荷 0x%X+0x%X 超出文件" % (hdr, size))
        ok = False
    else:
        print("[OK]   载荷范围合法: 0x%X+0x%X <= 0x%X" % (hdr, size, len(data)))
    if size and data[hdr] == 0:
        print("[WARN] 载荷首字节为 0，疑似损坏")
        ok = False
    print("结果        :", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_extract(args):
    data = load(args.bin)
    hdr = detect_header_size(data)
    size = u32(data, SIZE_OFFSET)
    with open(args.out, "wb") as f:
        f.write(data[hdr:hdr + size])
    print("已抽取载荷: %s  %s (来源 [0x%X..0x%X))"
          % (args.out, fmt_size(size), hdr, hdr + size))
    return 0


def cmd_repack(args):
    old = load(args.bin)
    hdr = detect_header_size(old)
    payload = load(args.replace)
    head = bytearray(old[:hdr])          # 保留原头部，仅刷新长度字段
    struct.pack_into("<I", head, SIZE_OFFSET, len(payload))
    total = args.pad
    if hdr + len(payload) > total:
        print("[FAIL] 头(0x%X)+载荷(%s) 超出目标总尺寸 %s"
              % (hdr, fmt_size(len(payload)), fmt_size(total)))
        print("        请增大 --pad（不得超过设备 uboot 分区尺寸）或对 U-Boot 瘦身。")
        return 1
    out = bytearray(total)
    out[:len(head)] = head
    out[hdr:hdr + len(payload)] = payload
    # 原厂表尾（签名/证书区）：实测位于 [旧载荷末 .. 0x76604)，
    # 内含以旧载荷大小为字段的迷你表；仅在载荷等长且 --keep-tail 时原样保留。
    old_size = u32(old, SIZE_OFFSET)
    old_tail_start = hdr + old_size
    old_tail_end = old_tail_start
    for i in range(len(old) - 1, old_tail_start - 1, -1):
        if old[i] != 0:
            old_tail_end = i + 1
            break
    kept_tail = False
    if args.keep_tail and old_tail_end > old_tail_start:
        if len(payload) == old_size:
            out[hdr + len(payload):old_tail_end] = old[old_tail_start:old_tail_end]
            kept_tail = True
        else:
            print("[WARN] 新载荷大小(0x%X)与原厂(0x%X)不等，表尾内含尺寸字段，未保留！"
                  % (len(payload), old_size))
    with open(args.out, "wb") as f:
        f.write(out)
    print("已重打包  : %s" % args.out)
    print("  DHTB 头 : %s，载荷大小字段已更新为 0x%X" % (fmt_size(hdr), len(payload)))
    print("  载荷    : %s" % fmt_size(len(payload)))
    print("  总尺寸  : %s (0 填充对齐)" % fmt_size(total))
    if kept_tail:
        print("  原厂表尾: 已原样保留 [0x%X..0x%X)（%d B）" % (old_tail_start, old_tail_end, old_tail_end - old_tail_start))
    else:
        print("注意      : 原镜像尾部签名/证书区未复制（或因载荷变长未保留）；")
        print("            secureboot 未锁机型可 FDL 直刷，否则需走原厂签名流程。")
    return 0


def main():
    p = argparse.ArgumentParser(description="UNISOC DHTB bootloader image tool")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("info", help="解析并展示镜像结构")
    sp.add_argument("bin")
    sp.set_defaults(func=cmd_info)

    sp = sub.add_parser("verify", help="校验头部与载荷一致性")
    sp.add_argument("bin")
    sp.set_defaults(func=cmd_verify)

    sp = sub.add_parser("extract", help="抽取载荷二进制")
    sp.add_argument("bin")
    sp.add_argument("-o", "--out", required=True)
    sp.set_defaults(func=cmd_extract)

    sp = sub.add_parser("repack", help="用新载荷重打包为 DHTB 镜像")
    sp.add_argument("bin", help="原厂 DHTB 镜像（提供头部）")
    sp.add_argument("--replace", required=True, help="新载荷二进制")
    sp.add_argument("-o", "--out", required=True)
    sp.add_argument("--pad", type=lambda x: int(x, 0), default=0x100000,
                    help="输出总尺寸，默认 0x100000")
    sp.add_argument("--keep-tail", action="store_true",
                    help="载荷等长替换时原样保留原厂表尾(签名/证书区)")
    sp.set_defaults(func=cmd_repack)

    args = p.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
