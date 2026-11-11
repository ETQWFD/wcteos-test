# 内核运行报告

只写实测到的东西，并给出复现命令。没有 KVM 的纯软件模拟环境下，任何“比别的系统快”的说法都不成立，所以这里不下这个结论。

## 一、测试环境

| 项目 | 值 |
| --- | --- |
| 虚拟化 | QEMU 6.2，`-accel tcg`，无 /dev/kvm |
| 内存 / CPU | 384MB，单核 |
| 固件 | OVMF 4MB（CODE + 每次全新 VARS） |
| 磁盘 | 48MB FAT32 ESP，IDE |
| 网卡 | e1000（另有 rtl8139 驱动） |
| 显示 | GOP 1024×768 32 位 |
| 镜像 | wcteos-0.9-test.iso / 同一份 BOOTX64.EFI |

## 二、复现命令

```bash
cp /usr/share/OVMF/OVMF_VARS_4M.fd vars.fd
qemu-system-x86_64 -m 384 -smp 1 -display none -no-reboot -accel tcg \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=vars.fd \
  -blockdev driver=file,filename=esp.img,node-name=e,locking=off \
  -blockdev driver=raw,file=e,node-name=ed \
  -device ide-hd,drive=ed,bus=ide.0,unit=0 \
  -netdev user,id=n0 -device e1000,netdev=n0 \
  -serial file:serial.log -monitor none
```

等 40 秒左右读 `serial.log`；截图用 monitor 的 `screendump`，路径必须是绝对路径。

## 三、启动各阶段

| 阶段 | 证据 | 结果 |
| --- | --- | --- |
| 固件加载 BOOTX64.EFI | 内核横幅 | 通过 |
| GOP 初始化 | 1024×768 纸片桌面 | 通过 |
| PS/2 鼠标 | `mouse: PS/2 i8042 ready (fallback)` | 通过 |
| e1000 + DHCP | `net: e1000 52:54:00:12:34:56 ip=10.0.2.15 link:up arp:ok rx:3 tx:3` | 通过 |
| rtl8139 | `link:up arp:no rx:2 tx:5` | 能识别，DHCP/ARP 不稳，未宣称可用 |
| HDA 音频 | `audio: HDA codecs=0 verb:no-response` | 无 codec 应答，未通过 |
| .wct（WCT1） | `[wct] loading module ... returned 0` | 通过 |
| .wct（WCTX） | 先解包再校验 CRC，返回 0 | 通过 |
| 文件属性 / 删除 | `attrib` 显示 16440 字节，系统文件拒删 | 通过 |
| 网络命令 | `ipconfig` 完整输出；`ping` 4/4 | 通过 |

## 四、rdtsc 原始计数

```text
copy=3,958,994 cyc/MB   fill=648,771 cyc/MB
copy=10,495,550 cyc/MB  fill=7,267,399 cyc/MB   （同一镜像另一次）
```

两次差 3 倍以上，原因是 TCG 逐条翻译指令，宿主调度与缓存都会影响计数。因此不能换算 MB/s、不能反推主频、不能证明“更快”。

## 五、能确认与不能确认

| 能确认 | 不能确认 |
| --- | --- |
| OVMF 下能进桌面 | 真机启动耗时 |
| 鼠标键盘 GOP 可用 | 与 Windows/Linux 的速度对比 |
| e1000 DHCP 与 ARP 可用 | rtl8139 在真实网络的稳定性 |
| .wct / WCTX 能加载执行 | 音频输出可用 |
| 文件读写、属性、删除保护可用 | .wcte 与 WIM 压缩速度对比 |

要做速度对比，必须在同一台真机、同一块盘、同一份测试程序上跑两组镜像，取多次采样中位数。当前环境不具备条件。

## 六、已知问题

- ISO El Torito 在部分沙箱 OVMF 里被跳过，会落到内置 shell；U 盘镜像可直接启动
- HDA 音频无 codec 应答
- 无线网卡没有驱动
- 安装程序尚未实现“写硬盘 + 注册 UEFI 启动项 + 自动重启”
- 没有 KVM，性能结论不成立
