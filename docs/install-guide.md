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
