/*
 * wcde.inc —— WCTEOS 模块容器（.wcde）读取/安装
 *
 * 容器格式（ASCII 清单 + 二进制载荷，freestanding 下无需解压库）：
 *
 *   WCDE1\n
 *   name=java\n
 *   version=25.0.4\n
 *   command=java\n
 *   author=WCTEOS\n
 *   desc=Java 25 runtime module\n
 *   root=\wct\java\n
 *   files=6\n
 *   --\n
 *   Hello.class\t1135\n
 *   Demo.class\t4123\n
 *   ...\n
 *   --\n
 *   <各文件内容按清单顺序拼接>
 */

static void wcde_line(const char **pp, char *out, UINTN cap)
{
    const char *p = *pp;
    UINTN n = 0;
    while (*p && *p != '\n' && n + 1 < cap) out[n++] = *p++;
    if (*p == '\n') ++p;
    out[n] = 0;
    *pp = p;
}

static int wcde_mkdirs(CHAR16 *path)
{
    /* 逐级创建，例如 \wct\java\wct */
    CHAR16 tmp[256];
    UINTN n = 0;
    for (UINTN i = 0; path[i] && i + 1 < 256; ++i) tmp[i] = path[i];
    tmp[wlen(path) < 255 ? wlen(path) : 255] = 0;
    (void)n;
    for (UINTN i = 1; tmp[i]; ++i) {
        if (tmp[i] != L'\\') continue;
        CHAR16 save = tmp[i];
        tmp[i] = 0;
        if (tmp[0]) {
            EFI_FILE_PROTOCOL *d = NULL;
            if (open_path(tmp, &d, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, EFI_FILE_DIRECTORY)) d->Close(d);
        }
        tmp[i] = save;
    }
    EFI_FILE_PROTOCOL *d = NULL;
    if (open_path(tmp, &d, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, EFI_FILE_DIRECTORY)) d->Close(d);
    return 1;
}

static int wcde_write_file(const CHAR16 *root, const char *rel, UINT8 *data, UINTN size)
{
    CHAR16 dest[320];
    UINTN n = 0;
    for (UINTN i = 0; root[i] && n + 1 < 300; ++i) dest[n++] = root[i];
    if (n && dest[n - 1] != L'\\') dest[n++] = L'\\';
    for (UINTN i = 0; rel[i] && n + 1 < 318; ++i) dest[n++] = (rel[i] == '/') ? L'\\' : (CHAR16)rel[i];
    dest[n] = 0;

    wcde_mkdirs(dest);

    EFI_FILE_PROTOCOL *f = NULL;
    if (!open_path(dest, &f, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0)) return 0;
    f->Delete(f);                                   /* 覆盖旧文件 */
    if (!open_path(dest, &f, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0)) return 0;
    UINTN remain = size;
    UINT8 *p = data;
    while (remain) {
        UINTN chunk = remain;
        EFI_STATUS st = f->Write(f, &chunk, p);
        if (EFI_IS_ERROR(st) || chunk == 0) break;
        p += chunk;
        remain -= chunk;
    }
    f->Close(f);
    return remain == 0;
}

/* 打印清单（wcde info） */
static int wcde_info_file(const CHAR16 *path)
{
    UINTN size = 0;
    UINT8 *data = NULL;
    VOID *raw = NULL;
    if (EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, 96 * 1024, &raw) || !raw)) { outln(L"wcde: out of memory"); return 0; }
    data = (UINT8 *)raw;
    size = read_whole_file(path, data, 96 * 1024 - 1);
    if (!size) { ST->BootServices->FreePool(raw); outln(L"wcde: cannot read file"); return 0; }
    if (size < 6 || data[0] != 'W' || data[1] != 'C' || data[2] != 'D' || data[3] != 'E' || data[4] != '1') {
        ST->BootServices->FreePool(raw);
        outln(L"wcde: not a WCDE1 container");
        return 0;
    }
    const char *p = (const char *)data;
    char line[256];
    wcde_line(&p, line, sizeof(line));               /* magic */
    for (int i = 0; i < 12; ++i) {
        wcde_line(&p, line, sizeof(line));
        if (!line[0]) break;
        if (!scmp(line, "--")) break;
        char *eq = line;
        while (*eq && *eq != '=') ++eq;
        if (*eq != '=') continue;
        *eq = 0;
        outc(theme()->dim, (CHAR16 *)L"");
        out_ascii(line);
        out(L" : ");
        out_ascii(eq + 1);
        outln(L"");
    }
    ST->BootServices->FreePool(raw);
    return 1;
}

/* 安装（pkg install x.wcde 或 wcde install x.wcde） */
static int wcde_install(const CHAR16 *path)
{
    VOID *raw = NULL;
    if (EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, 96 * 1024, &raw)) || !raw) {
        outln(L"wcde: out of memory");
        return 0;
    }
    UINT8 *data = (UINT8 *)raw;
    UINTN size = read_whole_file(path, data, 96 * 1024 - 1);
    if (!size) { ST->BootServices->FreePool(raw); outln(L"wcde: cannot read file"); return 0; }
    if (size < 6 || data[0] != 'W' || data[1] != 'C' || data[2] != 'D' || data[3] != 'E' || data[4] != '1') {
        ST->BootServices->FreePool(raw);
        outln(L"wcde: not a WCDE1 container");
        return 0;
    }

    const char *p = (const char *)data;
    char line[256];
    wcde_line(&p, line, sizeof(line));               /* magic */

    char root_ascii[200];
    root_ascii[0] = 0;
    char pkg_name[64]; pkg_name[0] = 0;
    char pkg_ver[32]; pkg_ver[0] = 0;
    int nfiles = 0;

    for (;;) {
        wcde_line(&p, line, sizeof(line));
        if (!line[0] || !scmp(line, "--")) break;
        char *eq = line;
        while (*eq && *eq != '=') ++eq;
        if (*eq != '=') continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!scmp((char *)k, "root")) scopy(root_ascii, sizeof(root_ascii), (char *)v);
        else if (!scmp((char *)k, "name")) scopy(pkg_name, sizeof(pkg_name), (char *)v);
        else if (!scmp((char *)k, "version")) scopy(pkg_ver, sizeof(pkg_ver), (char *)v);
        else if (!scmp((char *)k, "files")) nfiles = 0;
    }
    if (!root_ascii[0]) scopy(root_ascii, sizeof(root_ascii), "\\wct\\java");

    CHAR16 wroot[200];
    UINTN rn = 0;
    for (UINTN i = 0; root_ascii[i] && rn + 1 < 198; ++i) wroot[rn++] = (root_ascii[i] == '/') ? L'\\' : (CHAR16)(unsigned char)root_ascii[i];
    wroot[rn] = 0;

    /* 文件表：path<TAB>size */
    struct { const char *rel; UINTN off; UINTN size; } tab[32];
    int n = 0;
    const char *table_start = p;
    for (;;) {
        wcde_line(&p, line, sizeof(line));
        if (!line[0] || !scmp(line, "--")) break;
        if (n >= 32) continue;
        char *tabpos = line;
        while (*tabpos && (*tabpos == ' ' || *tabpos == '\t')) ++tabpos;
        char *rel = tabpos;
        char *sep = rel;
        while (*sep && *sep != '\t' && *sep != ' ') ++sep;
        if (!*sep) continue;
        *sep++ = 0;
        while (*sep == ' ' || *sep == '\t') ++sep;
        UINTN sz = 0;
        while (*sep >= '0' && *sep <= '9') { sz = sz * 10 + (UINTN)(*sep - '0'); ++sep; }
        /* 相对路径需要长期有效：拷到静态区 */
        static char names[32][96];
        UINTN q = 0;
        while (rel[q] && q + 1 < 96) { names[n][q] = rel[q]; ++q; }
        names[n][q] = 0;
        tab[n].rel = names[n];
        tab[n].size = sz;
        tab[n].off = 0;
        ++n;
    }
    (void)table_start;

    /* 载荷顺序累加 */
    const char *payload = p;
    UINTN cursor = 0;
    int ok = 0;
    for (int i = 0; i < n; ++i) {
        if (cursor + tab[i].size > size) break;
        if (wcde_write_file(wroot, tab[i].rel, (UINT8 *)payload + cursor, tab[i].size)) ++ok;
        cursor += tab[i].size;
    }

    ST->BootServices->FreePool(raw);
    outc(theme()->accent, L"wcde: installed ");
    out_int((UINT64)ok);
    out(L"/");
    out_int((UINT64)n);
    out(L" files -> ");
    outln(wroot);
    if (pkg_name[0]) {
        out(L"  module: "); out_ascii(pkg_name);
        if (pkg_ver[0]) { out(L" "); out_ascii(pkg_ver); }
        outln(L"");
    }
    return ok == n && n > 0;
}

static void cmd_wcde(CHAR16 *args)
{
    CHAR16 *sub, *rest;
    split_first(args, &sub, &rest);
    if (!sub[0] || weq(sub, L"help")) {
        outln(L"wcde: WCTEOS module container (.wcde)");
        outln(L"  wcde install FILE   unpack module files to its root dir");
        outln(L"  wcde info FILE      show module manifest");
        outln(L"  wcde list           list installed .wcde modules");
        return;
    }
    if (weq(sub, L"install")) {
        if (!rest[0]) { outln(L"wcde install: missing file"); return; }
        CHAR16 path[256];
        path_for(path, sizeof(path) / sizeof(path[0]), rest);
        wcde_install(path);
        return;
    }
    if (weq(sub, L"info")) {
        if (!rest[0]) { outln(L"wcde info: missing file"); return; }
        CHAR16 path[256];
        path_for(path, sizeof(path) / sizeof(path[0]), rest);
        wcde_info_file(path);
        return;
    }
    if (weq(sub, L"list")) {
        cmd_pkg(L"list");
        return;
    }
    outln(L"wcde: install | info | list");
}
