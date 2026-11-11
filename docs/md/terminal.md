# 终端使用手册

wcteos 的终端是内核自己实现的命令解释器，所有命令走内核 API，不调用 Linux，也不依赖外部程序。窗口正文是深色背景，提示符 `tty2>` 紧跟最后一行输出，不会钉在窗口底部。

## 一、打开方式

- 桌面双击「终端」图标
- 开始菜单选「终端」
- 任意界面按 `F1` 打开开始菜单
- 运行框输入 `wtr` 回车

## 二、输入与快捷键

| 操作 | 行为 |
| --- | --- |
| 直接打字 | 输入到当前命令行，只接受可见 ASCII |
| `Enter` | 执行并回显 |
| `Backspace` | 删一个字符 |
| `↑` / `PageUp` | 向上滚动 |
| `↓` / `PageDown` | 向下滚动 |
| `Esc` | 关闭终端窗口 |

## 三、文件命令

| 命令 | 作用 | 示例 |
| --- | --- | --- |
| `ls [路径]` | 列目录 | `ls \wct` |
| `cd [目录]` | 切换目录 | `cd \wct\wall` |
| `pwd` | 显示当前目录 | `pwd` |
| `cat` / `type` | 显示文本 | `cat \wct\note.txt` |
| `new` / `touch` | 新建空文件 | `new \wct\a.txt` |
| `write 文件 文本` | 覆盖写入一行 | `write \wct\a.txt hello` |
| `edit 文件` | 图形编辑器打开 | `edit \wct\a.txt` |
| `mkdir` / `md` | 建目录 | `mkdir \wct\test` |
| `rm` / `del` | 删文件 | `del \wct\a.txt` |
| `rd` | 删空目录 | `rd \wct\test` |
| `cp 源 目标` | 复制 | `cp \wct\a.txt \wct\b.txt` |
| `ren 源 新名` / `rename` | 重命名，可只改后缀 | `ren \wct\A.WCT \wct\B.WCT` |
| `attrib 文件` | 名称、字节数、属性位 | `attrib \wct\HELLO.WCT` |
| `run 文件.wct` | 执行模块，支持 WCTX | `run \wct\HELLO.WCT` |
| `script 文件` | 逐行执行 .wet | `script \wct\boot.wet` |

受保护路径：`\EFI` 下的引导文件、`.dwc` 依赖声明、`.glf` 壁纸。删除会被拒，这是设计，不是 bug。

```text
tty2> attrib \wct\HELLO.WCT
  名称：HELLO.WCT
  大小：16440 字节
  属性：文件
```

## 四、网络命令

| 命令 | 作用 |
| --- | --- |
| `ipconfig` | 驱动、IP、网关、dhcp/arp/link |
| `netstat` | 同 ipconfig，并附 rx/tx 计数 |
| `route` | 打印当前网络配置（无路由表） |
| `ping` | 对网关发 4 个 ICMP echo |

```text
tty2> ipconfig
e1000 ip=10.0.2.15 gw=10.0.2.2 dhcp=ok arp=ok link=up
rx=3 tx=3
```

没有 DNS、没有路由表，`ping` 只能打网关；TCG 下的周期数受宿主调度影响，只作功能验证。

## 五、系统命令

| 命令 | 作用 |
| --- | --- |
| `ps` | 内核进程表 |
| `pid` | 当前进程号 |
| `kill PID` | 结束进程，pid 1 受保护 |
| `taskmgr` | 图形任务管理器 |
| `sysinfo` / `about` / `wtcer` | 系统名称、版本、装机时间 |
| `date` / `hw` | 固件日期时间 |
| `mem` | 内存统计 |
| `log` | 启动日志缓冲 |
| `theme` / `color` / `font` | 主题、配色、字号 |
| `gui` / `exitgui` | 进图形 / 回文本 |
| `reboot` / `poweroff` | 重启 / 关机 |
| `clear` | 清屏 |
| `ver` / `native` / `linux` | 版本 / 原生说明 / 兼容层状态 |
| `usb` / `api` / `pkg` / `sudo` | USB 状态 / 模块 API / 包管理占位 / 管理员标记 |
| `help` | 命令总表 |

## 六、脚本与自动执行

`.wet` 相当于批处理，每行一条命令。启动时若存在 `\wct\autorun`，内核会逐行执行，常用于无人值守测试。

```text
ipconfig
attrib \wct\HELLO.WCT
run \wct\HELLO.WCT
```

## 七、限制

- 无管道、重定向、通配符、Tab 补全、命令历史
- 无 DNS、无路由表，`ping` 仅网关
- 没有真正的权限模型，“管理员”只是运行框里的标记
- 不能执行 Linux 程序，也没有 Linux 兼容层
