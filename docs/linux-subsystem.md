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

**做不到的（当前 initramfs 的天然限制）**

- **没有包管理器**：initramfs 里只有 busybox，没有 `apt`/`apk`/`dnf`，
  因为没有一个持久化的 rootfs。要装发行版软件包，需要另外挂一个
  rootfs 分区（下一阶段的工作）
- **没有网络**：initramfs 里没有网络栈配置，也没有网卡固件/驱动模块
- **没有持久化**：initramfs 是内存文件系统，重启即丢失，改动不会保存
- **退出即重启**：Linux 接管硬件后不返回 wcteos，要回原生系统需重启

这四条是 **initramfs 方案**的边界，不是内核能力的边界。内核本身是完整的
Ubuntu 5.15 通用内核（含绝大多数驱动），只是当前没有给它一个可写的 rootfs。

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
