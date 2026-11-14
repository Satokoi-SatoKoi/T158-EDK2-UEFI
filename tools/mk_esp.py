#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mk_esp.py —— 从原厂分区镜像生成 espboot (FAT32) 需要的三个文件

用法:
    python3 tools/mk_esp.py <镜像目录> [-o 输出目录] [--no-dtb]

输入: <镜像目录> 下放原厂分区镜像 (文件名=分区名, _a 优先, 回退 _b):
      boot_a  init_boot_a  vendor_boot_a

输出 (默认 ./espboot):
    kernel   boot.img 的 kernel 段 (未压缩 ARMd Image)
    ramdisk  init_boot 的 GKI ramdisk + vendor_boot 的 vendor_ramdisk
             (cpio 顺序拼接, UEFI 端整块读入, 语义与原厂一致)
    dtb      vendor_boot 里的设备树 (可选; 不放则 UEFI 用 U-Boot 传的)

然后把输出目录里的文件拷进设备的 espboot 分区根目录即可。
"""
import argparse
import os
import struct
import sys

PAGE = 4096


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def rd(path):
    with open(path, "rb") as f:
        return f.read()


def pick(root, base):
    """base_a 优先, 回退 base_b"""
    for name in (base + "_a", base + "_b"):
        p = os.path.join(root, name)
        if os.path.isfile(p):
            return p
    return None


def find_fdt(img):
    """扫描 FDT magic (0xd00dfeed 大端), 返回 (偏移, 大小)"""
    pos = img.find(b"\xd0\x0d\xfe\xed")
    while pos != -1:
        totalsize = struct.unpack_from(">I", img, pos + 4)[0]
        if 0 < totalsize <= len(img) - pos:
            return pos, totalsize
        pos = img.find(b"\xd0\x0d\xfe\xed", pos + 4)
    return -1, 0


def main():
    ap = argparse.ArgumentParser(description="生成 espboot 的 kernel/ramdisk/dtb")
    ap.add_argument("root", nargs="?", default=".", help="分区镜像目录")
    ap.add_argument("-o", "--outdir", default="espboot", help="输出目录 (默认 ./espboot)")
    ap.add_argument("--no-dtb", action="store_true", help="不生成 dtb")
    a = ap.parse_args()

    os.makedirs(a.outdir, exist_ok=True)

    # ---- kernel: boot.img kernel 段 ----
    boot = pick(a.root, "boot")
    if not boot:
        sys.exit("找不到 boot_a/boot_b 镜像")
    b = rd(boot)
    if b[:8] != b"ANDROID!":
        sys.exit("%s 不是 boot.img" % boot)
    hv, ks = u32(b, 0x28), u32(b, 8)
    if hv not in (3, 4) or ks == 0 or ks > 160 << 20:
        sys.exit("%s header_version=%d kernel_size=%d 异常" % (boot, hv, ks))
    kernel = b[PAGE:PAGE + ks]
    if kernel[0x38:0x3C] != b"ARMd":
        sys.exit("%s 的 kernel 不是未压缩 ARMd Image (0x38 处 magic 不对)" % boot)
    with open(os.path.join(a.outdir, "kernel"), "wb") as f:
        f.write(kernel)
    print("kernel  : %d KB  <- %s (v%d)" % (len(kernel) >> 10, os.path.basename(boot), hv))

    # ---- ramdisk: init_boot ramdisk + vendor_boot vendor_ramdisk ----
    parts = []

    ib = pick(a.root, "init_boot")
    if ib:
        d = rd(ib)
        rs = u32(d, 0x0C)
        if d[:8] == b"ANDROID!" and 0 < rs <= 256 << 20:
            parts.append(d[PAGE:PAGE + rs])
            print("ramdisk += %s ramdisk %d KB" % (os.path.basename(ib), rs >> 10))
    else:
        print("提示: 没有 init_boot, 跳过 GKI ramdisk")

    vb = pick(a.root, "vendor_boot")
    if vb:
        d = rd(vb)
        vs = u32(d, 0x18)
        if d[:8] == b"VNDRBOOT" and 0 < vs <= 256 << 20:
            parts.append(d[PAGE:PAGE + vs])
            print("ramdisk += %s vendor_ramdisk %d KB" % (os.path.basename(vb), vs >> 10))
        if not a.no_dtb:
            off, size = find_fdt(d)
            if off >= 0:
                with open(os.path.join(a.outdir, "dtb"), "wb") as f:
                    f.write(d[off:off + size])
                print("dtb     : %d KB  <- %s @0x%X" % (size >> 10, os.path.basename(vb), off))
    else:
        print("提示: 没有 vendor_boot")

    if not parts:
        sys.exit("没有凑出任何 ramdisk, 检查镜像目录")
    ramdisk = b"".join(parts)
    with open(os.path.join(a.outdir, "ramdisk"), "wb") as f:
        f.write(ramdisk)
    print("ramdisk : %d KB  (共 %d 段 cpio)" % (len(ramdisk) >> 10, len(parts)))
    print("\n把 %s/ 下的三个文件拷进设备 espboot 分区根目录即可。" % a.outdir)


if __name__ == "__main__":
    main()
