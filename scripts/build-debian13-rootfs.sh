#!/bin/bash
# =============================================================================
# build-debian13-rootfs.sh — Debian 13 (trixie) arm64 rootfs for Xiaomi Mi 4c (libra)
# -----------------------------------------------------------------------------
# Bootstraps a command-line Debian 13 root filesystem and packages it as an
# ext4 image to be flashed to the device's userdata partition (mmcblk1p44).
#
# Runs on an arm64 runner (ubuntu-24.04-arm) so no qemu is needed.
# Output: debian-trixie-arm64.img (ext4, default 2G; resized to fill the
#         partition on first boot by a bundled systemd oneshot).
#
# Access after boot:
#   - serial console: /dev/ttyGS0 (USB), getty auto-started from console=
#   - LCD console:    /dev/tty0   (fbcon)
#   - ssh:            enabled (once networking is available)
# =============================================================================
set -euo pipefail

SUITE="${SUITE:-trixie}"
ARCH="${ARCH:-arm64}"
MIRROR="${MIRROR:-http://deb.debian.org/debian}"
ROOTFS_DIR="${ROOTFS_DIR:-rootfs}"
IMAGE_SIZE="${IMAGE_SIZE:-2G}"
IMAGE_FILE="${IMAGE_FILE:-debian-trixie-arm64.img}"
ROOT_PASS="${ROOT_PASS:-libra}"
HOSTNAME="${HOSTNAME:-libra}"
ROOT_DEV="${ROOT_DEV:-/dev/mmcblk1p44}"   # libra eMMC userdata
TZ="${TZ:-Asia/Shanghai}"
DEBIAN_FRONTEND=noninteractive
export DEBIAN_FRONTEND

echo "==> [1/4] Install bootstrap tooling"
apt-get update
apt-get install -y --no-install-recommends mmdebstrap debootstrap e2fsprogs \
    debian-archive-keyring

echo "==> [2/4] Bootstrap Debian ${SUITE} (${ARCH})"
rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"

if command -v mmdebstrap >/dev/null; then
    echo "    using mmdebstrap"
    mmdebstrap --variant=minbase --arch="${ARCH}" \
        --keyring=/usr/share/keyrings/debian-archive-keyring.gpg \
        "${SUITE}" "${ROOTFS_DIR}" "${MIRROR}"
else
    echo "    using debootstrap"
    debootstrap --arch="${ARCH}" --variant=minbase \
        --keyring=/usr/share/keyrings/debian-archive-keyring.gpg \
        "${SUITE}" "${ROOTFS_DIR}" "${MIRROR}"
fi

echo "==> [3/4] Configure rootfs"
# bind-mount pseudo filesystems so package postinst scripts work inside chroot
for m in /proc /sys /dev; do
    mount --bind "$m" "${ROOTFS_DIR}${m}" 2>/dev/null || true
done
# network resolution for apt inside the chroot
if mountpoint -q "${ROOTFS_DIR}/etc/resolv.conf" 2>/dev/null; then
    umount "${ROOTFS_DIR}/etc/resolv.conf" 2>/dev/null || true
fi
mount --bind /etc/resolv.conf "${ROOTFS_DIR}/etc/resolv.conf" 2>/dev/null || \
    cp /etc/resolv.conf "${ROOTFS_DIR}/etc/resolv.conf"
cleanup() {
    umount "${ROOTFS_DIR}/etc/resolv.conf" 2>/dev/null || true
    umount "${ROOTFS_DIR}/dev" 2>/dev/null || true
    umount "${ROOTFS_DIR}/sys" 2>/dev/null || true
    umount "${ROOTFS_DIR}/proc" 2>/dev/null || true
}
trap cleanup EXIT

# pass variables through into the chroot so inner heredocs expand them
export ROOT_PASS HOSTNAME TZ ROOT_DEV

chroot "${ROOTFS_DIR}" /bin/bash <<'EOF'
set -e
export DEBIAN_FRONTEND=noninteractive
# --- identity / basics ---
echo "${HOSTNAME}" > /etc/hostname
echo "127.0.1.1 ${HOSTNAME}" >> /etc/hosts
ln -sf /usr/share/zoneinfo/"${TZ}" /etc/localtime
echo "root:${ROOT_PASS}" | chpasswd

# --- fstab ---
cat > /etc/fstab <<FSTAB
${ROOT_DEV}  /  ext4  errors=remount-ro  0 1
FSTAB

# --- extra packages (command-line focus) ---
apt-get update
apt-get install -y --no-install-recommends \
    openssh-server openssh-client \
    iproute2 procps psmisc bash-completion \
    less nano htop curl wget ca-certificates \
    e2fsprogs file
EOF

# --- enable services without a running systemd ---
systemctl --root="${ROOTFS_DIR}" enable ssh 2>/dev/null || true
# getty on the USB serial console (systemd's getty-generator also does this from console=)
ln -sf /lib/systemd/system/serial-getty@.service \
    "${ROOTFS_DIR}/etc/systemd/system/getty.target.wants/serial-getty@ttyGS0.service" 2>/dev/null || true

# --- one-shot resize of the root filesystem to fill the partition on first boot ---
cat > "${ROOTFS_DIR}/etc/systemd/system/resize-root.service" <<EOF
[Unit]
Description=Resize root filesystem to fill partition
DefaultDependencies=no
After=systemd-remount-fs.service
Before=local-fs.target
ConditionPathIsReadWrite=/

[Service]
Type=oneshot
ExecStart=/sbin/resize2fs ${ROOT_DEV}
ExecStart=/bin/true

[Install]
WantedBy=local-fs.target
EOF
systemctl --root="${ROOTFS_DIR}" enable resize-root 2>/dev/null || true

# --- clean up so systemd regenerates identity on first boot ---
rm -f "${ROOTFS_DIR}/etc/machine-id"
rm -f "${ROOTFS_DIR}/var/lib/dbus/machine-id"
rm -rf "${ROOTFS_DIR}/var/cache/apt/archives"/* 2>/dev/null || true

trap - EXIT
cleanup

echo "==> [4/4] Build ext4 image (${IMAGE_SIZE})"
rm -f "${IMAGE_FILE}"
truncate -s "${IMAGE_SIZE}" "${IMAGE_FILE}"
mkfs.ext4 -q -F -L debian -d "${ROOTFS_DIR}" "${IMAGE_FILE}"

echo "==> Done: ${IMAGE_FILE}"
ls -lh "${IMAGE_FILE}"
