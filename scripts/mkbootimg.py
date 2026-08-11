#!/usr/bin/env python3
# =============================================================================
# mkbootimg.py — minimal Android boot image (v0) packer for Xiaomi Mi 4c (libra)
# -----------------------------------------------------------------------------
# Self-contained, no external deps. Produces the classic boot image v0 that
# lk2nd understands (same format as AOSP's mkbootimg --header_version 0).
#
# lk2nd on libra defines ABOOT_IGNORE_BOOT_HEADER_ADDRS, so the address fields
# are ignored at boot time — they are filled with the standard MSM8992/8994
# values for correctness anyway.
#
# Usage: mkbootimg.py --kernel Image.gz-dtb --ramdisk initramfs.cpio.gz \
#                     --cmdline "..." --pagesize 2048 -o boot.img
# =============================================================================
import argparse
import struct

MAGIC = b"ANDROID!"
HEADER_VERSION = 0


def align(x, a):
    return (x + a - 1) & ~(a - 1)


def main():
    p = argparse.ArgumentParser(description="Minimal Android boot image v0 packer")
    p.add_argument("--kernel", required=True, help="kernel image (Image.gz-dtb)")
    p.add_argument("--ramdisk", required=True, help="ramdisk image (initramfs.cpio.gz)")
    p.add_argument("--base", default="0x00000000")
    p.add_argument("--kernel_offset", default="0x00008000")
    p.add_argument("--ramdisk_offset", default="0x02000000")
    p.add_argument("--tags_offset", default="0x01e00000")
    p.add_argument("--pagesize", type=int, default=2048)
    p.add_argument("--cmdline", default="")
    p.add_argument("-o", "--output", required=True)
    args = p.parse_args()

    kernel = open(args.kernel, "rb").read()
    ramdisk = open(args.ramdisk, "rb").read()
    page = args.pagesize
    base = int(args.base, 0)
    kernel_addr = (base + int(args.kernel_offset, 0)) & 0xFFFFFFFF
    ramdisk_addr = (base + int(args.ramdisk_offset, 0)) & 0xFFFFFFFF
    tags_addr = (base + int(args.tags_offset, 0)) & 0xFFFFFFFF

    cmdline = args.cmdline.encode("utf-8")
    if len(cmdline) >= 512:
        raise SystemExit("ERROR: cmdline too long (%d bytes, max 511)" % len(cmdline))
    cmdline += b"\x00" * (512 - len(cmdline))

    # boot_img_hdr (v0): magic[8] + 10*u32 + name[16] + cmdline[512] + id[8] + extra[1024]
    hdr = struct.pack(
        "<8sIIIIIIIIII16s512s8I1024s",
        MAGIC,
        len(kernel), kernel_addr,
        len(ramdisk), ramdisk_addr,
        0, 0,                      # second_size, second_addr
        tags_addr,
        page,
        HEADER_VERSION,
        0,                         # os_version
        b"\x00" * 16,              # name
        cmdline,
        0, 0, 0, 0, 0, 0, 0, 0,    # id[8]
        b"\x00" * 1024,            # extra_cmdline
    )
    if len(hdr) != 1632:
        raise SystemExit("ERROR: header size %d != 1632" % len(hdr))

    out = bytearray(hdr)
    out += b"\x00" * (align(len(out), page) - len(out))   # pad header to page
    out += kernel
    out += b"\x00" * (align(len(kernel), page) - len(kernel))
    out += ramdisk
    out += b"\x00" * (align(len(ramdisk), page) - len(ramdisk))

    with open(args.output, "wb") as f:
        f.write(out)
    print("boot.img: %d bytes (kernel %d, ramdisk %d, page %d)"
          % (len(out), len(kernel), len(ramdisk), page))


if __name__ == "__main__":
    main()
