/*
 * jvm.h —— WCTEOS Java 运行时（wctj）公共接口
 *
 * 这个头文件只声明"宿主需要提供什么"，不含任何平台依赖：
 *   - 打印输出（终端 / GUI 控制台 / 串口）
 *   - 读取 .class 文件
 *   - 一块连续内存（堆：对象、数组、字符串都从这来）
 *
 * 内核侧的适配代码在 jvm_wct.inc，Linux 侧的适配代码在 test/host_jvm.c。
 */
#ifndef WCT_JVM_H
#define WCT_JVM_H

typedef unsigned char      ju1;
typedef unsigned short     ju2;
typedef unsigned int       ju4;
typedef unsigned long long ju8;
typedef long long          ji8;

typedef struct {
    /* 输出一行业务文本（已保证以 '\n' 结尾，适配层负责换行/刷新） */
    void (*print)(void *ctx, const char *s);
    /* 读取 class 文件；返回 1 成功。缓冲区必须来自 alloc */
    int  (*read_class)(void *ctx, const char *path, unsigned char **buf, int *len);
    /* 申请一块内存（整个 JVM 生命周期内不单独释放） */
    void *(*alloc)(void *ctx, int bytes);
    /* 可选：调用 WCTEOS 内核 API（wct.WctRuntime.api 用），可空 */
    void (*api_call)(void *ctx, const char *method, const char *params);
} JvmHost;

/* 返回值：0 = 正常结束；负数 = 错误码（错误信息已通过 print 输出） */
int jvm_run(const JvmHost *host, void *ctx, const char *class_path,
            int argc, char **argv);

/* 诊断：最近一次失败的错误码，供内核日志使用 */
int jvm_last_error(void);
const char *jvm_last_error_text(void);

#endif
