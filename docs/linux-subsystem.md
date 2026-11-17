# Linux 运行层（Linux Subsystem）

## 这是什么

wcteos Pro 的第一层仍然是自研的 UEFI 原生内核（`BOOTX64.EFI`，249KB）。
在它之上叠加了一个**真实的 Linux 内核运行层**：启动时由原生内核用 UEFI
`LoadImage` 加载一个带 EFI stub 的 Linux `bzImage`，内核接管硬件、退出
BootServices，随后运行 initramfs 里的用户空间。

这不是模拟器、不是兼容层翻译，是真实的 Linux 内核在裸机上跑。

```
UEFI 固件
  └─ BOOTX64.EFI（wcteos 原生内核：shell / 商店 / Java / 分区工具）
       └─ linux 命令
            └─ \wct\bootx64.efi（Linux 5.15.0-203-generic bzImage，带 EFI stub）
                 └─ \wct\bootx64.initrd.gz（busybox initramfs）
                      └─ /bin/sh 终端
```

## 怎么用

在 wcteos 的 shell 里：

```
tty: wct\wct-os:\> linux
```

内核启动约 5–10 秒，屏幕切到 Linux 控制台：

```
wcteos Pro - Linux subsystem (initramfs shell)
kernel: 5.15.0-203-generic
BusyBox v1.30.1 built-in shell (ash)
/ #
```

已经实测可用的命令：

```
/ # uname -a
Linux (none) 5.15.0-203-generic #213-Ubuntu SMP ... x86_64 GNU/Linux
/ # cat /proc/version
Linux version 5.15.0-203-generic ...
/ # free
              total        used        free
Mem:         458608       29368      427072
/ # ls /
bin  dev  etc  init  proc  root  sys  tmp
```

`/proc`、`/sys`、`/dev`（devtmpfs）都已挂载，是完整的 Linux 用户空间入口。

启动前的 F11 菜单可以选择 "Native only" 跳过 Linux 层，此时不加载内核，
纯原生内核启动，速度更快。

## 文件布局

| 路径 | 大小 | 说明 |
|---|---|---|
| `\wct\bootx64.efi` | 11,744,776 | Linux bzImage（带 EFI stub，PE 头合法，`e_lfanew=0x82`） |
| `\wct\bootx64.initrd.gz` | 1,082,877 | busybox initramfs（cpio + gzip） |

原生内核在启动时检测 `\wct\bootx64.efi` 是否存在——存在则启用 `linux` 命令，
不存在则自动降级为纯原生模式（shell 里不会出现 `linux`）。

## 启动参数

`cmd_linux` 通过 `LoadOptions` 传给内核：

```
console=tty0 initrd=\wct\bootx64.initrd.gz
```

- `console=tty0`：输出走 UEFI GOP 帧缓冲，屏幕可见
- `initrd=`：EFI stub 按相对设备路径打开 initramfs

## 能力边界（重要）

**能做的**

- 真实 Linux 内核、真实终端、真实 `/proc` 与 sysfs
- 运行静态链接的 Linux 可执行文件（把 ELF 放进 initramfs 即可）
- 观察真实硬件信息（CPU、内存、设备拓扑）

**已打通的（Alpine rootfs + 网络 + 包管理器）**

initramfs 现在内置 Alpine 3.19 minirootfs（3.3MB gzip），启动后：

- `apk` 包管理器可用：`apk update` 实测返回 `OK: 23043 distinct packages available`
- 网络可用：内置 `e1000.ko`（302KB，从 `linux-modules-5.15.0-203-generic` 提取，
  版本与内核严格匹配），QEMU/真实 Intel 千兆网卡都能驱动；`udhcpc` 自动 DHCP
- 真装软件：`apk add nano` 实测下载安装 ncurses-terminfo-base / libncursesw / nano
  三个包，`OK: 8 MiB in 18 packages`，随后 `nano --version` 输出 `GNU nano, version 7.2`
- 完整工具链入口：`/bin/sh`、`vi`、`wget`、`ssh`、`python3`（`apk add python3` 后）等
  都按标准 Alpine 方式装

**仍然存在的边界**

- **不持久化**：rootfs 在 initramfs（内存）里，重启后 `apk add` 的东西丢失。
  要持久化需要把 rootfs 放到磁盘分区并让内核 `root=/dev/sdaX` 挂载，
  这是下一阶段的分区布局改动
- **退出即重启**：Linux 接管硬件后不返回 wcteos
- **HTTPS 证书校验**：minirootfs 的 CA bundle 在部分环境下会让 `https://` 源校验失败，
  默认已把 `/etc/apk/repositories` 配成 `http://`（Alpine 官方 HTTP 源仍校验包签名，
  安全性由 APK 签名保证）

## 为什么用 EFI stub 而不是自己写跳转

Linux `bzImage` 在 `CONFIG_EFI_STUB=y` 时会带一个 PE 头（本镜像 `e_lfanew=0x82`
处是 `PE\0\0`），UEFI 固件的 `LoadImage` 能直接把它当 EFI 应用加载，内核 stub
自己完成 `ExitBootServices`、页表切换、跳转到 `startup_64`。这样：

- 不需要手写 `boot_params` 填充、保护模式切换、内存布局协商
- 不需要自己处理 ACPI / SMBIOS 传递（stub 会从 UEFI 配置表读取）
- 失败时错误码由 `StartImage` 返回，能在原生 shell 里看到

自己写跳转只在没有 EFI stub 的内核上才必要，本项目不需要。

## 已验证的启动路径

| 场景 | 结果 |
|---|---|
| ISO（CD-ROM）UEFI 启动 → `linux` | ✅ 进 Linux shell（截图实测） |
| 已安装硬盘 UEFI 启动 → `linux` | ✅ 进 Linux shell（截图实测） |
| 无 `\wct\bootx64.efi` 的盘 | ✅ 自动降级为纯原生模式，不报错 |
