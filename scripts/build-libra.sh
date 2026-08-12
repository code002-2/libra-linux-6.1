#!/bin/bash
# =============================================================================
# build-libra.sh — Xiaomi Mi 4c (libra, MSM8992) mainline boot image build
# -----------------------------------------------------------------------------
# Builds, from THIS kernel source tree (Linux 6.1 + libra bring-up patches):
#   1. arm64 kernel image          -> arch/arm64/boot/Image.gz
#   2. libra device tree blob      -> arch/arm64/boot/dts/qcom/msm8992-xiaomi-libra.dtb
#   3. minimal initramfs           -> initramfs.cpio.gz
#        (Alpine aarch64 minirootfs + initramfs/init)
#   4. Android boot image          -> boot.img
#        (kernel with appended DTB + initramfs; lk2nd finds the appended DTB
#         by scanning for the FDT magic and passes it to the kernel)
#
# Intended to run on GitHub Actions (ubuntu-24.04) with:
#   gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu cpio curl xz-utils
#
# Outputs (relative to repo root):
#   boot.img          <- flash with: fastboot flash boot boot.img
#   Image.gz-dtb      <- kernel + appended DTB (debugging / extlinux use)
#   initramfs.cpio.gz <- the initramfs (debugging / extlinux use)
# =============================================================================
set -euo pipefail

cd "$(dirname "$0")/.."          # repo root

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
JOBS="${JOBS:-$(nproc)}"

DTC_DTB="arch/arm64/boot/dts/qcom/msm8992-xiaomi-libra.dtb"

# MSM8992/8994 (bullhead/angler) standard Android boot image layout.
BOOT_BASE=0x00000000
BOOT_KERNEL_OFFSET=0x00008000
BOOT_RAMDISK_OFFSET=0x02000000
BOOT_TAGS_OFFSET=0x01e00000
BOOT_PAGESIZE=2048
# Default: all 4 Cortex-A53 cores (stable). A57 cluster (CPU4/5) hangs on PSCI
# CPU_ON — needs cluster power/clock init not present in lk2nd or the kernel.
# Override with BOOT_CMDLINE="... maxcpus=6" to test (reversible).
# loglevel=5: shows the boot logo (one Tux per online CPU) while suppressing
# KERN_INFO, so the penguins stay on screen; loglevel<=4 hides the logo.
BOOT_CMDLINE="${BOOT_CMDLINE:-console=ttyGS0,115200n8 maxcpus=4 loglevel=5}"

echo "==> [1/4] Configure kernel (olddefconfig on committed .config)"
make olddefconfig

echo "==> [2/4] Build kernel Image.gz + libra DTB"
make -j"$JOBS" Image.gz
make -j"$JOBS" dtbs

echo "==> [3/4] Build initramfs (Alpine minirootfs + initramfs/init)"
ALPINE_INDEX_URL="https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/"
MINIROOTFS_NAME="$(curl -fsSL "$ALPINE_INDEX_URL" \
    | grep -oE 'alpine-minirootfs-[0-9.]+-aarch64\.tar\.gz' | sort -V | tail -1)"
if [ -z "${MINIROOTFS_NAME:-}" ]; then
    echo "ERROR: could not resolve Alpine minirootfs from $ALPINE_INDEX_URL" >&2
    exit 1
fi
MINIROOTFS_URL="${ALPINE_INDEX_URL}${MINIROOTFS_NAME}"

rm -rf initramfs-root initramfs.cpio.gz initramfs.cpio.gz.tmp
mkdir -p initramfs-root
echo "    downloading ${MINIROOTFS_URL}"
curl -fsSL "$MINIROOTFS_URL" -o alpine-minirootfs.tar.gz
tar -xzf alpine-minirootfs.tar.gz -C initramfs-root
install -m 0755 initramfs/init initramfs-root/init
( cd initramfs-root && find . -print | cpio -o -H newc 2>/dev/null | gzip > ../initramfs.cpio.gz )

echo "==> [4/4] Pack boot.img"
cat "arch/arm64/boot/Image.gz" "$DTC_DTB" > Image.gz-dtb
python3 scripts/mkbootimg.py \
    --kernel Image.gz-dtb \
    --ramdisk initramfs.cpio.gz \
    --base "$BOOT_BASE" \
    --kernel_offset "$BOOT_KERNEL_OFFSET" \
    --ramdisk_offset "$BOOT_RAMDISK_OFFSET" \
    --tags_offset "$BOOT_TAGS_OFFSET" \
    --pagesize "$BOOT_PAGESIZE" \
    --cmdline "$BOOT_CMDLINE" \
    -o boot.img

echo "==> Done"
ls -lh boot.img Image.gz-dtb initramfs.cpio.gz "$DTC_DTB"
