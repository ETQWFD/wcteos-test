# wcteos Pro 1.0 安装手册

版本：1.0（图形安装器）
构建标识：BUILD=73d0cd19

## 1. 准备工作

- 安装介质：FAT32 分区，内含 `EFI/BOOT/BOOTX64.EFI`（安装器本体）
- 目标磁盘：容量 ≥ 400 MB，安装会**清空整块磁盘**
- 固件：UEFI（x86_64），关闭 Secure Boot

## 2. 图形安装步骤

1. 从安装介质启动，进入系统 shell，输入命令：
   ```
   installer
   ```
2. 进入“wcteos Pro Setup”页面，用 ↑/↓ 选择：
   - 第 1 项：图形安装（推荐）
   - 第 2 项：命令行安装（当前为占位提示，尚未实现）
   按 Enter 进入。
3. 许可协议页：阅读条款，按 Enter 接受，Esc 退出。
4. 选盘页：↑/↓ 切换目标磁盘（页面显示真实容量，如 `Disk 0 512 MB`），Enter 开始安装。
5. 安装过程自动执行：
   - 写入 MBR（分区类型 0x0C，起始 LBA 2048）
   - 写入 FAT32 引导扇区、FSInfo、FAT 双副本、根目录
   - 创建 `EFI`、`EFI/BOOT` 目录
   - 复制安装器本体到 `EFI/BOOT/BOOTX64.EFI`
6. 完成后显示 “Setup - Complete”，移除安装介质并重启，从新盘启动。

## 3. 命令行安装（无需图形界面）

在系统 shell（`tty1 wct0wct-os:>`）中依次执行三条命令，索引从 0 开始：

```
disk-info        # 列出可用磁盘与容量，确认目标盘
format-disk      # 对 0 号盘写入 MBR + FAT32 引导区、FSP、FAT 双副本、根目录
install-files    # 在 0 号盘创建 EFI/BOOT 目录并写入 BOOTX64.EFI
```

成功输出：

```
disk-info
  disks: found
format-disk: OK
install-files: OK
```

说明：
- `format-disk` 会清空 0 号盘，执行前请确认 `disk-info` 列出的就是目标盘。
- `install-files` 从安装介质上的 `EFI/BOOT/BOOTX64.EFI` 读取原始文件并写入目标盘，写入后大小与 md5 应与源文件一致。
- 命令行安装路径当前固定使用 0 号盘；多盘选择请使用图形安装界面。

## 4. 校验方法（安装后）

在 Linux 主机上挂载目标盘分区（分区起始 LBA 2048，即偏移 1048576）：

```
mdir  -i /dev/sdX@@1048576 ::/EFI/BOOT
mcopy -o -i /dev/sdX@@1048576 ::/EFI/BOOT/BOOTX64.EFI ./check.efi
md5sum ./check.efi
```

判据：
- `::/EFI/BOOT/BOOTX64.EFI` 存在，大小与安装器本体一致
- md5 与源文件一致（实测样例：164,929 字节，`c41dbabc863408512a2991153bb34381`）
- 目标盘单独启动进入系统，出现 `tty1 wct0wct-os:>` 提示符

## 5. 已知限制

- 菜单第 2 项“命令行安装”尚未实现，仅有占位提示
- 分区新建/删除未实现，安装固定为“整盘单分区”
- 安装进度按块刷新，非逐字节精确进度

## 6. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 页面显示 `S1=4` 等 | 安装阶段失败，S1 后为阶段号 | 记录阶段号，按阶段排查 |
| 目标盘 `non DOS media` | 格式化未完成 | 重新安装，确认磁盘容量 ≥ 400 MB |
| 回车无反应 | 旧版固件把 Enter 报为 `\n`/ScanCode 0x0D | 使用 BUILD=73d0cd19 及之后版本 |

## 启动方式（重要）

wcteos Pro 是**纯 UEFI** 系统，没有 BIOS/CSM 引导代码。

- 主板必须开启 UEFI 启动，关闭 CSM / Legacy / Legacy Support。
- 启动菜单里选带 **UEFI:** 前缀的那一项（如 `UEFI: Kingston USB`）。
- 如果在 Legacy 模式启动，MBR 会打印提示：
  `wcteos Pro is a UEFI system. Enable UEFI boot (disable CSM/Legacy) and pick the UEFI device.`
  此时回到 BIOS 改启动模式即可，不是介质损坏。

QEMU 下启动已安装镜像：

```
qemu-system-x86_64 -m 512 -smp 1 \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd \
  -drive file=wcteos-Pro-1.0-installed.img,format=raw,if=ide
```

### BIOS / Legacy 模式下会发生什么

镜像的 MBR 和 ISO 的 BIOS 引导入口都内置了一段提示程序：在 Legacy 模式启动时显示

```
wcteos Pro is a UEFI-only system, BIOS boot is not supported.
Enable UEFI boot in BIOS (disable CSM / Legacy), then select
the boot entry prefixed with UEFI: for this device.
Press any key to continue...
```

按任意键后交回 BIOS 继续尝试下一个启动设备，不会死等。
这不是安装介质损坏，是启动模式不对：请在 BIOS 中关闭 CSM/Legacy Support，
并在启动菜单选择带 `UEFI:` 前缀的那一项。

## 固件层面的三条硬限制（不是 bug，是加载机制决定的）

| 限制 | 原因 | 当前处理 |
|---|---|---|
| Legacy BIOS 无法启动 | 内核是 PE32+ 的 EFI 应用，靠固件 `LoadImage/StartImage` 加载；BIOS 的 INT 19h 只认 MBR 前 446 字节，没有 PE 加载器，也不认 ESP。镜像里的 Legacy 分支只能跑实模式代码 | MBR 和 ISO 的 BIOS 入口只打印"需要 UEFI"提示并交回 BIOS，**不假装能在 Legacy 下安装或启动** |
| Secure Boot 会拒绝未签名镜像 | `BOOTX64.EFI` 没有 Microsoft/厂商签名，开启 Secure Boot 的固件会直接拒绝加载 | 使用前在固件里关闭 Secure Boot；或自行用 `sbctl`/`KeyTool` 把自己的密钥 enroll 进 db 并签名（本项目未提供签名流程） |
| 32 位 UEFI 固件无法加载 | 镜像的 PE Machine 是 `0x8664`（x86-64），32 位固件的 `LoadImage` 只接受 `0x014C` | 仅支持 x64 UEFI。老 Atom 平板等 32 位 UEFI 设备不支持 |

### 分区表约定

安装盘使用 MBR，单分区：
- type = `0xEF`（EFI System Partition，UEFI 规范要求；**不是** `0x0C`，部分固件不把 `0x0C` 当作 ESP 扫描）
- boot flag = `0x80`（active）
- 起始 LBA 2048，分区内含 `\EFI\BOOT\BOOTX64.EFI`（UEFI 标准可移动介质路径，不依赖 NVRAM 启动项）

`BOOTX64.EFI` 头部自检：
```
MZ = 4D5A, PE = 50450000, Machine = 0x8664, Subsystem = 10 (EFI Application)
```

## 分区管理（part 命令）

安装器支持 MBR 分区表的新建与删除，命令行下：

```
part list                  # 列出 4 个分区槽（type / start LBA / sectors）
part create 256            # 新建一个 256MB 的 ESP（type 0xEF，起始 LBA 自动对齐到上一分区末尾 + 2048）
part delete 1              # 删除 1 号分区
```

实测输出：

```
part: MBR partition table
  #1  type=0xEF  start=2048  sectors=1046528
  [empty]
  [empty]
  [empty]
```

## 构建水印 BUILD.TXT

安装完成后，目标盘根目录会写入 16 字节的构建标识：

```
$ mtype -i disk.img@@1048576 ::/BUILD.TXT
BUILD=73d0cd19
```

用于核对「这块盘是哪次构建装的」，避免旧盘冒充新盘。

## 复制进度

`install-files` 的进度按真实字节推进（`copied / FileSize`），不再是固定估算值。

## ISO 的双引导能力边界

| 介质形态 | Legacy BIOS | UEFI |
|---|---|---|
| ISO 刻成光盘 / QEMU `-drive media=cdrom` | ❌ El Torito BIOS 条目被固件拒绝读取（`Could not read from CDROM`），受 xorriso 生成限制 | ✅ 正常 |
| ISO `dd` 到 U 盘 / 硬盘 | ✅ 执行 MBR 里的提示代码 | ✅ 正常 |
| 已安装的硬盘 | ✅ 执行 MBR 里的提示代码 | ✅ 正常 |

也就是说：**用 U 盘安装请 dd 整个 ISO，不要用刻录软件做成「光盘」**。dd 后的 U 盘在 BIOS 下会给出 UEFI 提示，不会静默失败。
