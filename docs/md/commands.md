# 命令参考

终端全部命令的完整清单：语法、参数、示例、注意事项。按功能分组，找不到的命令用 `help` 或 `pkg` 查看是否由包提供。

约定：`[方括号]` 表示可选参数，`大写` 表示要替换成实际值。路径用反斜杠，命令不区分大小写。

## 一、帮助与系统

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `help` | `help` | 打印命令表 | `help` |
| `ver` | `ver` | 版本、架构、兼容层 | `ver` |
| `about` | `about` | 作者与版本信息 | `about` |
| `hw` | `hw` | CPU、内存、固件、显卡 | `hw` |
| `sysinfo` | `sysinfo` | 同 `hw` | `sysinfo` |
| `date` | `date` | UEFI 固件时钟 | `date` |
| `mem` | `mem [SIZE\|free]` | 显示内存；`free` 释放测试占用 | `mem free` |
| `api` | `api` | 内核 API 表版本与函数 | `api` |
| `native` | `native` | 是否原生启动 | `native` |
| `tty` | `tty` | 显示 tty1/tty2 | `tty` |

## 二、目录与文件

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `ls` | `ls [PATH]` | 列目录 | `ls \wct` |
| `cd` | `cd [DIR]` | 切目录，无参回根 | `cd \wct\wall` |
| `pwd` | `pwd` | 当前目录 | `pwd` |
| `cat` | `cat FILE` | 显示文本 | `cat \wct\autorun` |
| `type` | `type FILE` | `cat` 别名 | `type \wct\note.txt` |
| `new` | `new FILE` | 建空文件 | `new \wct\a.txt` |
| `create` | `create FILE` | `new` 别名 | `create \wct\a.txt` |
| `touch` | `touch FILE` | `new` 别名 | `touch \wct\a.txt` |
| `write` | `write FILE TEXT` | 覆盖写入一行 | `write \wct\a.txt hello` |
| `edit` | `edit FILE` | 行编辑器，`Ctrl+S` 保存 | `edit \wct\a.txt` |
| `cp` | `cp SRC DST` | 复制文件，覆盖目标 | `cp a.wct b.wct` |
| `ren` | `ren OLD NEW` | 改名，可只改后缀 | `ren a.wct a.cok` |
| `rename` | `rename OLD NEW` | `ren` 别名 | `rename a.txt a.bak` |
| `rm` | `rm FILE` | 删文件 | `rm \wct\a.txt` |
| `del` | `del FILE` | `rm` 别名 | `del \wct\a.txt` |
| `rd` | `rd FILE` | `rm` 别名 | `rd \wct\a.txt` |
| `mkdir` | `mkdir DIR` | 建目录 | `mkdir \wct\backup` |
| `md` | `md DIR` | `mkdir` 别名 | `md \wct\backup` |
| `attrib` | `attrib FILE` | 大小、属性位、类型 | `attrib \wct\hello.wct` |

### 受保护、不能删除的文件

- `\EFI\BOOT\BOOTX64.EFI`、`BOOTIA32.EFI`：引导文件；
- 扩展名 `.dwc`、`.glf`：壁纸定义；
- 位于 `\EFI` 目录下的系统文件。

删除时返回“拒绝”，不是 bug，是保护规则。要改这些文件，请在宿主机上操作镜像，不要在系统内删。

## 三、执行

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `run` | `run FILE` | 运行 `.wct`/`.cok`/`.wxtk` 模块；其他按脚本 | `run \wct\hello.wct` |
| `script` | `script FILE` | 同 `run`，按脚本处理 | `script \wct\autorun` |
| `pkg` | `pkg [list\|install FILE]` | 包管理 | `pkg install \wct\tool.wctpkg` |

脚本规则：逐行执行；空行跳过；`#` 开头为注释；一行一条命令，不支持管道与重定向。

## 四、网络

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `ipconfig` | `ipconfig` | 网卡、IP、网关、DHCP、ARP、计数 | `ipconfig` |
| `netstat` | `netstat` | 同 `ipconfig` | `netstat` |
| `route` | `route` | 同 `ipconfig` 的网络摘要 | `route` |
| `ping` | `ping` | ping 网关 4 次，报最小 TSC 周期 | `ping` |

限制：只支持网关，不支持指定 IP、次数、域名；没有 DNS、没有路由表、没有 HTTP。

## 五、进程

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `ps` | `ps` | 进程列表 | `ps` |
| `pid` | `pid` | 当前 shell 的 PID | `pid` |
| `kill` | `kill PID` | 结束普通进程 | `kill 3` |
| `sudo` | `sudo kill PID` | 结束受保护进程 | `sudo kill 3` |

## 六、外观

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `theme` | `theme [NAME]` | 切主题：`ubuntu` `wct` `matrix` `amber` `ice` | `theme matrix` |
| `color` | `color NAME\|reset` | 终端文字颜色 | `color green` |
| `font` | `font [N\|next]` | 控制台字体/模式 | `font next` |
| `banner` | `banner` | 重画标题 | `banner` |
| `clear` | `clear` | 清屏 | `clear` |

## 七、日志

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `log` | `log [save\|clear\|LEVEL TEXT]` | 查看/保存/清空/追加日志 | `log 3 test` |

## 八、卷与电源

| 命令 | 语法 | 说明 | 示例 |
| --- | --- | --- | --- |
| `usb` | `usb [N\|boot\|rescan]` | 列卷/切卷/重扫 | `usb 1` |
| `poweroff` | `poweroff` | 关机 | `poweroff` |
| `reboot` | `reboot` | 重启 | `reboot` |
| `gui` | `gui` | 回桌面 | `gui` |
| `exitgui` | `exitgui` | 退出桌面到终端 | `exitgui` |
| `linux` | `linux` | 内置 Linux 兼容演示 | `linux` |

## 九、别名对照

| 别名 | 等价命令 |
| --- | --- |
| `type` | `cat` |
| `del`、`rd` | `rm` |
| `md` | `mkdir` |
| `rename` | `ren` |
| `create`、`touch` | `new` |
| `script` | `run` |
| `sysinfo` | `hw` |
| `netstat`、`route` | `ipconfig` |

## 十、不存在的命令

以下命令**没有实现**，输入会得到 `unknown command`：

`ping 1.1.1.1`、`nslookup`、`curl`、`wget`、`ip`、`ifconfig`、`netstat -an`、`tasklist`、`dir /s`、`copy`、`move`、`xcopy`、`format`、`diskpart`、`mount`、`tar`、`zip`、`python`（用 `linux` 进兼容层另说）、`sudo` 以外的提权命令。

原因是内核里没有对应的网络协议栈、压缩库或权限模型。需要的话得先实现，不是改个名字就能用。

## 十一、退出码

终端命令不返回退出码，也没有 `&&` / `||`。失败信息直接打印在下一行，脚本继续执行下一条。
