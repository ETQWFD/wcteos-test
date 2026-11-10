# .wct 可执行文件编译手册

## 一、.wct 是什么

.wct 是 wcteos 的可执行程序格式：一个扁平二进制，入口在文件偏移 0，由内核 `wctmod.c` 加载到内存后直接调用。它不依赖 PE/ELF 头，也不链接任何系统库（没有 libc），需要的内核能力通过 `api` 命令列出的函数表获得。

文件结构（当前实现）：

```
偏移 0     代码入口（第一条指令）
偏移 4     可选：版本号（UINT32，小端）
其余       代码与只读数据，位置无关
```

内核加载流程：读文件 → 分配内存 → 复制 → 取首 4 字节为入口偏移（无版本号时按 0 处理）→ 调用。

## 二、写一个最小 .wct

```c
/* hello.c —— 不依赖任何头文件 */
typedef unsigned short u16;
typedef unsigned int   u32;

/* 内核通过固定地址传入的调用表（wctmod.c 里注册） */
struct wct_api {
    void (*print)(const char *s);
    void (*print_cn)(const u16 *s);
    int  (*file_read)(const char *path, void *buf, int cap);
    int  (*file_write)(const char *path, const void *buf, int len);
};

static struct wct_api *api;

void wct_main(struct wct_api *a)
{
    api = a;
    api->print("hello from hello.wct\n");
}
```

入口函数名必须是 `wct_main`，签名 `void wct_main(struct wct_api *)`。内核按名字找不到符号，所以实际做法是把入口放在文件第一个函数，并在链接时用 `-e wct_main` 固定起始地址。

## 三、编译（Linux / WSL）

```bash
# 目标：32 位扁平二进制，位置无关，无 libc
gcc -m32 -O2 -fno-stack-protector -fno-builtin -nostdlib \
    -fno-asynchronous-unwind-tables -fno-pic \
    -Wl,-e,wct_main -Wl,--oformat=binary \
    -o hello.wct hello.c
```

要点：

- `-nostdlib`：不链 libc，内核不提供
- `-fno-pic` + `--oformat=binary`：产出裸二进制，没有 ELF 头
- `-Wl,-e,wct_main`：把入口固定在第一字节
- 需要 64 位时用 `gcc -m64`，但内核的 .wct 加载器按 32 位调用约定实现，当前只保证 32 位可用

编译完拷到 FAT32 的 `\wct\` 下：

```bash
mcopy -o -i esp.img hello.wct ::/wct/hello.wct
```

系统里运行：

```
C:\wct> run hello.wct
```

桌面里也可以双击 `hello.wct`。

## 四、.wet 一键脚本

纯文本，一行一条终端命令，`#` 开头为注释：

```
# deploy.wet
mkdir \wct\app
cp \wct\hello.wct \wct\app\hello.wct
run \wct\app\hello.wct
```

执行：`script deploy.wet`，或在桌面双击。

## 五、.dwc 依赖声明

等价于 .dll 的系统文件与依赖描述，纯文本键值：

```
name=hello
version=1.0
requires=kernel>=0.9
requires=net
entry=\wct\hello.wct
```

`pkg hello.dwc` 会登记依赖；缺依赖时终端给出提示，不加载程序。当前只做登记与检查，不做自动下载。

## 六、.wcte 压缩格式

WCTE-Compressor 的容器格式，源码在 `WCTE-Compressor/`：

```
magic "WCTE" | 版本 u16 | 条目数 u16 | 每条目: 名字长度 u8 + 名字 + 原始长度 u32 + 压缩长度 u32 + 数据
```

压缩算法是自实现的 RLE + 字典混合，面向文本与代码；打包/解包命令：

```bash
./wcte c  src/  out.wcte
./wcte x  out.wcte  dst/
```

速度：在开发机上对本项目源码（约 600KB 文本）实测打包 0.9s、解包 0.4s。**没有与 WIM 做同机对比**，因此“比 WIM 快两三倍”目前没有数据支撑，不能作为结论；需要的话得在同一台机器上用 wimlib 对同一份数据跑一遍才有可比性。

## 七、限制

- 没有动态链接、没有堆分配器接口，缓冲区要静态分配
- 不能直接调用 UEFI 协议，只能走内核 API 表
- 没有异常处理，越界写会直接崩在客户机里
