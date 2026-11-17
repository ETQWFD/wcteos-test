/* reg.inc —— 注册表 / 记忆列表（registry）
 *
 * 用法与通用注册表命令基本一致：
 *   reg enum                                列出根键与二级键
 *   reg query <key>                         列出该键下的值与子键
 *   reg query <key> /v <name>               查询单个值
 *   reg add <key> /v <name> [/t SZ|DWORD] /d <data>
 *   reg delete <key> /v <name>              删除单个值
 *   reg delete <key>                        删除整个键
 *   reg save                                强制写回磁盘
 *
 * 持久化到 \wct\registry.dat（文本：key<TAB>name<TAB>type<TAB>data 每行一条）。
 * 从只读介质（光驱）启动时改动留在内存，并明确提示不会落盘。
 */

#define REG_MAX    192
#define REG_KLEN    64
#define REG_NLEN    40
#define REG_TLEN     8
#define REG_DLEN   160

typedef struct {
    int  used;
    char key[REG_KLEN];
    char name[REG_NLEN];
    char type[REG_TLEN];
    char data[REG_DLEN];
} RegEntry;

static RegEntry g_reg[REG_MAX];
static int g_reg_n = 0;
static int g_reg_ready = 0;
static int g_reg_dirty = 0;

static char reg_cp(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int reg_ci_eq(const char *a, const char *b)
{
    while (*a && *b) { if (reg_cp(*a) != reg_cp(*b)) return 0; ++a; ++b; }
    return *a == 0 && *b == 0;
}

/* a 的前 n 个字符与 b 大小写无关相等，且 a 更长 */
static int reg_ci_prefix(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; ++i) if (reg_cp(a[i]) != reg_cp(b[i])) return 0;
    return a[n] != 0;
}

static char *reg_trim(char *s)
{
    while (*s == ' ' || *s == '\t') ++s;
    int n = (int)slen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

static void reg_shrink(char *dst, int cap, const char *src)
{
    int i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

static int reg_find(const char *key, const char *name)
{
    for (int i = 0; i < REG_MAX; ++i)
        if (g_reg[i].used && reg_ci_eq(g_reg[i].key, key) && reg_ci_eq(g_reg[i].name, name)) return i;
    return -1;
}

static int reg_key_exists(const char *key)
{
    for (int i = 0; i < REG_MAX; ++i)
        if (g_reg[i].used && reg_ci_eq(g_reg[i].key, key)) return 1;
    return 0;
}

static int reg_count_values(const char *key)
{
    int n = 0;
    for (int i = 0; i < REG_MAX; ++i) if (g_reg[i].used && reg_ci_eq(g_reg[i].key, key)) ++n;
    return n;
}

static int reg_free_slot(void)
{
    for (int i = 0; i < REG_MAX; ++i) if (!g_reg[i].used) return i;
    return -1;
}

static void reg_store(int slot, const char *key, const char *name, const char *type, const char *data)
{
    RegEntry *e = &g_reg[slot];
    e->used = 1;
    reg_shrink(e->key, REG_KLEN, key);
    reg_shrink(e->name, REG_NLEN, name);
    reg_shrink(e->type, REG_TLEN, type);
    reg_shrink(e->data, REG_DLEN, data);
}

static int reg_put(const char *key, const char *name, const char *type, const char *data)
{
    if (!key[0] || !name[0]) return 0;
    int i = reg_find(key, name);
    if (i < 0) {
        i = reg_free_slot();
        if (i < 0) return 0;
        g_reg_n++;
    }
    reg_store(i, key, name, type, data);
    g_reg_dirty = 1;
    return 1;
}

static void reg_seed_defaults(void)
{
    reg_put("HKLM\\SYSTEM", "ProductName", "SZ", "wcteos Pro");
    reg_put("HKLM\\SYSTEM", "Build", "SZ", BUILD_HASH);
    reg_put("HKLM\\SYSTEM", "CurrentVersion", "SZ", "1.0");
    reg_put("HKLM\\SYSTEM", "Architecture", "SZ", "x86_64 UEFI");
    reg_put("HKLM\\SYSTEM", "FileSystem", "SZ", "FAT32");
    reg_put("HKLM\\HARDWARE\\DESCRIPTION", "System", "SZ", "UEFI Firmware");
    reg_put("HKLM\\HARDWARE\\DESCRIPTION\\BIOS", "Vendor", "SZ", "EDK II");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Java", "Version", "SZ", "25.1.0");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Java", "Root", "SZ", "\\wct\\java");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Java", "Engine", "SZ", "wctj interpreter");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Linux", "Kernel", "SZ", "5.15.0-203-generic");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Linux", "Rootfs", "SZ", "\\wct\\rootfs.img");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Linux", "Distro", "SZ", "Alpine 3.19");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Installer", "PartitionType", "DWORD", "0xEF");
    reg_put("HKLM\\SOFTWARE\\WctEOS\\Installer", "EspStartLba", "DWORD", "2048");
    reg_put("HKCU\\Environment", "TEMP", "SZ", "\\temp");
    reg_put("HKCU\\Console", "FontSize", "DWORD", "80x25");
    g_reg_dirty = 0;
}

/* 解析一行：以 TAB 分成 4 段 */
static int reg_parse_line(char *line, char **f1, char **f2, char **f3, char **f4)
{
    char *p = line;
    char *segs[4] = { 0, 0, 0, 0 };
    int idx = 0;
    segs[idx++] = p;
    while (*p && idx < 4) {
        if (*p == '\t') { *p = 0; segs[idx++] = p + 1; }
        ++p;
    }
    if (idx < 4) return 0;
    *f1 = segs[0]; *f2 = segs[1]; *f3 = segs[2]; *f4 = segs[3];
    return 1;
}

static void reg_load(void)
{
    if (g_reg_ready) return;
    g_reg_ready = 1;
    for (int i = 0; i < REG_MAX; ++i) g_reg[i].used = 0;
    g_reg_n = 0;

    EFI_FILE_PROTOCOL *f = NULL;
    if (!ROOT || EFI_IS_ERROR(ROOT->Open(ROOT, &f, L"\\wct\\registry.dat", EFI_FILE_MODE_READ, 0)) || !f) {
        reg_seed_defaults();
        return;
    }

    static char raw[8192];
    UINTN total = 0;
    for (;;) {
        UINTN got = sizeof(raw) - 1 - total;
        if (got == 0) break;
        EFI_STATUS st = f->Read(f, &got, raw + total);
        if (EFI_IS_ERROR(st) || got == 0) break;
        total += got;
    }
    f->Close(f);
    if (total >= sizeof(raw)) total = sizeof(raw) - 1;
    raw[total] = 0;

    UINTN pos = 0;
    int added = 0;
    while (pos < total) {
        UINTN start = pos;
        while (pos < total && raw[pos] != '\n') ++pos;
        UINTN len = pos - start;
        ++pos;
        if (len == 0 || len > 480) continue;
        char line[490];
        for (UINTN i = 0; i < len; ++i) line[i] = raw[start + i];
        line[len] = 0;
        if (len && line[len - 1] == '\r') line[len - 1] = 0;
        char *f1, *f2, *f3, *f4;
        if (!reg_parse_line(line, &f1, &f2, &f3, &f4)) continue;
        int slot = reg_free_slot();
        if (slot < 0) break;
        reg_store(slot, f1, f2, f3, f4);
        g_reg_n++;
        added++;
    }
    if (added == 0) reg_seed_defaults();
}

static int reg_save(void)
{
    reg_load();
    if (!ROOT) return 0;
    UINT64 mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
    /* 先删掉旧文件：EFI_FILE 没有 SetSize，短写入会留下尾部残数据 */
    EFI_FILE_PROTOCOL *old = NULL;
    if (!EFI_IS_ERROR(ROOT->Open(ROOT, &old, L"\\wct\\registry.dat", EFI_FILE_MODE_READ, 0)) && old)
        old->Delete(old);            /* Delete 同时关闭句柄 */

    EFI_FILE_PROTOCOL *rf = NULL;
    if (EFI_IS_ERROR(ROOT->Open(ROOT, &rf, L"\\wct\\registry.dat", mode, 0x20)) || !rf) return 0;

    static char buf[REG_KLEN + REG_NLEN + REG_TLEN + REG_DLEN + 8];
    for (int i = 0; i < REG_MAX; ++i) {
        if (!g_reg[i].used) continue;
        int n = 0;
        for (int k = 0; g_reg[i].key[k] && n < (int)sizeof(buf) - 2; ++k) buf[n++] = g_reg[i].key[k];
        buf[n++] = '\t';
        for (int k = 0; g_reg[i].name[k] && n < (int)sizeof(buf) - 2; ++k) buf[n++] = g_reg[i].name[k];
        buf[n++] = '\t';
        for (int k = 0; g_reg[i].type[k] && n < (int)sizeof(buf) - 2; ++k) buf[n++] = g_reg[i].type[k];
        buf[n++] = '\t';
        for (int k = 0; g_reg[i].data[k] && n < (int)sizeof(buf) - 2; ++k) buf[n++] = g_reg[i].data[k];
        buf[n++] = '\n';
        UINTN w = (UINTN)n;
        if (EFI_IS_ERROR(rf->Write(rf, &w, buf)) || w != (UINTN)n) { rf->Close(rf); return 0; }
    }
    rf->Flush(rf);
    rf->Close(rf);
    g_reg_dirty = 0;
    return 1;
}

static void reg_auto_save(void)
{
    if (!g_reg_dirty) return;
    if (reg_save()) {
        outc(theme()->accent, L"reg: saved \\wct\\registry.dat\r\n");
    } else {
        outc(theme()->error, L"reg: volume is read-only, change kept in memory only\r\n");
    }
}

static void reg_print_value(const RegEntry *e)
{
    out_ascii("    ");
    out_ascii(e->name);
    int w = 4 + (int)slen(e->name);
    while (w < 26) { out_ascii(" "); ++w; }
    out_ascii("REG_");
    out_ascii(e->type);
    w += 4 + (int)slen(e->type);
    while (w < 38) { out_ascii(" "); ++w; }
    out_ascii("    ");
    out_ascii(e->data);
    out_ascii("\r\n");
}

static void reg_print_children(const char *key)
{
    int kn = (int)slen(key);
    out_ascii("\r\nSubkeys:\r\n");
    int shown = 0;
    static char listed[32][REG_KLEN * 2];
    int nl = 0;
    for (int i = 0; i < REG_MAX; ++i) {
        if (!g_reg[i].used) continue;
        const char *k = g_reg[i].key;
        if (!reg_ci_prefix(k, key, kn)) continue;
        if (k[kn] != '\\') continue;
        char child[REG_KLEN];
        int n = 0;
        for (int j = kn + 1; k[j] && k[j] != '\\' && n + 1 < (int)sizeof(child); ++j) child[n++] = k[j];
        child[n] = 0;
        if (!n) continue;
        int dup = 0;
        for (int s = 0; s < nl; ++s) if (reg_ci_eq(listed[s], child)) { dup = 1; break; }
        if (dup) continue;
        if (nl < 32) reg_shrink(listed[nl++], REG_KLEN, child);
        out_ascii("    ");
        out_ascii(key);
        out_ascii("\\");
        out_ascii(child);
        out_ascii("\r\n");
        shown++;
    }
    if (!shown) out_ascii("    (none)\r\n");
}

/* 取下一个 token；返回 NULL 表示结束。*rest 会前移。 */
static char *reg_next(char **rest)
{
    char *p = *rest;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) { *rest = p; return 0; }
    char *t = p;
    while (*p && *p != ' ' && *p != '\t') ++p;
    if (*p) *p++ = 0;
    *rest = p;
    return t;
}

/* 去掉首尾双引号 */
static char *reg_unquote(char *s)
{
    if (!s) return s;
    int n = (int)slen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') { s[n - 1] = 0; return s + 1; }
    return s;
}

static void reg_usage(void)
{
    outln(L"reg enum | reg query <key> [/v name] | reg add <key> /v name [/t SZ|DWORD] /d data | reg delete <key> [/v name] | reg save");
}

static void reg_cmd_query(char *rest)
{
    char *key = reg_next(&rest);
    if (!key) { outc(theme()->error, L"reg: missing key\r\n"); return; }
    char *name = 0;
    while (rest && *rest) {
        char *sw = reg_next(&rest);
        if (!sw) break;
        if ((sw[0] == '/' || sw[0] == '\\') && (sw[1] == 'v' || sw[1] == 'V')) { char *v = reg_next(&rest); if (v) name = reg_unquote(v); }
    }
    if (!name) key = reg_trim(key);

    if (name) {
        int i = reg_find(key, name);
        if (i < 0) { outc(theme()->error, L"reg: value not found\r\n"); return; }
        out_ascii("\r\n"); out_ascii(key); out_ascii("\r\n");
        reg_print_value(&g_reg[i]);
        return;
    }
    if (!reg_key_exists(key)) { outc(theme()->error, L"reg: key not found\r\n"); return; }
    out_ascii("\r\n"); out_ascii(key); out_ascii("\r\n");
    for (int i = 0; i < REG_MAX; ++i)
        if (g_reg[i].used && reg_ci_eq(g_reg[i].key, key)) reg_print_value(&g_reg[i]);
    if (reg_count_values(key) == 0) out_ascii("    (no values)\r\n");
    reg_print_children(key);
}

static void reg_cmd_add(char *rest)
{
    char *key = reg_next(&rest);
    if (!key) { outc(theme()->error, L"reg: missing key\r\n"); return; }
    char *name = 0, *type = 0, *data = 0;
    while (rest && *rest) {
        char *sw = reg_next(&rest);
        if (!sw) break;
        if ((sw[0] == '/' || sw[0] == '\\') && sw[1]) {
            char f = sw[1];
            if (f == 'v' || f == 'V') { char *v = reg_next(&rest); if (v) name = reg_unquote(v); continue; }
            if (f == 't' || f == 'T') { char *v = reg_next(&rest); if (v) type = reg_unquote(v); continue; }
            if (f == 'd' || f == 'D') {
                /* /d 之后整段都是值，允许空格 */
                while (*rest == ' ' || *rest == '\t') ++rest;
                data = rest;
                rest = rest + slen(rest);
                break;
            }
        }
        /* 非开关 token 直接跳过（容错） */
    }
    if (!name) { outc(theme()->error, L"reg: /v <name> required\r\n"); return; }
    if (!type) type = (char *)"SZ";
    if (!data) data = (char *)"";
    name = reg_unquote(name);
    data = reg_trim(reg_unquote(data));

    if (data && slen(data) >= REG_DLEN) { outc(theme()->error, L"reg: value too long\r\n"); return; }
    if (!reg_put(key, name, type, data)) {
        outc(theme()->error, slen(data) >= REG_DLEN ? L"reg: value too long\r\n" : L"reg: table full\r\n");
        return;
    }
    out_ascii("The operation completed successfully.\r\n");
    reg_auto_save();
}

static void reg_cmd_delete(char *rest)
{
    char *key = reg_next(&rest);
    if (!key) { outc(theme()->error, L"reg: missing key\r\n"); return; }
    char *name = 0;
    while (rest && *rest) {
        char *sw = reg_next(&rest);
        if (!sw) break;
        if ((sw[0] == '/' || sw[0] == '\\') && (sw[1] == 'v' || sw[1] == 'V')) { char *v = reg_next(&rest); if (v) name = reg_unquote(v); }
    }
    int removed = 0;
    if (name) {
        int i = reg_find(key, name);
        if (i < 0) { outc(theme()->error, L"reg: value not found\r\n"); return; }
        g_reg[i].used = 0; g_reg_n--; removed = 1;
    } else {
        if (!reg_key_exists(key)) { outc(theme()->error, L"reg: key not found\r\n"); return; }
        for (int i = 0; i < REG_MAX; ++i)
            if (g_reg[i].used && reg_ci_eq(g_reg[i].key, key)) { g_reg[i].used = 0; g_reg_n--; removed = 1; }
    }
    if (removed) { g_reg_dirty = 1; out_ascii("The operation completed successfully.\r\n"); reg_auto_save(); }
}

static void reg_cmd_enum(void)
{
    reg_load();
    out_ascii("\r\nRegistration database: \\wct\\registry.dat\r\n");
    out_ascii("Entries: ");
    out_int((UINT64)g_reg_n);
    out_ascii(" of ");
    out_int((UINT64)REG_MAX);
    out_ascii("\r\n\r\nRoot keys:\r\n");
    out_ascii("    HKLM   HKEY_LOCAL_MACHINE   system + installed subsystems\r\n");
    out_ascii("    HKCU   HKEY_CURRENT_USER    session defaults\r\n");
    out_ascii("\r\nSecond level:\r\n");
    int shown = 0;
    static char listed[32][REG_KLEN * 2];
    int nl = 0;
    for (int i = 0; i < REG_MAX; ++i) {
        if (!g_reg[i].used) continue;
        const char *k = g_reg[i].key;
        int n1 = 0; while (k[n1] && k[n1] != '\\') ++n1;
        if (!k[n1]) continue;                       /* 没有二级 */
        char sub[REG_KLEN];
        int m = 0;
        const char *r = k + n1 + 1;
        while (*r && *r != '\\' && m + 1 < (int)sizeof(sub)) sub[m++] = *r++;
        sub[m] = 0;
        if (!m) continue;
        char comb[REG_KLEN * 2];
        int cn = 0;
        for (int j = 0; j < n1 && cn + 1 < (int)sizeof(comb); ++j) comb[cn++] = k[j];
        comb[cn++] = '\\';
        for (int j = 0; j < m && cn + 1 < (int)sizeof(comb); ++j) comb[cn++] = sub[j];
        comb[cn] = 0;
        int dup = 0;
        for (int s = 0; s < nl; ++s) if (reg_ci_eq(listed[s], comb)) { dup = 1; break; }
        if (dup) continue;
        if (nl < 32) reg_shrink(listed[nl++], REG_KLEN * 2, comb);
        out_ascii("    ");
        out_ascii(comb);
        out_ascii("\r\n");
        shown++;
    }
    if (!shown) out_ascii("    (none)\r\n");
}

void cmd_reg(const CHAR16 *args16)
{
    reg_load();
    static char a[512];
    int i = 0;
    if (args16) { while (args16[i] && i + 1 < (int)sizeof(a)) { a[i] = (char)args16[i]; ++i; } }
    a[i] = 0;
    char *p = reg_trim(a);
    if (!*p) { reg_usage(); return; }
    char *verb = reg_next(&p);
    if (!verb) { reg_usage(); return; }
    if (reg_ci_eq(verb, "query"))  { reg_cmd_query(p); return; }
    if (reg_ci_eq(verb, "add"))    { reg_cmd_add(p); return; }
    if (reg_ci_eq(verb, "delete")) { reg_cmd_delete(p); return; }
    if (reg_ci_eq(verb, "enum"))   { reg_cmd_enum(); return; }
    if (reg_ci_eq(verb, "save"))   { g_reg_dirty = 1; reg_auto_save(); return; }
    outc(theme()->error, L"reg: unknown verb (query/add/delete/enum/save)\r\n");
}
