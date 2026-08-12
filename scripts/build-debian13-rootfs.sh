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
ROOTFS_DIR="$(cd "${ROOTFS_DIR}" && pwd)"   # absolute path (chpasswd --root needs it)

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

# --- fstab ---
cat > /etc/fstab <<FSTAB
${ROOT_DEV}  /  ext4  errors=remount-ro  0 1
FSTAB

# --- extra packages (command-line focus) ---
# NOTE: minbase ships NO init system. trixie no longer ships systemd-sysvinit,
# so /sbin/init is created manually below (symlink to systemd).
apt-get update
apt-get install -y --no-install-recommends \
    systemd dbus \
    openssh-server openssh-client \
    iproute2 procps psmisc bash-completion \
    less nano htop curl wget ca-certificates \
    e2fsprogs file
# optional/cosmetic; tolerate absence
apt-get install -y --no-install-recommends fastfetch || true
EOF

# --- /sbin/init -> systemd (PID 1) ---
ln -sf /lib/systemd/systemd "${ROOTFS_DIR}/sbin/init"

# --- root password (chpasswd --root avoids PAM in the chroot) ---
echo "root:${ROOT_PASS}" | chpasswd --root "${ROOTFS_DIR}"

# --- enable services without a running systemd ---
systemctl --root="${ROOTFS_DIR}" enable ssh 2>/dev/null || true
# getty on the USB serial console (systemd's getty-generator also does this from console=)
ln -sf /lib/systemd/system/serial-getty@.service \
    "${ROOTFS_DIR}/etc/systemd/system/getty.target.wants/serial-getty@ttyGS0.service" 2>/dev/null || true

# --- autologin root on the USB serial console (ttyGS0) ---
mkdir -p "${ROOTFS_DIR}/etc/systemd/system/serial-getty@ttyGS0.service.d"
cat > "${ROOTFS_DIR}/etc/systemd/system/serial-getty@ttyGS0.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear --keep-baud %I 115200,38400,9600 $TERM
EOF

# --- allow root SSH login with password ---
echo "PermitRootLogin yes" >> "${ROOTFS_DIR}/etc/ssh/sshd_config"

# --- USB RNDIS network gadget (device becomes a USB NIC; SSH over USB) ---
cat > "${ROOTFS_DIR}/usr/local/bin/usb-gadget-net.sh" <<'EOF'
#!/bin/sh
# Set up a USB RNDIS gadget and bring up usb0 (host: 192.168.42.1, device: 192.168.42.2)
set -e
mount -t configfs none /sys/kernel/config 2>/dev/null || true
G=/sys/kernel/config/usb_gadget/libra
if [ -d "$G" ]; then
    UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
    [ -n "$UDC" ] && echo "$UDC" > "$G/UDC" 2>/dev/null || true
else
    mkdir -p "$G"
    echo 0x1d6b > "$G/idVendor"
    echo 0x0104 > "$G/idProduct"
    mkdir -p "$G/strings/0x409"
    echo "libra" > "$G/strings/0x409/serialnumber"
    echo "Xiaomi" > "$G/strings/0x409/manufacturer"
    echo "Mi 4C" > "$G/strings/0x409/product"
    mkdir -p "$G/configs/c.1/strings/0x409"
    echo "RNDIS" > "$G/configs/c.1/strings/0x409/configuration"
    mkdir -p "$G/functions/rndis.usb0"
    ln -sf "$G/functions/rndis.usb0" "$G/configs/c.1/"
    UDC=$(ls /sys/class/udc/ 2>/dev/null | head -1)
    if [ -n "$UDC" ]; then
        echo "$UDC" > "$G/UDC"
    else
        echo "no UDC" >&2
        exit 1
    fi
fi
sleep 1
ip link set usb0 up 2>/dev/null || true
ip addr flush dev usb0 2>/dev/null || true
ip addr add 192.168.42.2/24 dev usb0 2>/dev/null || true
EOF
chmod +x "${ROOTFS_DIR}/usr/local/bin/usb-gadget-net.sh"

cat > "${ROOTFS_DIR}/etc/systemd/system/usb-gadget-net.service" <<'EOF'
[Unit]
Description=USB RNDIS network gadget
After=systemd-udevd.service systemd-modules-load.service
Wants=systemd-udevd.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/usb-gadget-net.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
systemctl --root="${ROOTFS_DIR}" enable usb-gadget-net 2>/dev/null || true

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
