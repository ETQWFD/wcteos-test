# .wct 可执行文件编译手册

写给第一次编译 wcteos 模块的人。按顺序做完，你会得到一个能双击运行、也能在终端 `run` 的 `hello.wct`。每条命令都可直接复制。

> **先弄清 .wct 是什么**：它不是裸二进制。文件由 24 字节 WCT1 头和扁平机器码组成，头里记录 magic、基址、入口偏移、代码长度和 CRC32，内核加载前会校验。可选的 WCTX 容器会在外面再套一层 XOR 混淆。

## 一、准备环境

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10/11（推荐）或 Ubuntu/Debian |
| 编译器 | mingw-w64 x86_64 GCC + binutils |
| Python | 3.8 以上，用于封 WCT1 头 |
| 运行环境 | wcteos 测试版 ISO，或写好镜像的 U 盘 |

### Windows：MSYS2 安装步骤

1. 打开 `https://www.msys2.org` 下载安装包，默认装到 `C:\msys64`。
2. 从开始菜单启动 **MSYS2 UCRT64**，不要用 cmd 或 PowerShell。
3. 依次执行：

```bash
pacman -Syu
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils python
```

4. 验证：

```bash
x86_64-w64-mingw32-gcc --version
python --version
```

### Linux

```bash
sudo apt-get update
sudo apt-get install -y gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 python3
```

出现 `command not found` 就说明没装成功，先重装，不要继续往下做。

## 二、写 hello.c

在 `D:\wct-build`（Linux 下 `~/wct-build`）新建 `hello.c`：

```c
typedef unsigned long long u64;

typedef struct {
    void (*puts)(const char *s);
    void (*putu)(u64 v);
    void (*clear)(void);
    u64 version;
    const char *kernel_tag;
} WCT_API;

int wct_main(WCT_API *api)
{
    api->puts("hello from my first .wct module\r\n");
    api->puts("kernel tag: ");
    api->puts(api->kernel_tag);
    api->puts("\r\n");
    return 0;
}
```

入口名必须是 `wct_main`，第一个参数是 `WCT_API *`，返回 `int`。API 表就这 5 个字段，顺序不能改。

## 三、编译

```bash
x86_64-w64-mingw32-gcc -ffreestanding -fno-stack-protector -fno-builtin \
    -mno-red-zone -O2 -c hello.c -o hello.o
```

| 参数 | 作用 |
| --- | --- |
| `-ffreestanding` | 声明没有标准库，不生成 printf/memcpy 调用 |
| `-fno-stack-protector` | 内核没有栈保护桩函数，开着会链接失败 |
| `-fno-builtin` | 禁止把循环优化成 memset |
| `-mno-red-zone` | UEFI 中断会踩坏红区 |
| `-O2` | 优化级别，也可 `-Os` |

**为什么必须用 mingw**：模块跑在 Windows x64 调用约定下，第一个参数放 RCX。Linux GCC 用 System V 约定把参数放 RDI，调进去拿到错误指针，直接崩。

## 四、链接并抽二进制

```bash
x86_64-w64-mingw32-ld -e wct_main --image-base 0x00400000 -o hello.elf hello.o
x86_64-w64-mingw32-objcopy -O binary hello.elf hello.bin
```

内核不解析 PE/ELF，只认连续机器码。objcopy 从最低有内容段开始抽，入口偏移必须以段 VMA 为基准。

## 五、算入口偏移

```bash
VMA=$(x86_64-w64-mingw32-objdump -h hello.elf | awk '$2==".text"{print $4; exit}')
SYM=$(x86_64-w64-mingw32-nm hello.elf | awk '/ T wct_main$/{print $1}')
OFF=$((0x$SYM - 0x$VMA))
echo "VMA=$VMA SYM=$SYM OFF=$OFF"
```

正常结果是 `OFF=0`。算错会跳进代码中段，表现为 `#UD`、重启或死机。

## 六、封 WCT1 头

新建 `build-wct.py`：

```python
import struct, sys, zlib

if len(sys.argv) not in (4, 5):
    print("usage: build-wct.py code.bin entry_offset out.wct [base]")
    sys.exit(2)

code = open(sys.argv[1], "rb").read()
entry_off = int(sys.argv[2], 0)
base = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x00400000

hdr = bytearray(24)
hdr[0:4] = b"WCT1"
hdr[4] = 1
hdr[5] = 0
struct.pack_into("<H", hdr, 6, 1)
struct.pack_into("<I", hdr, 8, base)
struct.pack_into("<I", hdr, 12, entry_off)
struct.pack_into("<I", hdr, 16, len(code))
struct.pack_into("<I", hdr, 20, zlib.crc32(code) & 0xFFFFFFFF)
open(sys.argv[3], "wb").write(hdr + code)
```

执行：

```bash
python build-wct.py hello.bin $OFF hello.wct 0x00400000
```

头字段：

| 偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0 | char[4] | `WCT1` |
| 4 | u8 | 版本 1 |
| 5 | u8 | 保留 0 |
| 6 | u16 | 段数 1 |
| 8 | u32 | 首选基址 |
| 12 | u32 | 入口偏移 |
| 16 | u32 | 代码长度 |
| 20 | u32 | CRC32 |

## 七、打包 WCTX

```bash
./wctpack hello.wct hello.p.wct 0xA5C3E71B
```

加载顺序是：看到 `WCTX` → 取密钥 → 解 XOR → 得到完整 WCT1 → 校验 CRC。

> WCTX 是混淆，不是加密。密钥写在文件里，拿到文件的人可以解出来。

## 八、放进镜像并运行

```bash
mcopy -o -i esp.img hello.p.wct ::/wct/HELLO.WCT
```

FAT 短名建议用 8.3 形式，长名在部分工具下会变成 `HELLO~1.WCT` 导致找不到。真机把文件复制到 U 盘 EFI 分区的 `\wct\` 即可。

运行：终端 `run \wct\HELLO.WCT`，或文件管理器双击。成功时串口输出：

```text
[wct] loading module at 0x00400000 (fixed base) entry=+0
hello from my first .wct module
kernel tag: WCT-Kernel
[wct] module returned 0
```

## 九、会不会被别人盗取代码

这是最常问的问题，分开说：

- **别人拿到 .wct 能做什么**：能直接反汇编出机器码，看到字符串。WCTX 只是让 `xxd` 第一眼是乱码，密钥就在文件头后面，写个小脚本就能还原，所以**不能防住有决心的人**。
- **.wct 里有什么可被抄的**：你的算法与逻辑（机器码）。符号表在 objcopy 时已经丢了，但字符串、常量、控制流还在。
- **这版没有做什么**：没有签名校验、没有服务端授权、没有硬件绑定、没有真正的对称/非对称加密。
- **能做的加固**（按成本从低到高）：
  1. WCTX 混淆 + 去掉调试字符串；
  2. 关键逻辑放服务端，模块只做 UI；
  3. 加载前校验签名（需要内核内置公钥，这版未实现）；
  4. 按机器码/固件 UUID 绑定授权。
- **法律层面**：源码与二进制都建议附 LICENSE；商业分发前确认第三方代码许可。

一句话：**这版能提高门槛，不能承诺“别人无法反编译”**。任何说纯本地文件无法被逆向的说法都不成立。

## 十、常见错误

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| undefined reference | 用了 libc 或没关栈保护 | 只用 API 表，检查三个编译开关 |
| 加载即 #UD/重启 | 入口偏移错 | 按第五节重算 |
| checksum mismatch | 文件被截断 | 重新 objcopy 与封头 |
| run: not found | 路径错或短名别名 | `ls \wct` 看真实名字 |
| 输出乱码 | 非 ASCII 字符串 | 模块输出保持英文 |

## 十一、限制

- 没有堆分配器，缓冲区用静态数组或栈。
- 没有浮点、SSE、异常处理，越界写会打挂客户机。
- 不能调 UEFI Boot Services，也不能用 Linux 系统调用。
- 代码必须位置无关，首选基址被占用时会重定位。
