# Linux 持久化根文件系统

## 从"内存 rootfs"到"可写磁盘根"

之前的 Linux 层跑在 initramfs（内存）里，`apk add` 装的东西重启即丢。
现在加了一个**可写根文件系统**：ESP 上放一个预格式化的 ext4 镜像文件
`\wct\rootfs.img`（48MB），内核通过 `loop` 设备挂载它作为真正的根。

```
ESP (FAT32)
  ├─ EFI\BOOT\BOOTX64.EFI          自研 UEFI 内核
  └─ wct\
       ├─ bootx64.efi             Linux bzImage（EFI stub）
       ├─ bootx64.initrd.gz       Alpine initramfs
       └─ rootfs.img              ext4 镜像（可写根，48MB）
                                     ↑ loop0 → 挂载为 /
```

## 启动流程

`/init` 在 initramfs 里做这些事：

1. 挂载 `/proc` `/sys` `/dev`（devtmpfs）
2. `insmod virtio_blk.ko` —— 让内核看见 `/dev/vda`
3. `insmod e1000.ko` + `udhcpc` —— 网卡与 DHCP
4. `insmod nls_iso8859-1.ko` —— vfat 挂载需要的字符集
5. `mount -t vfat /dev/vda1 /esp`
6. `losetup /dev/loop0 /esp/wct/rootfs.img` + `mount -t ext4 /dev/loop0 /mnt`
7. 首次启动（无 `.wct-ready`）时把 initramfs 里的 rootfs `cp -a` 进 ext4
8. `mount --move` 三个伪文件系统，`chroot /mnt /bin/sh`

第 7 步只在第一次执行；之后每次都直接进持久根。

## 为什么用 loop + 镜像文件，而不是单独分区

- **不动分区表**：安装器仍是「单 ESP」布局，`rootfs.img` 就是一个普通文件，
  不存在"分区大小算错把系统挤坏"的风险
- **镜像可整体替换/备份**：拷走一个文件就是完整系统快照
- **FAT32 单文件上限 4GB** 远大于当前 48MB，扩容只需换一个更大的镜像

## 已实测的关键路径

| 步骤 | 实测输出 |
|---|---|
| 内核对磁盘 | `virtio_blk virtio0: [vda] 2097152 512-byte logical blocks (1.07 GB)` |
| vfat 挂载 | `FAT-fs (vda1): mounted filesystem` |
| ext4 挂载 | `EXT4-fs (loop0): mounted filesystem with ordered data mode` |
| 首次 seeding | `persist: first boot - seeding rootfs into ext4 image` → `persist: seeding done` |
| 进入持久根 | `persist: entering persistent root (/dev/vda1)` |
| 装软件 | `apk add nano` → `OK: 8 MiB in 18 packages` |
| 重启后 | `nano --version` 仍输出 `GNU nano, version 7.2`（ext4 已用 8.1M → 11.0M） |

## 安装器改动

`ins_copy_pkgs` 现在在复制商店包的同时，把 Linux 子系统的三件套一起复制到
目标盘的 `\wct\`：

- `bootx64.efi`（11.7MB）
- `bootx64.initrd.gz`（3.4MB）
- `rootfs.img`（48MB）

实测从 ISO 装到全新 1GB 盘后，`\wct` 下三个文件齐全，重启后独立启动可
直接进持久根。

## 边界

- **仍然依赖 virtio-blk / AHCI 的驱动模块**：当前 initramfs 内置 `virtio_blk.ko`；
  IDE/SATA 机器需要再补 `ata_piix.ko`（内核 5.15.0-203-generic 版本必须严格匹配）
- **rootfs.img 大小固定**：48MB 用满后 `apk add` 会报 no space，需要换更大的镜像
- **没有 swap**：内存 446MB，重型软件会 OOM
- **单用户**：没有 PAM、没有多用户登录，`/bin/sh` 直接是 root
