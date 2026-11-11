# .wct 可执行文件编译手册

从一台什么都没装的电脑开始，到做出一个能在 wcteos 里双击运行的模块。所有命令都可以直接复制，每一步都给出“这条命令在干什么”“失败会是什么样”“怎么改”。

适用对象：会一点 C、没编译过 UEFI 模块的人。不需要懂 PE/ELF 格式，也不需要装 Visual Studio。

---

## 一、先认识三种可执行文件

wcteos 里的可执行模块都装在 WCT1 容器里，文件名后缀不同，用途不同，加载方式完全一样：

| 后缀 | 用途 | 能不能双击运行 | 能不能 `run` | 说明 |
| --- | --- | --- | --- | --- |
| `.wct` | 普通模块 | 是 | 是 | 最常用。工具、小游戏、命令扩展都打成这个 |
| `.cok` | 系统组件（component kit） | 是 | 是 | 与 `.wct` 同一容器；按“组件”归类，便于系统工具识别 |
| `.wxtk` | 带 WCTX 外壳的模块 | 是 | 是 | 外层是 `WCTX` 头 + XOR 载荷，内核先解包再按 WCT1 校验 |
| `.wet` | 文本脚本 | 是（交给脚本解释器） | 是 | 纯文本，一行一条命令，不经过 WCT1 |
| `.dwc` | 桌面壁纸定义 | 是（作为壁纸） | 否 | 纯文本 |

判断依据不是后缀本身，而是文件头 4 个字节：`WCT1` 或 `WCTX`。后缀只是给人和文件管理器看的分类。

**关于“属性”**：在文件管理器里选中文件 → 底部「属性」按钮，或在终端 `attrib 文件名`，会显示大小、类型、只读/隐藏/系统属性位。`.cok` 与 `.wxtk` 的属性位与 `.wct` 相同，不同的只是分类标签。复制用 `cp 源 目标`，或在文件管理器里选中后按「复制 / 粘贴」。

**先说清楚一件事**：`.wct` 不是裸二进制，也不是加密文件。它是 24 字节头 + 扁平机器码，头里带 CRC32。WCTX 只是把整段 WCT1 用固定密钥 XOR 了一遍，密钥就写在文件头里，任何人拿到文件都能解出来。它能挡住“随手改一个字节”“用文本编辑器打开看”，挡不住有决心的人。这一点在第十二节会再讲一次。

---

## 二、准备编译环境

需要三样东西：mingw-w64 的 GCC（必须，因为模块用 Windows x64 调用约定）、binutils（ld / objcopy / objdump / nm）、Python 3。

### 2.1 Windows：用 MSYS2

1. 打开 `https://www.msys2.org`，下载 `msys2-x86_64-latest.exe`，一路默认安装到 `C:\msys64`。
2. 安装完成后，从开始菜单找到并打开 **MSYS2 UCRT64**。不要用 cmd，也不要用 PowerShell，这两个环境里没有 pacman。
3. 在打开的黑色窗口里依次执行（第一次会装很久，正常）：

```bash
pacman -Syu
```

窗口可能会提示关闭后重新打开，照做。然后：

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils python
```

4. 验证三件套是否可用：

```bash
x86_64-w64-mingw32-gcc --version
x86_64-w64-mingw32-objcopy --version
python --version
```

三条都打印出版本号就算装好。任何一条提示 `command not found`，说明上一步没装完，重跑一次 `pacman -S --needed ...`。

### 2.2 Linux（Ubuntu / Debian）

```bash
sudo apt-get update
sudo apt-get install -y gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 python3
```

验证：

```bash
x86_64-w64-mingw32-gcc --version
python3 --version
```

### 2.3 建工作目录

Windows（MSYS2 里）：

```bash
mkdir -p ~/wct-dev && cd ~/wct-dev
```

Linux：

```bash
mkdir -p ~/wct-dev && cd ~/wct-dev
```

后面所有命令都在这个目录里执行。

---

## 三、写第一个模块

新建 `hello.c`，内容如下，整段复制：

```c
/* hello.c —— 最小 WCT 模块：只通过内核 API 输出，不链接任何库 */
typedef struct {
    void (*puts)(const char *s);        /* 输出一行 ASCII，自动换行 */
    void (*putu)(unsigned long long v); /* 输出十进制无符号数 */
    void (*clear)(void);                /* 清屏 */
    int  version;                       /* API 版本 */
    const char *kernel_tag;             /* 内核标识字符串 */
} WCT_API;

int wct_main(WCT_API *api)
{
    api->puts("  hello from a .wct module");
    api->puts("  kernel says: ");
    api->puts(api->kernel_tag);
    api->puts("  api version=");
    api->putu((unsigned long long)api->version);
    api->puts("");
    return 0;
}
```

要点：

- 入口函数必须叫 `wct_main`，参数是一个 `WCT_API *`，返回值 `int`（0 表示成功）。
- 不要 `#include` 任何头文件，不要调用 `printf`、`malloc`、`memcpy`。模块里没有 C 运行库。
- 字符串用 `char *`（ASCII）。中文要显示，必须在 C 里写 UTF-16 并调用内核的宽字符接口，当前 API 表没有开放，所以模块默认只输出英文。
- `api->puts` 收到的是窄字符，内核会逐字节转成宽字符再画，所以只支持 ASCII。

---

## 四、分四步编译

### 第一步：编译成目标文件

```bash
x86_64-w64-mingw32-gcc -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -O2 -c hello.c -o hello.o
```

每个参数的意思：

| 参数 | 作用 | 去掉会怎样 |
| --- | --- | --- |
| `-ffreestanding` | 告诉编译器“没有标准库、程序入口不是 main” | 可能生成对 `memcpy`/`memset` 的调用，链接时报未定义 |
| `-fno-stack-protector` | 关掉栈保护 | 生成 `__stack_chk_fail` 引用，链接失败 |
| `-fno-builtin` | 不把循环优化成库函数调用 | 大循环可能变成 `memset` 调用，链接失败 |
| `-mno-red-zone` | 禁用 128 字节红区 | 中断发生时红区被踩，表现为随机死机 |
| `-O2` | 开优化 | 体积变大，速度变慢 |
| `-c` | 只编译不链接 | 得到的是可执行文件，不是 .o |

成功时没有任何输出，目录里多出 `hello.o`。

### 第二步：链接成 ELF

```bash
x86_64-w64-mingw32-ld -e wct_main --image-base 0x00400000 -o hello.elf hello.o
```

- `-e wct_main`：指定入口符号。
- `--image-base 0x00400000`：模块固定加载到 4MB 处。内核按这个地址 `AllocatePages(AllocateAddress)`，地址被别的固件占用时会加载失败。
- 这里用 `ld` 而不是 `gcc` 链接，避免把 mingw 的启动代码和库带进来。

成功时目录里出现 `hello.elf`。

### 第三步：抽出裸机器码

```bash
x86_64-w64-mingw32-objcopy -O binary hello.elf hello.bin
```

`objcopy` 输出的裸二进制**从最低的“有内容的段”的 VMA 开始**，不是从 `0x00400000` 开始。这一步很容易错，第四步会处理。

### 第四步：算入口偏移，封 WCT1 头

先取两个数：

```bash
VMA=$(x86_64-w64-mingw32-objdump -h hello.elf | awk '$2==".text"{print $4; exit}')
SYM=$(x86_64-w64-mingw32-nm hello.elf | awk '/ T wct_main$/{print $1}')
echo "VMA=0x$VMA  SYM=0x$SYM"
```

然后算偏移并打包：

```bash
OFF=$((0x$SYM - 0x$VMA))
python3 build-wct.py hello.bin $OFF hello.wct 0x00400000
```

`build-wct.py` 见第六节，先把它保存到 `~/wct-dev/build-wct.py`。

成功时输出类似：

```
wct: hello.wct  code=312 base=0x00400000 entry=+0x0
```

到这里 `hello.wct` 就做完了。

**为什么偏移不是相对 0x00400000**：`objcopy` 的裸二进制把最低 VMA 当成文件第 0 字节，而 `ld` 的入口符号是按链接地址记的。如果直接把 `SYM - 0x400000` 填进入口，多数情况下会得到 0 或负数，内核跳到代码中段，表现为 `#UD`（非法指令）或直接重启。正确做法就是上面这条：`SYM - VMA`。

---

## 五、WCT1 文件头字段

头固定 24 字节，小端序：

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 4 | magic | 固定 `WCT1`（0x57 0x43 0x54 0x31） |
| 4 | 1 | version | 固定 1 |
| 5 | 1 | flags | 保留，填 0 |
| 6 | 2 | segments | 段数量。当前实现只支持 1 段（整块代码），填 1 |
| 8 | 4 | base | 链接基址，通常 `0x00400000` |
| 12 | 4 | entry | 入口偏移，相对代码起点（不是相对 base） |
| 16 | 4 | length | 代码字节数 |
| 20 | 4 | crc32 | 代码段的 CRC32（IEEE 多项式，与 zlib.crc32 一致） |
| 24 | — | code | 扁平机器码 |

加载时内核按顺序做四件事：

1. 读文件，长度必须 ≥ 24；
2. 校验 magic 是 `WCT1`、version 是 1、segments 是 1；
3. 校验 `length` 与实际字节数一致、CRC32 匹配；
4. `AllocatePages(AllocateAddress, base)` 固定地址，拷贝代码，清指令缓存，调用 `base + entry`。

任何一步失败都会打印原因并拒绝执行，不会“带病运行”。

---

## 六、build-wct.py 全文

保存为 `build-wct.py`，和 `hello.c` 放同一目录：

```python
#!/usr/bin/env python3
"""把裸二进制封装成 .wct（WCT1 容器）。用法：
   python3 build-wct.py <code.bin> <entry_offset> <out.wct> [base]
"""
import struct, sys, zlib

def main():
    if len(sys.argv) not in (4, 5):
        print("用法: build-wct.py <code.bin> <entry_offset> <out.wct> [base]")
        return 2
    code = open(sys.argv[1], "rb").read()
    entry_off = int(sys.argv[2], 0)
    base = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x00400000
    hdr = bytearray(24)
    hdr[0:4] = b"WCT1"
    hdr[4] = 1                  # version
    hdr[5] = 0                  # flags
    struct.pack_into("<H", hdr, 6, 1)        # 段数量：1
    struct.pack_into("<I", hdr, 8, base)     # 链接基址
    struct.pack_into("<I", hdr, 12, entry_off)
    struct.pack_into("<I", hdr, 16, len(code))
    struct.pack_into("<I", hdr, 20, zlib.crc32(code) & 0xFFFFFFFF)
    with open(sys.argv[3], "wb") as f:
        f.write(hdr)
        f.write(code)
    print("wct: %s  code=%d base=0x%08X entry=+0x%X" % (sys.argv[3], len(code), base, entry_off))
    return 0

sys.exit(main())
```

三个必填参数：代码文件、入口偏移（可以是 `0x10` 这种十六进制，也可以是 `16`）、输出文件名。第四个参数是基址，省略时用 `0x00400000`。

---

## 七、打包成 .wxtk（WCTX 外壳）

`.wxtk` = `WCTX` 头 + XOR 后的 WCT1 数据。头 12 字节：

| 偏移 | 长度 | 内容 |
| --- | --- | --- |
| 0 | 4 | `WCTX` |
| 4 | 4 | key（u32，小端） |
| 8 | 4 | 载荷长度（u32，小端） |
| 12 | — | XOR 后的完整 WCT1 文件 |

打包命令（`wctpack` 在源码 `wct/wcte/wctpack.c`，可自己编译）：

```bash
x86_64-w64-mingw32-gcc -O2 wctpack.c -o wctpack.exe
./wctpack.exe hello.wct hello.wxtk 0xA5C3E71B
```

Linux 下把 `wctpack.exe` 换成 `wctpack`。

实测：`hello.wct` 16,428 字节 → 打包后 16,440 字节，多出的 12 字节就是 WCTX 头。

内核加载 `.wxtk` 时先读头、按 key 逐字节 XOR 还原，再走 WCT1 的四步校验；校验失败一样拒绝执行。所以“改了后缀”不会绕过 CRC。

**它是混淆，不是加密**：key 明文写在文件里，XOR 是可逆的，任何人拿脚本都能还原。它只防止“用十六进制编辑器瞎改”和“直接文本打开看到字符串”。第十二节展开。

---

## 八、把模块放进系统

### 8.1 放进 U 盘镜像

假设镜像文件是 `wcteos-0.9-test-usb.img`：

```bash
mcopy -o -i wcteos-0.9-test-usb.img hello.wct ::/wct/hello.wct
mcopy -o -i wcteos-0.9-test-usb.img hello.wxtk ::/wct/hello.wxtk
```

查看是否写进去：

```bash
mdir -i wcteos-0.9-test-usb.img ::/wct
```

### 8.2 放进 ISO

ISO 是只读镜像，要改就得重打。把 ISO 里的 `efi.img` 取出来，改完 FAT32 再按原参数重打：

```bash
xorriso -osirrox on -indev wcteos-0.9-test.iso -extract /efi.img efi.img
mcopy -o -i efi.img hello.wct ::/wct/hello.wct
cd root && xorriso -as mkisofs -o ../new.iso -V WCTEOS \
  -eltorito-platform efi -eltorito-boot efi.img -no-emul-boot \
  -boot-load-size 4 -append_partition 2 0xef efi.img \
  -partition_offset 16 -isohybrid-gpt-basdat .
```

注意 `-eltorito-boot efi.img` 必须和 `-append_partition` 的文件是同一个，否则固件找不到启动镜像。

### 8.3 虚拟机里直接换文件

QEMU 用 `-blockdev` 挂 FAT32 镜像时，宿主机可以直接 `mcopy` 写入，重启虚拟机即可看到新文件：

```bash
mcopy -o -i work/esp.img hello.wct ::/wct/hello.wct
```

---

## 九、在系统里运行

三种方式，任选：

1. **双击**：文件管理器里双击 `hello.wct` / `hello.cok` / `hello.wxtk`，内核按扩展名识别为可执行，直接调用加载器。
2. **终端 `run`**：`run \wct\hello.wct`，相对路径也可以 `run hello.wct`。
3. **运行框**：桌面按运行快捷键（`wtr`），输入文件名回车。

运行成功时终端会打印：

```
loading module at 0x00400000 (fixed base) entry=+0
hello from a .wct module
kernel says: ...
module returned 0
```

`entry=+0` 就是头部第 12 字节的值。`module returned 0` 是 `wct_main` 的返回值。

---

## 十、错误对照表

| 现象 | 原因 | 解决办法 |
| --- | --- | --- |
| 链接报 `undefined reference to memcpy` | 编译器生成了库函数调用 | 加 `-ffreestanding -fno-builtin`，或自己写循环 |
| 链接报 `undefined reference to __stack_chk_fail` | 栈保护没关 | 加 `-fno-stack-protector` |
| 链接报 `undefined reference to wct_main` | 入口函数名写错 | 函数名必须是 `wct_main`，签名 `int wct_main(WCT_API*)` |
| 加载打印 `bad magic` | 文件被改过，或打包时写错头 | 重新用 `build-wct.py` 打包，不要手改 |
| 加载打印 `crc mismatch` | 代码段在打包后被修改 | 重新打包；WCTX 也要重打 |
| 加载打印 `alloc failed` | `0x00400000` 被固件或其他模块占用 | 换一个基址（如 `0x00600000`），打包时同步改 base |
| 运行后立刻重启 / 黑屏 | 入口偏移错，跳到代码中段 | 用 `SYM - VMA` 重算，别用 `SYM - 0x400000` |
| 打印 `entry=+0x...` 但没有任何输出 | API 指针用错，或模块里调了不存在的函数 | 只通过 `api->` 调用；不要把内核符号当地址用 |
| 终端 `run` 报 `not found` | 路径不对 | 先 `ls \wct` 确认；相对路径从当前目录算 |
| 双击 `.cok` 没反应 | 扩展名拼错（如 `.cok1`） | 内核判定不区分大小写，但文件名要真以 `.cok` 结尾 |
| `.wxtk` 报 `bad container` | 外层不是 WCTX，或长度字段不对 | 用 `wctpack` 重打，别手工拼头 |
| 运行后中文变方块 | 模块只走窄字符 API | 模块输出用 ASCII；中文界面由内核绘制 |

---

## 十一、实测记录

以下是在测试机上跑通的记录，可作为对照：

- `hello.c` 编译产物 `hello.wct` 16,428 字节，`entry=+0`；
- `wctpack` 打包后 `hello.wct` → 16,440 字节；
- QEMU（OVMF，无 KVM）里 `run \wct\hello.wct` 输出：

```
loading module at 0x00400000 (fixed base) entry=+0
  [module] hello from a .wct module
  [module] kernel says: wcteos-native-1.0
  [module] api=1
  [module] computed sum 1..1000000 = 500000500000
module returned 0
```

- 同一份文件改名为 `.cok` 后双击、`run` 均正常；
- 改名为 `.wxtk`（内容是 WCTX）后双击、`run` 均正常；
- 故意改掉 `hello.wct` 中间一个字节，加载器打印 CRC 不匹配并拒绝执行，符合预期。

---

## 十二、会不会被别人盗取代码

直接说结论：

- `.wct` 里是机器码，不是源码。别人拿到文件，能反汇编，能看到字符串，能改字节，但拿不到你的 C 源码。
- WCTX 的 XOR 只是让文件“不像明文”，密钥就在头里，任何人写十行脚本就能解开。它不是加密，不提供任何密码学保护。
- 这版没有代码签名、没有许可证校验、没有硬件绑定、没有联网激活。也就是说，任何人拿到 `.wct` 都能直接运行、直接改、直接再分发。
- 真正的保护只有两条：一是许可证（MIT 允许再分发，但必须保留版权声明）；二是别把源码和私钥一起发出去。

如果你要更强的保护，需要另外做：签名校验（内核里内置公钥）、模块与机器绑定、授权文件有效期。这三样当前都没有实现，手册不把它们写成“已有能力”。

---

## 十三、常见问题

**问：一定要用 mingw 吗，Linux 原生 gcc 行不行？**
不行。模块按 Windows x64 调用约定传参（第一个参数在 RCX），Linux 原生 gcc 用另一套寄存器约定，链接出来的代码一进模块就会读错参数。交叉编译器 `x86_64-w64-mingw32-gcc` 生成的是 Windows ABI，这是必须的。

**问：能不能写 C++？**
当前不行。没有异常、没有 RTTI、没有 `new`，入口也必须按 C 链接名导出。要用 C++ 得自己关掉这些特性，并且保证导出名是 `wct_main`。

**问：模块能不能申请内存？**
不能。API 表里没有分配函数。需要缓冲区就在栈上开固定大小数组。

**问：模块能不能读写文件？**
当前 API 没有开放文件接口。文件操作请用终端命令或文件管理器。

**问：`.cok` 和 `.wct` 到底差在哪？**
容器完全一样，差在分类标签和系统工具的展示归类。你可以把同一个文件复制成两份、分别改名，两个都能跑。

**问：改后缀能绕过 CRC 吗？**
不能。内核先看文件头，再看 CRC，后缀只影响“双击时按哪种类型处理”。
