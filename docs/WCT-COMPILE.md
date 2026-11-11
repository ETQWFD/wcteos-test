# .wct 可执行文件编译手册（wcteos 测试版）

本手册里的命令全部在 Linux 上实测通过，可以直接复制执行。产物是能被 wcteos 内核
`run` 命令或文件管理器双击执行的 `.wct` 文件。

## 一、为什么是"扁平二进制 + 头"

`.wct` 不是 ELF/PE。内核加载器只做三件事：校验头、按头里的基址分配页、跳到入口偏移。
所以文件 = 24 字节 WCT1 头 + 去掉所有段信息的机器码。这和"裸二进制"是两件事：

| 层 | 内容 | 作用 |
| --- | --- | --- |
| 代码段 | `objcopy -O binary` 得到的机器码 | 真正执行的指令 |
| WCT1 头 | magic/版本/段数/基址/入口偏移/长度/CRC32 | 内核校验、定位入口、拒绝损坏文件 |
| WCTX 容器（可选） | magic `WCTX` + 32 位密钥 + 长度 + XOR 载荷 | 提高反编译门槛，载荷仍是完整 WCT1 镜像 |

所以 `.wct` 有头、有长度、有 CRC，能被加载器验证；直接用 `xxd` 看是密文（WCTX）。
注意：WCTX 是**混淆**不是密码学加密——密钥随文件一起走，只能挡住随手反汇编，挡不住
有决心的逆向。文档里不会把它说成"无法反编译"。

## 二、环境

```bash
sudo apt-get update
sudo apt-get install -y gcc-mingw-w64-x86-64 mtools dosfstools
```

模块必须按 **Windows x64 调用约定**编译（第一个参数在 RCX），所以用 mingw 的 GCC 和
binutils，不能用 Linux 原生 ELF 链接器。

## 三、最小可运行示例

`hello.c`：

```c
typedef unsigned long long u64;
typedef struct {
    void (*puts)(const char *);
    void (*putu)(unsigned long long);
    void (*clear)(void);
    u64 version;
    const char *kernel_tag;
} WCT_API;

int wct_main(WCT_API *api)   /* 入口名固定为 wct_main */
{
    api->puts("[module] hello from a .wct module\r\n");
    api->puts("kernel: ");
    api->puts(api->kernel_tag);
    api->puts("\r\n");
    return 0;                 /* 返回值回显到内核串口 */
}
```

编译、链接、封头、打包，一条条复制执行：

```bash
# 1) 编译成目标文件
x86_64-w64-mingw32-gcc -ffreestanding -fno-stack-protector -fno-builtin \
    -mno-red-zone -O2 -c hello.c -o hello.o

# 2) 链接成 PE，再抽成扁平二进制
x86_64-w64-mingw32-ld -e wct_main --image-base 0x00400000 -o hello.elf hello.o
x86_64-w64-mingw32-objcopy -O binary hello.elf hello.bin

# 3) 算入口偏移：裸二进制从最低 .text 段 VMA 开始，入口偏移必须相对 VMA，而不是相对链接基址
VMA=$(x86_64-w64-mingw32-objdump -h hello.elf | awk '$2==".text"{print $4; exit}')
SYM=$(x86_64-w64-mingw32-nm hello.elf | awk '/ T wct_main$/{print $1}')
OFF=$((0x$SYM - 0x$VMA))

# 4) 封 WCT1 头（脚本在仓库 wct/demo/build-wct.py）
python3 build-wct.py hello.bin $OFF hello.wct 0x00400000

# 5) 可选：打包成 WCTX 混淆容器
./wctpack hello.wct hello.p.wct 0xA5C3E71B
```

`build-wct.py` 头结构（与 `wct/kernel_new/wctmod.c` 完全一致，可自行实现）：

| 偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0 | char[4] | `WCT1` |
| 4 | u8 | 版本 = 1 |
| 5 | u8 | 保留 = 0 |
| 6 | u16 | 段数 = 1 |
| 8 | u32 | 基址（示例 0x00400000） |
| 12 | u32 | 入口偏移（OFF） |
| 16 | u32 | 代码长度 |
| 20 | u32 | 代码 CRC32 |

`wctpack` 的 WCTX 头：`WCTX` + u32 key + u32 len + len 字节载荷（载荷 = 完整 WCT1 镜像，
逐字节 XOR 密钥的四个字节循环）。内核先解包再走 WCT1 校验，CRC 仍然有效。

## 四、跑起来

放到 ESP 的 `\wct\` 目录里，然后：

- 终端：`run \wct\hello.p.wct`
- 图形界面：文件管理器里双击该文件

内核串口会打印：

```
  [wct] loading module at 0x00400000  (fixed base)  entry=+0
  [module] hello from a .wct module
  [wct] module returned 0
```

这段日志是本机实测输出（WCTX 打包版同样通过）。

## 五、必须遵守的约束

1. 入口函数名必须是 `wct_main`，签名 `int wct_main(WCT_API *)`。
2. 只能通过 `WCT_API` 表输出，不能调 UEFI Boot Services、不能碰 Linux 系统调用。
3. 代码必须位置无关（RIP 相对寻址），因为首选基址被占用时加载器会退化到任意页。
4. 不要用全局可写数据依赖绝对地址；需要缓冲区就在栈上开或让内核 API 提供。
5. 浮点、SSE、异常处理都没有；越界写会直接把客户机打挂。

## 六、打包与安装

安装程序本身也是 `.wct`（`wct/installer/install.wct` 的编译方式与上面完全相同），
镜像压缩格式 `.wcte` 的工具在 `wct/wcte/`，用法见 `docs/COMMANDS.md`。

## 七、限制（如实写）

- 没有动态链接、没有堆分配器接口，缓冲区要静态分配
- 不能直接调用 UEFI 协议，只能走内核 API 表
- 没有异常处理，越界写会直接崩在客户机里
- WCTX 只是混淆；没有做真正的对称加密
- 还没有与 WIM 同机对比的数据，因此不宣称 `.wcte` 比 WIM 快
