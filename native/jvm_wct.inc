/*
 * jvm_wct.inc —— jvm.c 的 WCTEOS 内核适配层
 *
 * 提供三件事：
 *   1. 把 JVM 的文本输出接到内核控制台（终端/GUI 终端都能捕获）
 *   2. 用内核的 FAT32 文件接口读 .class
 *   3. 把 wct.WctRuntime.api(...) 转给内核命令分发器（接入 WCT API）
 */
#include "jvm.h"

#define JVMBUF_CAP (32 * 1024)

static void jvmw_print(void *ctx, const char *s)
{
    (void)ctx;
    CHAR16 line[512];
    UINTN n = 0;
    for (const char *p = s; p && *p; ++p) {
        if (*p == '\n') {
            line[n] = 0;
            out(line);
            outln(L"");
            n = 0;
            continue;
        }
        if (*p == '\r') continue;
        if (n + 1 >= sizeof(line) / sizeof(line[0])) {
            line[n] = 0;
            out(line);
            n = 0;
        }
        line[n++] = (CHAR16)(unsigned char)*p;
    }
    if (n) { line[n] = 0; out(line); }
}

static void *jvmw_alloc(void *ctx, int bytes)
{
    (void)ctx;
    VOID *p = NULL;
    if (bytes <= 0) return NULL;
    if (EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, (UINTN)bytes, &p))) return NULL;
    return p;
}

static int jvmw_read(void *ctx, const char *path, unsigned char **buf, int *len)
{
    (void)ctx;
    CHAR16 w[260];
    UINTN n = 0;
    for (UINTN i = 0; i < 258; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c < 32 || c > 126) break;
        w[n++] = (CHAR16)c;
    }
    w[n] = 0;

    CHAR16 resolved[260];
    path_for(resolved, sizeof(resolved) / sizeof(resolved[0]), w);

    UINT8 *data = (UINT8 *)jvmw_alloc(ctx, JVMBUF_CAP);
    if (!data) { outln(L"java: out of memory"); return 0; }

    EFI_FILE_PROTOCOL *root = ROOT;
    CHAR16 rel[256];
    const CHAR16 *use = resolved;
    if (resolve_mount(resolved, &root, rel, sizeof(rel) / sizeof(rel[0]))) use = rel;

    EFI_FILE_PROTOCOL *f = NULL;
    if (EFI_IS_ERROR(root->Open(root, &f, (CHAR16 *)use, EFI_FILE_MODE_READ, 0)) || !f) {
        ST->BootServices->FreePool(data);
        outc(theme()->error, L"java: cannot open ");
        outln(resolved);
        return 0;
    }
    UINTN total = 0;
    for (;;) {
        UINTN chunk = JVMBUF_CAP - 1 - total;
        if (!chunk) break;
        EFI_STATUS rs = f->Read(f, &chunk, data + total);
        if (EFI_IS_ERROR(rs) || chunk == 0) break;
        total += chunk;
    }
    f->Close(f);
    if (!total) {
        ST->BootServices->FreePool(data);
        outc(theme()->error, L"java: empty class file ");
        outln(resolved);
        return 0;
    }
    *buf = data;
    *len = (int)total;
    return 1;
}

static void jvmw_api(void *ctx, const char *method, const char *params)
{
    (void)ctx;
    CHAR16 cmd[520];
    UINTN n = 0;
    const char *pre = "api call ";
    for (int i = 0; pre[i] && n + 1 < 500; ++i) cmd[n++] = (CHAR16)pre[i];
    for (int i = 0; method && method[i] && n + 1 < 500; ++i) cmd[n++] = (CHAR16)(unsigned char)method[i];
    if (params && params[0]) {
        cmd[n++] = L' ';
        for (int i = 0; params[i] && n + 1 < 510; ++i) cmd[n++] = (CHAR16)(unsigned char)params[i];
    }
    cmd[n] = 0;
    process_command(cmd);
}

/* java CLASS [args...] */
static void cmd_java(CHAR16 *args)
{
    CHAR16 *file, *rest;
    split_first(args, &file, &rest);

    if (!file[0] || weq(file, L"-help") || weq(file, L"--help") || weq(file, L"-h")) {
        outln(L"wctj - WCTEOS built-in Java runtime");
        outln(L"usage: java CLASS [args...]      e.g. java Hello  |  java \\wct\\java\\Hello.class");
        outln(L"       java -version              show runtime identity");
        outln(L"notes: bare names are looked up under \\wct\\java. No -cp / -jar / modules.");
        return;
    }

    if (weq(file, L"-version") || weq(file, L"--version") || weq(file, L"-v")) {
        outln(L"wctj 25.1.0  (WCTEOS Java runtime, UEFI native)");
        outln(L"accepts class files produced by javac 25 (major version 69)");
        outln(L"no classpath, no jar, no modules, no threads, no JIT");
        return;
    }

    /* 路径转 ASCII，同时判断是否带目录、是否已带 .class */
    char apath[260];
    UINTN an = 0;
    int has_dir = 0;
    for (UINTN i = 0; file[i] && an < 250; ++i) {
        char c = (char)(unsigned char)file[i];
        if (c < 32 || c > 126) break;
        if (c == '\\' || c == '/') has_dir = 1;
        apath[an++] = c;
    }
    apath[an] = 0;

    int ends_class = an >= 6 &&
        apath[an - 6] == '.' && apath[an - 5] == 'c' && apath[an - 4] == 'l' &&
        apath[an - 3] == 'a' && apath[an - 2] == 's' && apath[an - 1] == 's';

    char full[300];
    UINTN fn = 0;
    if (has_dir) {
        for (UINTN i = 0; i < an; ++i) full[fn++] = apath[i];
    } else {
        const char *pre = "\\wct\\java\\";
        for (int i = 0; pre[i]; ++i) full[fn++] = pre[i];
        for (UINTN i = 0; i < an; ++i) full[fn++] = apath[i];
    }
    if (!ends_class) {
        const char *ext = ".class";
        for (int i = 0; ext[i]; ++i) full[fn++] = ext[i];
    }
    full[fn] = 0;

    /* 参数：最多 8 个，按空格切分 */
    char *argv[8];
    int argc = 0;
    static char argbuf[8][64];
    if (rest[0]) {
        CHAR16 *p = rest;
        while (*p && argc < 8) {
            while (*p == L' ' || *p == L'\t') ++p;
            if (!*p) break;
            UINTN k = 0;
            while (*p && *p != L' ' && *p != L'\t' && k + 1 < 64) {
                argbuf[argc][k++] = (char)(unsigned char)*p++;
            }
            argbuf[argc][k] = 0;
            argv[argc] = argbuf[argc];
            ++argc;
        }
    }

    JvmHost host;
    host.print = jvmw_print;
    host.read_class = jvmw_read;
    host.alloc = jvmw_alloc;
    host.api_call = jvmw_api;

    int rc = jvm_run(&host, NULL, full, argc, argv);
    if (rc != 0) {
        outc(theme()->error, L"java: ");
        const char *t = jvm_last_error_text();
        if (t && t[0]) out_ascii(t);
        else outln(L"failed");
    }
}
