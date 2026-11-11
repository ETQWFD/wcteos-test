# 终端使用手册

wcteos 的终端是内核自己实现的命令解释器，所有命令走内核 API，不调用 Linux，也不依赖外部程序。窗口正文是深色背景，提示符紧跟最后一行输出，不会钉在窗口底部——和 Linux 终端一样，输入行永远在内容下方。

## 一、打开终端

三种入口：

1. 桌面双击「终端」图标；
2. 任务栏点终端按钮；
3. 桌面运行框输入 `wtr` 回车。

提示符格式为 `tty2>` 或当前目录，例如 `\wct>`。

## 二、键盘操作

| 按键 | 作用 |
| --- | --- |
| 字符键 | 插入到输入行光标处，只接受可见 ASCII |
| `Enter` | 执行当前行并回显 |
| `Backspace` | 删除光标前一个字符 |
| `↑` / `PageUp` | 向上滚动历史输出 |
| `↓` / `PageDown` | 向下滚动 |
| `Esc` | 关闭终端窗口 |
| `Ctrl+L` 或 `clear` | 清屏并重画标题 |

不支持 `Tab` 补全，不支持 `Ctrl+C` 中断。长命令请一次输完。

## 三、路径规则

- 根目录是 `\`，代表启动分区（ESP）根。
- 分隔符是反斜杠。多数命令里 `/` 也能识别，但建议统一用 `\`。
- 相对路径从“当前目录”算：`cd \wct` 之后，`ls` 看的是 `\wct`。
- 卷用 `usb` 命令切换：`usb 1` 进第二个卷，`usb boot` 回启动卷。
- FAT32 限制：单文件最大 4GB，文件名最长 255 字符，不支持 `:` `*` `?` 等字符。

## 四、文件与目录

### 看目录

```bash
ls
ls \wct
ls \wct\wall
```

输出每个条目的名字、大小、目录标记。空目录打印 `(empty)`。

### 进目录 / 看当前路径

```bash
cd \wct
cd ..
pwd
```

`cd` 不带参数回到根目录 `\`；`cd ..` 回上级，已在根目录时停在根目录。

### 看文本文件

```bash
cat \wct\autorun
type \wct\autorun
```

`type` 是 `cat` 的别名。超过一屏的内容连续输出，用滚动键回看。

### 新建文件

```bash
new \wct\note.txt
touch \wct\note.txt
create \wct\note.txt
```

三个名字是同一条命令。文件已存在时不会清空内容。

### 写文件

```bash
write \wct\note.txt hello world
```

整行覆盖写入，不支持追加。要改内容用 `edit`。

### 编辑文件

```bash
edit \wct\note.txt
```

进入行编辑模式：

| 按键 | 作用 |
| --- | --- |
| 直接输入 | 追加到当前行 |
| `Enter` | 提交当前行，开始下一行 |
| `Ctrl+S` | 保存并退出 |
| `Esc` | 放弃修改退出 |

文件不存在时会先创建。保存时按当前编码写回。

### 复制

```bash
cp \wct\hello.wct \wct\hello2.wct
cp hello.wct \wct\backup\hello.wct
```

目标已存在会被覆盖。不支持递归复制目录。

### 改名

```bash
ren \wct\HI.WCT \wct\HI2.WCT
rename \wct\a.txt \wct\a.bak
ren \wct\hello.wct \wct\hello.cok
```

可以只改后缀。实现方式是“读源文件 → 写目标 → 删源”，不是原子重命名；空间不足时保留源文件并报错。

### 删除

```bash
rm \wct\hello.wct
del \wct\hello.wct
rd \wct\hello.wct
```

三个名字是同一条命令。受保护文件会拒绝删除并打印原因。

### 建目录

```bash
mkdir \wct\backup
md \wct\backup
```

### 看属性

```bash
attrib \wct\hello.wct
```

输出示例：

```
file: \wct\hello.wct
  size: 16428 bytes
  attributes: 0x20
  type: .wct executable
```

属性位含义：

| 位 | 含义 |
| --- | --- |
| `0x01` | 只读 |
| `0x02` | 隐藏 |
| `0x04` | 系统 |
| `0x10` | 目录 |
| `0x20` | 归档（普通文件） |

图形界面里选中文件点底部「属性」按钮，看到的是同一组信息；「复制」「粘贴」按钮用于同目录或跨目录复制文件。

## 五、运行程序

### 运行模块

```bash
run \wct\hello.wct
run hello.wct
run \wct\hello.cok
run \wct\hello.wxtk
```

`.wct` / `.cok` / `.wxtk` 都按 WCT1 容器加载，校验通过才执行；失败会打印具体原因（magic、CRC、分配地址失败等）。

### 运行脚本

```bash
run \wct\autorun
script \wct\autorun
```

不带可执行后缀的文件按文本脚本处理：逐行读取，跳过空行和 `#` 注释，依次执行。

### 直接双击

文件管理器里双击 `.wct` / `.cok` / `.wxtk` 等同于 `run`；双击 `.txt` 打开编辑器；双击 `.dwc` 设为壁纸。

## 六、网络

### 看状态

```bash
ipconfig
netstat
route
```

三个命令输出同一份网络状态，包含网卡型号、链路状态、IP、网关、DHCP 是否成功、ARP 是否解析到网关、收发包计数。

正常输出：

```
nic: e1000
ip=10.0.2.15 gw=10.0.2.2 dhcp=ok arp=ok rx:3 tx:3
```

`arp=no` 说明网关 MAC 没拿到，此时 `ping` 一定失败。

### ping 网关

```bash
ping
```

只支持 ping 网关，四次 ICMP，打印每次往返的 TSC 周期与最小值。没有 DNS、没有路由表，不能 ping 外网地址。

失败提示：

- `ping: 网络未就绪或没有网关`：网卡没起来或 DHCP 没拿到地址；
- `ping: 4 次请求无应答`：ARP 没通，检查网关。

### 已知限制

- e1000 在 QEMU slirp 下实测可用；
- rtl8139 能识别、能初始化，但模拟网络下 DHCP/ARP 不稳定，不保证可用；
- 没有无线驱动，扫不到 WiFi；
- 没有 DNS 解析，没有 HTTP 客户端。

## 七、系统信息

```bash
ver
about
hw
sysinfo
date
mem
mem free
```

- `ver`：内核版本、架构、兼容层信息；
- `about`：作者与版本；
- `hw` / `sysinfo`：CPU 厂商、型号、内存大小、固件信息、显卡模式；
- `date`：UEFI 固件时钟（不是网络时间）；
- `mem`：显示内存占用；`mem free` 释放测试占用。

## 八、进程

```bash
ps
pid
kill 3
sudo kill 3
```

- `ps`：列出进程号、名字、状态、父进程；
- `pid`：显示当前 shell 的进程号；
- `kill PID`：结束普通进程；
- `sudo kill PID`：结束受保护进程（内核进程、init 等），需要确认。

受保护进程普通 `kill` 会返回拒绝，必须用 `sudo`。

## 九、外观

```bash
theme
theme matrix
theme amber
theme ice
theme ubuntu
theme wct
color green
color reset
font next
font 2
banner
```

- `theme` 不带参数列出 5 套配色：`ubuntu` `wct` `matrix` `amber` `ice`；
- `color` 改终端文字颜色，`reset` 恢复默认；
- `font` 切换控制台字体/模式；
- `banner` 重画顶部标题。

图形界面对应入口是「设置」→ 主题 / 壁纸。

## 十、日志

```bash
log
log save
log clear
log 3 something happened
```

`log` 查看启动与命令日志；`save` 写回磁盘；`clear` 清空；`log LEVEL TEXT` 手动追加一条，LEVEL 是 0-3 的数字。

## 十一、电源与模式

```bash
poweroff
reboot
gui
exitgui
tty
linux
native
```

- `poweroff`：关机；
- `reboot`：重启；
- `gui`：从终端切回桌面；
- `exitgui`：从桌面退回终端；
- `tty`：显示 tty1/tty2 两个终端实例；
- `linux`：进入内置 Linux 兼容子系统（CPython 3.11.2 演示环境，不是完整 Linux）；
- `native`：显示当前是否以原生模式启动。

## 十二、包与 API

```bash
pkg
pkg list
pkg install \wct\tool.wctpkg
api
```

- `pkg` 列出已安装的包命令；
- `pkg install` 安装 `.wctpkg` 包（本质是带命令元数据的模块）；
- `api` 打印内核 API 表版本与可用函数。

## 十三、卷与 U 盘

```bash
usb
usb 0
usb 1
usb boot
usb rescan
```

- `usb` 列出所有 FAT 卷；
- `usb N` 切到第 N 个卷；
- `usb boot` 回启动卷；
- `usb rescan` 重新扫描。

切换后 `pwd` 显示新卷路径，`ls` 看到该卷根目录。

## 十四、常见错误

| 提示 | 原因 | 处理 |
| --- | --- | --- |
| `unknown command: xxx` | 命令拼错，或没装对应包 | `help` 看命令表；`pkg` 看已装包 |
| `not found` | 路径不对 | `ls` 确认目录；注意反斜杠 |
| `access denied` | 文件只读或受保护 | 引导文件、`.dwc`、`.glf` 不允许删除 |
| `no space` | FAT32 剩余空间不足 | 删无用文件，或换更大镜像 |
| `ping: 网络未就绪` | 网卡没起来 / 没拿到 IP | 先 `ipconfig` 看 `dhcp=ok` |
| `run: usage` | 没给文件名 | `run \wct\xxx.wct` |
| `bad magic` | 不是 WCT1 文件 | 重新编译打包，见编译手册 |

## 十五、和 Windows / Linux 终端的差别

- 像 Windows：路径用 `\`，`del` `rd` `md` `ren` `type` 这些别名直接可用，盘符用 `usb` 切换。
- 像 Linux：提示符跟着输出走，`ls` `cat` `cp` `rm` `mkdir` `pwd` 这些名字保留，输入行在内容下方而不是固定在底栏。
- 不一样：没有管道、没有重定向、没有通配符、没有环境变量、没有后台任务、没有 `Tab` 补全；参数按空格切分，含空格的路径暂不支持。
