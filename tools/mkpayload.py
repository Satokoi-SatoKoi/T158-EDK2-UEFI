#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mkpayload.py —— 把 EDK2 编译出的 FD 打包成展锐 T158 原厂 U-Boot 能启动的载荷

背景 (对某 UMS9621 设备的原厂固件实测):
  * 原厂 boot_a 是 Android boot.img header v4, kernel 位于偏移 0x1000,
    是未压缩的 arm64 Image; 其 magic 实测为 "ARMd"(不是主线的 "ARM64")。
  * kernel_size = 0x2C96A00, bootconfig_size = 0x630, header_version = 4, page = 4096。
  * 原厂 u-boot 没有 efi_loader(二进制里无 bootefi), 所以只能以 "内核载荷" 方式接管。

用法:
  # 1) 生成 boot.img (可直接 fastboot boot / fastboot flash boot_a)
  py -3 tools/mkpayload.py bootimg -i Build/T158/DEBUG_GCC5/FV/T158.fd -o payload.img

  # 2) 只生成裸载荷 (供 ResearchDownload / 其它封装方式使用)
  py -3 tools/mkpayload.py raw -i T158.fd -o uefi.bin

  # 3) 顺手生成配套的 vendor_boot.img (含 DTB)
  py -3 tools/mkpayload.py vendorboot -o vendor_boot_uefi.img --dtb dtb_ums9621.dtb

  # 4) 查看一个 boot.img
  py -3 tools/mkpayload.py info -i boot_a
"""

import argparse
import os
import struct
import sys

PAGE = 4096
BOOT_MAGIC = b"ANDROID!"
VNDR_MAGIC = b"VNDRBOOT"

# 实测: 原厂内核镜像 magic 为 "ARMd"; 主线标准是 "ARM64"
MAGIC_ARM_D = b"ARMd"
MAGIC_ARM64 = b"ARM64"

# FFS 文件类型
FFS_TYPE_SECURITY_CORE = 0x03


def rd(path):
    with open(path, "rb") as f:
        return f.read()


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def align_up(v, a=PAGE):
    return (v + a - 1) & ~(a - 1)


# --------------------------------------------------------------------------- FV 解析
def find_sec_entry(fd, fd_base):
    """
    在 FV (FD 偏移 0) 中定位 SEC 文件的 PE32/TE 段, 返回其 (物理入口地址, FD 内偏移)。
    XIP 假设: PE32 的 ImageBase 即其物理地址, 与 FD 内偏移一一对应。

    为兼容不同 edk2 版本在 EFI_FFS_FILE_HEADER2 / SECTION2 头部长度上的差异,
    这里对候选头部长度逐一尝试, 用 "入口地址必须落在 FD 范围内" 做校验。
    """
    # EFI_FIRMWARE_VOLUME_HEADER: Signature @0x28 ("_FVH"), HeaderLength @0x30 (u16)
    if len(fd) < 0x40 or u32(fd, 0x28) != 0x4856465F:
        raise SystemExit("输入不是 FV (0x28 处缺少 _FVH 签名), 请确认传入的是 .fd 文件")

    hdr_len = struct.unpack_from("<H", fd, 0x30)[0]
    if hdr_len == 0 or hdr_len > len(fd):
        hdr_len = 0x48

    off = align_up(hdr_len, 8)
    while off + 24 <= len(fd):
        ftype = fd[off + 18]
        attrs = fd[off + 19]
        is_file2 = (attrs & 0x01) != 0
        if is_file2:
            size = u32(fd, off + 20)
            hdr_candidates = (28, 25, 24)
        else:
            size = (fd[off + 20] | (fd[off + 21] << 8) | (fd[off + 22] << 16))
            hdr_candidates = (24,)
        if size == 0 or size > (len(fd) - off):
            break

        if ftype == FFS_TYPE_SECURITY_CORE:
            for hdr in hdr_candidates:
                soff = off + hdr
                while soff + 4 <= min(off + size, len(fd)):
                    ssize = fd[soff] | (fd[soff + 1] << 8) | (fd[soff + 2] << 16)
                    stype = fd[soff + 3]
                    is_sec2 = (ssize == 0xFFFFFF)
                    if is_sec2:
                        if soff + 8 > len(fd):
                            break
                        ssize = u32(fd, soff + 4)
                        shdr = 8
                    else:
                        shdr = 4
                    if ssize == 0:
                        break

                    if stype in (0x10, 0x12):        # PE32 / TE
                        data = soff + shdr
                        entry, image_base = pe_entry(fd, data, stype == 0x12)
                        if entry is not None:
                            rel = entry - fd_base
                            if 0 <= rel < len(fd):
                                return entry, rel

                    soff += align_up(ssize, 4)

        off += align_up(size, 8)

    raise SystemExit("未在 FV 中找到 SEC 的 PE32 段 (请确认 .fd 的 FV 布局)")


def pe_entry(buf, off, is_te=False):
    """解析 PE/COFF (或 TE) 的入口点与映像基址"""
    try:
        if is_te:
            if buf[off:off + 2] != b"VZ":
                return None, None
            # TE: Signature(2) Machine(2) NumberOfSections(1) Subsystem(1)
            #     StrippedSize(2) AddressOfEntryPoint(4) BaseOfCode(4) ImageBase(8)
            entry = u32(buf, off + 8)
            image_base = u64(buf, off + 16)
            return entry + image_base, image_base

        if buf[off:off + 2] != b"MZ":
            return None, None
        e_lfanew = u32(buf, off + 0x3C)
        pe = off + e_lfanew
        if buf[pe:pe + 4] != b"PE\0\0":
            return None, None
        n_opt = struct.unpack_from("<H", buf, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from("<H", buf, opt)[0]
        if magic == 0x20B:            # PE32+
            entry = u32(buf, opt + 16)
            image_base = u64(buf, opt + 24)
        else:                          # PE32
            entry = u32(buf, opt + 16)
            image_base = u32(buf, opt + 28)
        return entry + image_base, image_base
    except Exception:
        return None, None


# ------------------------------------------------------------------ ARM64 载荷头
def make_arm64_header(branch_offset, image_size, magic=MAGIC_ARM_D):
    """
    ARM64 Linux 内核镜像头 (64 字节), 第一句是跳到 SEC 入口的分支指令。
    """
    imm = branch_offset // 4
    if not (0 <= imm < (1 << 26)):
        raise SystemExit("分支偏移超出 26 位有符号范围 (固件太大?)")
    branch = 0x14000000 | (imm & 0x03FFFFFF)

    h = bytearray(0x40)
    struct.pack_into("<I", h, 0x00, branch)          # b <stub>
    struct.pack_into("<I", h, 0x04, 0)               # reserved
    struct.pack_into("<Q", h, 0x08, 0)               # text_offset: 自身会搬迁, 填 0
    struct.pack_into("<Q", h, 0x10, image_size)      # image_size
    struct.pack_into("<Q", h, 0x18, 0x0A)            # flags: LE + 4K 页 + 可任意放置
    struct.pack_into("<I", h, 0x38, struct.unpack("<I", magic)[0])
    struct.pack_into("<I", h, 0x3C, 0x40)            # PE header offset
    return bytes(h)


def build_raw_payload(fd, fd_base, magic=MAGIC_ARM_D):
    entry, entry_off = find_sec_entry(fd, fd_base)
    payload_off = 0x40 + entry_off                    # 头部占 0x40 字节
    total = 0x40 + len(fd)
    hdr = make_arm64_header(payload_off, total, magic)
    return hdr + fd, entry, total


# ------------------------------------------------------------------ boot.img v4
def build_bootimg_v4(payload, cmdline="", kernel_addr=0, ramdisk_addr=0,
                     android_kernel=b""):
    """
    payload          : UEFI 裸载荷 -> kernel 段
    android_kernel   : 可选, 原厂安卓的未压缩 ARMd Image -> ramdisk 段。
                       塞进去后 payload 自包含 (UEFI 常驻 boot_a 也能启动安卓,
                       不依赖 boot_b), UEFI 端用 T158LinuxBootFromPayload 读出。
    """
    hdr = bytearray(PAGE)
    hdr[0:8] = BOOT_MAGIC
    struct.pack_into("<I", hdr, 8, len(payload))         # kernel_size
    struct.pack_into("<I", hdr, 12, len(android_kernel)) # ramdisk_size (内嵌安卓 kernel)
    struct.pack_into("<I", hdr, 16, 0)                   # vendor_ramdisk_size
    struct.pack_into("<I", hdr, 20, 0)                   # bootconfig_size
    struct.pack_into("<I", hdr, 24, kernel_addr)         # kernel_addr
    struct.pack_into("<I", hdr, 28, ramdisk_addr)        # ramdisk_addr
    struct.pack_into("<I", hdr, 32, 0)                   # vendor_ramdisk_addr
    struct.pack_into("<I", hdr, 36, 0)                   # bootconfig_addr
    struct.pack_into("<I", hdr, 40, 4)                   # header_version = 4
    struct.pack_into("<I", hdr, 44, PAGE)                # page_size (v4 固定 4096)
    cl = cmdline.encode("ascii")[:2047]
    hdr[48:48 + len(cl)] = cl
    out = bytes(hdr) + payload + b"\0" * (align_up(len(payload)) - len(payload))
    if android_kernel:
        out += android_kernel + b"\0" * (align_up(len(android_kernel)) - len(android_kernel))
    return out


def extract_android_kernel(bootimg):
    """从原厂安卓 boot.img 提取 kernel 段 (v4: 偏移 0x1000 起 kernel_size 字节)"""
    if bootimg[:8] != BOOT_MAGIC:
        raise SystemExit("-k 输入不是 boot.img (缺少 ANDROID! 头)")
    hv = u32(bootimg, 40)
    if hv not in (3, 4):
        raise SystemExit("-k 输入 header_version=%d, 仅支持 v3/v4" % hv)
    ks = u32(bootimg, 8)
    if ks == 0 or ks > 200 * 1024 * 1024:
        raise SystemExit("-k 输入 kernel_size=%d 异常" % ks)
    k = bootimg[PAGE:PAGE + ks]
    if len(k) < 0x3C or k[0x38:0x3C] not in (MAGIC_ARM_D, MAGIC_ARM64):
        raise SystemExit("-k 输入的 kernel 是压缩的或 magic 不对 (0x38 处非 ARMd/ARM64)")
    return k


def build_vendorboot_v4(dtb=b"", cmdline="console=ttyS1,115200",
                        kernel_addr=0x8000, ramdisk_addr=0x5400000):
    """与原厂 vendor_boot_a 同构: v4 头 + (vendor ramdisk 为空) + DTB"""
    hdr = bytearray(PAGE)
    hdr[0:8] = VNDR_MAGIC
    struct.pack_into("<I", hdr, 8, 4)                  # header_version
    struct.pack_into("<I", hdr, 12, PAGE)              # page_size
    struct.pack_into("<I", hdr, 16, kernel_addr)       # kernel_addr
    struct.pack_into("<I", hdr, 20, ramdisk_addr)      # ramdisk_addr
    struct.pack_into("<I", hdr, 24, 0)                 # vendor_ramdisk_size
    cl = cmdline.encode("ascii")[:2047]
    hdr[28:28 + len(cl)] = cl
    struct.pack_into("<I", hdr, 2076, 0)               # vendor_ramdisk_table_size
    struct.pack_into("<I", hdr, 2080, 0)               # entry_num
    struct.pack_into("<I", hdr, 2084, 0)               # entry_size
    out = bytes(hdr)
    if dtb:
        out += dtb + b"\0" * (align_up(len(dtb)) - len(dtb))
    return out


# --------------------------------------------------------------------------- CLI
def cmd_bootimg(a):
    fd = rd(a.input)
    payload, entry, total = build_raw_payload(fd, a.fd_base,
                                              MAGIC_ARM_D if a.magic == "ARMd" else MAGIC_ARM64)
    ak = b""
    if a.embed_android:
        ak = extract_android_kernel(rd(a.embed_android))
    img = build_bootimg_v4(payload, a.cmdline, android_kernel=ak)
    with open(a.output, "wb") as f:
        f.write(img)
    print("SEC 入口        : 0x%X" % entry)
    print("载荷大小        : %d (0x%X) 字节" % (total, total))
    print("boot.img (v4)   : %s  (%d 字节)" % (a.output, len(img)))
    print("  kernel_size   = 0x%X  (UEFI, magic=%s)" % (len(payload), a.magic))
    if ak:
        print("  ramdisk_size  = 0x%X  (内嵌安卓 kernel, 不依赖 boot_b)" % len(ak))
    else:
        print("  ramdisk_size  = 0")


def cmd_raw(a):
    fd = rd(a.input)
    payload, entry, total = build_raw_payload(fd, a.fd_base,
                                              MAGIC_ARM_D if a.magic == "ARMd" else MAGIC_ARM64)
    with open(a.output, "wb") as f:
        f.write(payload)
    print("裸载荷: %s (%d 字节, SEC 入口 0x%X)" % (a.output, total, entry))


def cmd_vendorboot(a):
    dtb = rd(a.dtb) if a.dtb else b""
    img = build_vendorboot_v4(dtb)
    with open(a.output, "wb") as f:
        f.write(img)
    print("vendor_boot.img : %s (%d 字节, DTB %d 字节)" % (a.output, len(img), len(dtb)))


def cmd_info(a):
    d = rd(a.input)
    if d[:8] != BOOT_MAGIC:
        print("非 boot.img, 前 8 字节:", d[:8])
        return
    ks = u32(d, 8)
    ka = u32(d, 24)
    hv = u32(d, 40)
    print("boot.img header_version = %d" % hv)
    print("kernel_size   = 0x%X (%d)" % (ks, ks))
    print("kernel_addr   = 0x%X" % ka)
    print("cmdline       = %s" % d[48:560].rstrip(b"\0").decode("ascii", "replace"))
    k = d[PAGE:PAGE + 0x40]
    print("kernel[0:16]  = %s" % " ".join("%02X" % c for c in k[:16]))
    print("kernel magic  = %r (0x38 处)" % k[0x38:0x3C])


def main():
    ap = argparse.ArgumentParser(description="T158 UEFI 载荷打包工具")
    ap.add_argument("cmd", choices=["bootimg", "raw", "vendorboot", "info"])
    ap.add_argument("-i", "--input")
    ap.add_argument("-o", "--output")
    ap.add_argument("--fd-base", type=lambda x: int(x, 0), default=0xA9000000,
                    help="FD 的链接/运行基址, 默认 0xA9000000 (与 PcdFdBaseAddress 一致)")
    ap.add_argument("--magic", choices=["ARMd", "ARM64"], default="ARMd",
                    help="ARM64 镜像 magic: 原厂实测为 ARMd")
    ap.add_argument("--cmdline", default="console=ttyS1,115200 earlycon")
    ap.add_argument("--dtb", help="vendor_boot 中要附带的 DTB 文件")
    ap.add_argument("-k", "--embed-android", metavar="BOOT_IMG",
                    help="把原厂安卓 boot.img 的 kernel 段内嵌进 payload 的 ramdisk 段"
                         " (自包含部署: 只刷 boot_a 即可启动安卓, 不依赖 boot_b)")
    a = ap.parse_args()

    {"bootimg": cmd_bootimg, "raw": cmd_raw,
     "vendorboot": cmd_vendorboot, "info": cmd_info}[a.cmd](a)


if __name__ == "__main__":
    main()
