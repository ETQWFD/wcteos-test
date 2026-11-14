# wcteos Pro 内置 Java 运行时（wctj）

## 是什么

`wctj` 是一个 UEFI 原生的轻量 Java 运行时，编译在内核 `BOOTX64.EFI` 里，终端通过 `java` 命令调用。
它解析真实的 `.class` 文件（`javac 25` 产物，major version 69）并解释执行字节码。

**它不是 OpenJDK**。没有 libc/POSIX，HotSpot 无法在 freestanding UEFI 应用里启动，所以这里是自研解释器。
能力边界：

| 支持 | 不支持 |
|---|---|
| 类/接口/继承、静态与虚方法、invokeinterface | 线程（`Thread` 仅 sleep 占位） |
| 数组、switch、try/catch/finally、异常表 | 反射、Lambda（除字符串拼接 invokedynamic） |
| long 双槽运算、字符串拼接 | 可运行 jar、`-cp`、模块系统 |
| ArrayList/HashMap/Objects/Arrays/String/StringBuilder/包装类 | 网络、AWT/Swing、JDBC |
| `wct.WctRuntime.api()` 调内核 API | 真实浮点运算、GC、JIT |

上限：64 类 / 每类 192 方法 / 常量池 2048 / 对象表 4096 / 堆 4MB / 调用深度 32 帧 / 参数 8 个。

## 安装

包放在 `\wct\pkgs\java-25.wcde`，终端执行：

```
store                      # 列出可用包
store install java-25.wcde # 解包到 \wct\java
```

输出应为 `wcde: installed 11/11 files -> \wct\java` / `module: java 25.1.0`。

## 运行

```
java -version                  # wctj 25.1.0
java Hello                     # 裸名自动在 \wct\java 下查找并补 .class
java MyApp hello world         # 最多 8 个参数传入 main(String[])
java \wct\java\Demo.class a b  # 显式路径
```

实测输出：

```
ttyl wct@wct-os:\> java Hello
WCTEOS Java: sum 1..10 = 55
upper=HELLO WORLD len=11
```

## 自己编译类

```
javac -d out X.java
```

把 `out/X.class` 拷到 `\wct\java\X.class`，然后 `java X`。

## `.wcde` 容器格式

```
WCDE1
name=java
version=25.1.0
command=java
root=\wct\java
files=11
--
Hello.class	1135
Demo.class	4123
--
<按清单顺序拼接的二进制>
```

打包工具：`pack-wcde.py <源目录> <输出.wcde> name=... version=... root=...`

## 图形商店

终端输入 `gstore` 打开图形应用商店：

- 上下键选择，Enter 安装/卸载，Esc 退出
- `.wcde` 包走解包安装，状态显示 `[installed]` / `[available]`
- 安装完成后可直接退出到终端运行 `java <类名>`

注意：`format-disk` 仅支持 ≤512MB 的目标盘，更大的盘会明确报错
`format-disk: disk too large (max 512MB supported)`，不再静默失败。
