#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_mkpayload.py —— 用合成的最小 FV 验证 mkpayload.py 的 FV/FFS/PE 解析与打包逻辑
(不需要 EDK2 构建环境即可自测)

用法: py -3 tools/test_mkpayload.py
"""
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkpayload as M  # noqa: E402

FD_BASE = 0xA9000000
BLOCK_SIZE = 0x1000


def make_pe32(entry_rva=0x200, image_size=0x1000):
    """最小 PE32+ 映像: MZ -> e_lfanew -> PE -> COFF -> Optional(PE32+)"""
    img = bytearray(0x400)
    img[0:2] = b"MZ"
    struct.pack_into("<I", img, 0x3C, 0x40)          # e_lfanew
    struct.pack_into("<I", img, 0x40, 0x00004550)    # "PE\0\0"
    # COFF header @0x44: Machine(2) NumberOfSections(2) TimeDateStamp(4)
    #                    PointerToSymbolTable(4) NumberOfSymbols(4)
    #                    SizeOfOptionalHeader(2) Characteristics(2)
    struct.pack_into("<H", img, 0x44, 0xAA64)
    struct.pack_into("<H", img, 0x46, 1)
    struct.pack_into("<H", img, 0x54, 0xF0)          # SizeOfOptionalHeader = 240
    struct.pack_into("<H", img, 0x56, 0x0022)
    opt = 0x58
    struct.pack_into("<H", img, opt, 0x20B)          # PE32+
    struct.pack_into("<I", img, opt + 16, entry_rva)             # AddressOfEntryPoint
    struct.pack_into("<Q", img, opt + 24, 0)         # ImageBase (占位, 稍后回填)
    struct.pack_into("<I", img, opt + 56, image_size)            # SizeOfImage
    struct.pack_into("<I", img, opt + 60, 0x200)                 # SizeOfHeaders
    return bytes(img)


def make_fd():
    pe = bytearray(make_pe32())
    # 构造: [FV header 0x48][FFS(SEC): hdr 24B][section hdr 4B][PE32]
    fv_hdr = bytearray(0x48)
    struct.pack_into("<Q", fv_hdr, 0x20, 0x100000)          # FvLength
    struct.pack_into("<I", fv_hdr, 0x28, 0x4856465F)        # Signature "_FVH"
    struct.pack_into("<H", fv_hdr, 0x30, 0x48)              # HeaderLength
    struct.pack_into("<H", fv_hdr, 0x34, 0)                 # ExtHeaderOffset
    fv_hdr[0x38] = 2                                        # Revision
    struct.pack_into("<I", fv_hdr, 0x38 + 1, 0)             # (padding)
    struct.pack_into("<I", fv_hdr, 0x40, 0x100)             # BlockMap[0].NumBlocks
    struct.pack_into("<I", fv_hdr, 0x44, BLOCK_SIZE)        # BlockMap[0].Length

    sec_off = 0x48
    ffs_hdr = bytearray(0x18)
    ffs_hdr[0:16] = bytes(range(16))                        # Name GUID (dummy)
    ffs_hdr[16] = 0xAA                                      # IntegrityCheck.header
    ffs_hdr[17] = 0x00
    ffs_hdr[18] = M.FFS_TYPE_SECURITY_CORE                  # Type = SEC
    ffs_hdr[19] = 0x00                                      # Attributes
    file_size = 0x18 + 4 + len(pe)
    ffs_hdr[20] = file_size & 0xFF
    ffs_hdr[21] = (file_size >> 8) & 0xFF
    ffs_hdr[22] = (file_size >> 16) & 0xFF
    ffs_hdr[23] = 0xF8                                      # State

    sec_hdr = bytearray(4)
    sec_size = 4 + len(pe)
    sec_hdr[0] = sec_size & 0xFF
    sec_hdr[1] = (sec_size >> 8) & 0xFF
    sec_hdr[2] = (sec_size >> 16) & 0xFF
    sec_hdr[3] = 0x10                                       # EFI_SECTION_PE32

    pe_phys = FD_BASE + sec_off + 0x18 + 4
    struct.pack_into("<Q", pe, 0x58 + 24, pe_phys)          # ImageBase = 物理地址 (XIP)

    return bytes(fv_hdr) + bytes(ffs_hdr) + bytes(sec_hdr) + bytes(pe)


def main():
    fd = make_fd()
    entry, rel = M.find_sec_entry(fd, FD_BASE)
    print("SEC 入口物理地址 = 0x%X, FD 内偏移 = 0x%X" % (entry, rel))
    assert entry == (FD_BASE + 0x48 + 0x18 + 4 + 0x200), "入口地址计算错误"
    assert 0 < rel < len(fd), "偏移越界"

    tmp = tempfile.mkdtemp()
    fd_path = os.path.join(tmp, "T158.fd")
    with open(fd_path, "wb") as f:
        f.write(fd)

    payload, ent, total = M.build_raw_payload(fd, FD_BASE, M.MAGIC_ARM_D)
    assert payload[0x38:0x3C] == M.MAGIC_ARM_D, "magic 错误"
    assert len(payload) == 0x40 + len(fd), "载荷长度错误"
    # 校验分支指令: b #(0x40 + rel)
    ins = struct.unpack_from("<I", payload, 0)[0]
    imm = ins & 0x03FFFFFF
    assert (ins >> 26) == 0b000101, "首指令不是 b"
    assert imm * 4 == 0x40 + rel, "分支偏移错误: 0x%X != 0x%X" % (imm * 4, 0x40 + rel)
    print("ARM64 头: magic=%r 分支->0x%X image_size=0x%X" %
          (payload[0x38:0x3C], imm * 4, struct.unpack_from("<Q", payload, 0x10)[0]))

    img = M.build_bootimg_v4(payload)
    assert img[:8] == M.BOOT_MAGIC
    assert struct.unpack_from("<I", img, 40)[0] == 4
    assert struct.unpack_from("<I", img, 8)[0] == len(payload)
    assert img[0x1000:0x1004] == payload[0:4]
    print("boot.img v4: %d 字节, kernel @0x1000, magic 校验通过" % len(img))

    vb = M.build_vendorboot_v4(dtb=bytes(0x1000))
    assert vb[:8] == M.VNDR_MAGIC
    print("vendor_boot.img: %d 字节" % len(vb))

    print("\n全部自检通过 ✔")


if __name__ == "__main__":
    main()
