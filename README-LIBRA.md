# Xiaomi Mi 4c (libra) — 主线 Linux 启动

基于 [dys0re/libra-linux-6.1](https://github.com/dys0re/libra-linux-6.1)
（Linux **6.1.177** + libra bring-up 补丁）在 GitHub Actions 上构建可刷入的 Android boot.img。

设备 bootloader 为自定义 [lk2nd-msm8992](https://gitee.com/jinchengsong/lk2nd-msm8992)
（已含 libra 板级条目，`dtb-files = "msm8992-xiaomi-libra"`）。

## 当前能力

- 内核：6.1.177，MSM8992（Snapdragon 808）6 核（默认 `maxcpus=1` 单核稳定启动）
- 已配置：eMMC、**USB gadget 串口 console（ttyGS0）**、DRM 显示 / 触摸（bring-up 中）
- 未实现：WiFi / BT / 调制解调器 / 摄像头 / 完整音频 —— 这是 bring-up 内核，不是日用发行版

## 构建

push 到本仓库或手动触发 `libra-boot` workflow，产物：
`boot.img`、`Image.gz-dtb`、`initramfs.cpio.gz`、`msm8992-xiaomi-libra.dtb`

## 刷机

前置：安装 platform-tools（提供 `fastboot`）：
- `winget install Google.PlatformTools`，或到
  https://developer.android.com/tools/releases/platform-tools 下载解压

```bash
# 1. 手机进 lk2nd fastboot：关机，按住【音量下】再开机
fastboot devices                       # 应显示设备序列号
# 2. 刷入 boot.img（lk2nd 自动以 512KiB 偏移写入，不覆盖 lk2nd 本体）
fastboot flash boot boot.img
# 3. 重启
fastboot reboot
```

> 想回到安卓：从 MIUI 线刷包里拿原版 `boot.img` 用同样命令刷回去即可。

## 看 console（验证启动）

1. 手机 USB 保持连着电脑（引导全程别拔）
2. 内核起来后，g_serial 会让 Windows 枚举出一个新的 **COM 串口**（CDC-ACM）
3. 用 PuTTY（Connection type: Serial，波特率 **115200**）连接该 COM 口
4. 成功标志：内核 log → `[libra-init] === LIBRA MAINLINE BOOT OK (6.1.177) ===`
   → 分区表 + eMMC PARTNAME 映射 → `#` shell 提示符

在 shell 里确认：
- `uname -r` → `6.1.177`
- `cat /proc/partitions` → 能看到 `mmcblk0pNN`
- 分区映射会打印 `PARTNAME`，用来确认 `p30`（内核默认 `root=/dev/mmcblk0p30`）是哪个分区

## 常见问题

| 现象 | 处理 |
|---|---|
| 电脑没有新 COM 口出现 | 确认 boot.img 的 cmdline 含 `console=ttyGS0`；试换一根只数据线的 USB 线 |
| 无任何输出 | 换未压缩 `Image` + 追加 DTB 重打包（改 `scripts/build-libra.sh`）；或查看 workflow 日志确认 DTB 是否生成 |
| 卡在多核 hang | 属已知：DTS chosen 默认 `maxcpus=1`；后续可去掉该参数测试 6 核 |
| 显示黑屏 | 预期内，DRM panel 还在 bring-up；调试走 USB 串口 |
| boot 分区装不下 | 关 `CONFIG_DEBUG_INFO` 重新构建以缩小 Image.gz |

## 技术说明（为什么这样打包）

- arm64 内核不支持 appended-DTB；DTB 由 **lk2nd 扫描内核镜像尾部的 FDT magic（`d00dfeed`）**
  找到后放 X0 传给内核 → 所以 `boot.img` 里是 `Image.gz` + 追加 `msm8992-xiaomi-libra.dtb`
- 内核 cmdline 由三部分拼接：boot.img 的 `--cmdline` + DTS `chosen/bootargs` + 内置 `CONFIG_CMDLINE`
  （`root=/dev/mmcblk0p30 console=tty0 console=ttyMSM0,115200n8`）
- initramfs 为 Alpine aarch64 minirootfs + `initramfs/init`（自带 busybox，无需交叉编译 busybox）
