/*
 * jvm.c —— wctj：WCTEOS 自带 Java 运行时
 *
 * 只依赖 jvm.h 里的三个宿主能力，不调用 libc、不调用 UEFI 协议，
 * 因此同一份代码既能进内核（jvm_wct.inc），也能在 Linux 上做单元测试。
 *
 * 支持范围（实测覆盖，见 java-port/README.md）：
 *   - class 文件解析：常量池全部标签、字段/方法、Code/BootstrapMethods/ConstantValue
 *   - 字节码：整数/长整数/引用/数组/字符串/分支/异常表/invokedynamic(StringConcatFactory)
 *   - 原生类：System、PrintStream、String、StringBuilder、Integer、Long、Math、Object
 *   - WCT 扩展：wct.WctRuntime（调用 WCTEOS 内核 API）
 *
 * 不支持：浮点运算（按位保留，仅打印）、Lambda/MethodHandle 调用、反射、线程、GC 压缩。
 */
#include "jvm.h"

/* ------------------------------------------------------------------ */
/* 基础工具（不依赖 libc）                                             */
/* ------------------------------------------------------------------ */
static void *jmemset(void *d, int v, unsigned n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)v;
    return d;
}
static void jmemcpy(void *d, const void *s, unsigned n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n--) *a++ = *b++;
}
static int jstrlen(const char *s) { int n = 0; while (s && s[n]) ++n; return n; }
static int jstreq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; ++a; ++b; }
    return *a == *b;
}
static int jstrneq_prefix(const char *a, const char *b, int n) {
    for (int i = 0; i < n; ++i) { if (!a[i] || a[i] != b[i]) return 0; }
    return 1;
}
static char *jstrdup_arena(void *arena_base, int *used, int cap, const char *s, int len);

/* ------------------------------------------------------------------ */
/* 全局状态                                                            */
/* ------------------------------------------------------------------ */
#define MAX_CP        2048
#define MAX_METHODS   192
#define MAX_FIELDS    192
#define MAX_CLASSES   64
#define MAX_OBJ       4096
#define MAX_ARENA     (4 * 1024 * 1024)
#define MAX_DEPTH     32
#define MAX_SLOTS     96        /* 单帧局部变量/操作数最大槽数 */
#define MAX_STRING    8192

static const JvmHost *H;
static void *HC;
static int g_err;
static const char *g_err_text;
static int g_quiet;   /* 1 = 静默（用于 -version 之类） */

typedef struct { ju1 tag; ju2 a, b; ju4 v; ju4 v2; const char *s; ju4 slen; } Cp;

typedef struct {
    const char *name;
    const char *desc;
    int is_static;
    int slot;          /* 实例字段：相对对象起始；静态字段：相对静态区 */
    int slots;
} Field;

typedef struct { ju2 start, end, handler, catch_type; } ExEntry;

typedef struct {
    const char *name;
    const char *desc;
    int access;
    const ju1 *code;
    int code_len;
    int max_stack;
    int max_locals;
    ExEntry ex[16];
    int nex;
} Method;

typedef struct {
    const char *name;
    const char *super;
    Cp cp[MAX_CP];
    int cp_count;
    Field fields[MAX_FIELDS];
    int nfields;
    int inst_slots;
    int stat_slots;
    Method methods[MAX_METHODS];
    int nmethods;
    ji8 stat_vals[MAX_FIELDS];
    /* BootstrapMethods */
    struct { ju2 ref; ju2 nargs; ju2 args[16]; } bsm[16];
    int nbsm;
    int ok;
    char *path;   /* 实际加载路径 */
} KClass;

typedef struct {
    int type;      /* 0 空闲 1 普通对象 2 字符串 3 数组 4 PrintStream */
    int cls;       /* KClass 下标 */
    int nslots;
    ji8 *slots;
    int slen;
    char *sbuf;
    int alen;
    int acls;      /* 数组元素类 */
    ju1 aelem;     /* 元素宽度：1/4/8, 0=引用 */
    ji8 *adata;
    const char *exc_cls;   /* type==5 时的异常类名 */
    const char *exc_msg;   /* type==5 时的异常消息 */
} Obj;

static KClass classes[MAX_CLASSES];
static int nclasses;
static Obj objs[MAX_OBJ];
static int nobjs;

static char *arena_base;
static int arena_used;
static int arena_cap;

static char *cls_dir;          /* 主类所在目录（加载引用类时复用） */
static char g_sep = '\\';      /* 路径分隔符：UEFI 用 \\，POSIX 用 / */

static int g_next_id = 1;

/* ------------------------------------------------------------------ */
/* 错误                                                                */
/* ------------------------------------------------------------------ */
int jvm_last_error(void) { return g_err; }
const char *jvm_last_error_text(void) { return g_err_text; }

static void fail(int code, const char *text) {
    g_err = code;
    g_err_text = text;
    if (H && H->print) H->print(HC, text);
}

/* ------------------------------------------------------------------ */
/* 内存                                                                */
/* ------------------------------------------------------------------ */
static char *arena_alloc(int bytes, int align) {
    if (align < 8) align = 8;
    int off = (arena_used + (align - 1)) & ~(align - 1);
    if (off + bytes > arena_cap) return 0;
    char *p = arena_base + off;
    arena_used = off + bytes;
    jmemset(p, 0, bytes);
    return p;
}

/* ------------------------------------------------------------------ */
/* class 解析                                                          */
/* ------------------------------------------------------------------ */
static ju2 rd2(const ju1 *p) { return (ju2)((p[0] << 8) | p[1]); }
static ju4 rd4(const ju1 *p) { return ((ju4)p[0] << 24) | ((ju4)p[1] << 16) | ((ju4)p[2] << 8) | (ju4)p[3]; }

static const char *cp_utf8(KClass *c, ju2 idx) {
    if (idx == 0 || idx >= (ju2)c->cp_count) return "";
    if (c->cp[idx].tag != 1) return "";
    return c->cp[idx].s ? c->cp[idx].s : "";
}
static const char *cp_class_name(KClass *c, ju2 idx) {
    if (idx == 0 || idx >= (ju2)c->cp_count || c->cp[idx].tag != 7) return 0;
    return cp_utf8(c, c->cp[idx].a);
}

static KClass *find_class(const char *name);
static KClass *load_class(const char *name);
static Method *resolve_virtual(KClass *c, const char *name, const char *desc, KClass **owner);
static int exec_method(KClass *c, Method *m, ji8 *args, int nargs, ji8 *result);
static void print_object(Obj *o);
static int is_exception_class(const char *cn);
static void print_uncaught(const char *cls, const char *msg);

static int dcmp(KClass *c, ju2 nt_idx, const char **name, const char **desc) {
    if (nt_idx == 0 || nt_idx >= (ju2)c->cp_count || c->cp[nt_idx].tag != 12) return 0;
    *name = cp_utf8(c, c->cp[nt_idx].a);
    *desc = cp_utf8(c, c->cp[nt_idx].b);
    return 1;
}

/* 解析 UTF8 常量：Modified UTF-8 简化处理（仅处理常见 ASCII + \u00XX） */
static const char *decode_utf8(const ju1 *bytes, ju4 len, char *dst, int cap) {
    int o = 0;
    ju4 i = 0;
    while (i < len && o < cap - 1) {
        ju1 b = bytes[i];
        if (b == 0xC0 && i + 1 < len && bytes[i + 1] == 0x80) { dst[o++] = 0; i += 2; continue; }
        if (b < 0x80) { dst[o++] = (char)b; ++i; continue; }
        /* 多字节：保留为 '?'（类名/方法名都是 ASCII，不影响） */
        dst[o++] = '?';
        ++i;
    }
    dst[o] = 0;
    return dst;
}

static int parse_class(KClass *c, const ju1 *img, int len) {
    if (len < 32 || img[0] != 0xCA || img[1] != 0xFE || img[2] != 0xBA || img[3] != 0xBE) {
        fail(-10, "wctj: not a Java class file\n");
        return 0;
    }
    ju2 minor = rd2(img + 4), major = rd2(img + 6);
    if (major < 45 || major > 72) {
        fail(-11, "wctj: unsupported class version\n");
        return 0;
    }
    (void)minor;
    int p = 10;                       /* magic(4)+minor(2)+major(2)+cp_count(2) */
    c->cp_count = rd2(img + 8);
    if (c->cp_count > MAX_CP) { fail(-12, "wctj: constant pool too large\n"); return 0; }
    for (int i = 1; i < c->cp_count; ++i) {
        if (p >= len) { fail(-13, "wctj: truncated constant pool\n"); return 0; }
        ju1 tag = img[p++];
        c->cp[i].tag = tag;
        switch (tag) {
        case 1: {
            ju2 sl = rd2(img + p); p += 2;
            if (p + (int)sl > len) { fail(-13, "wctj: truncated utf8\n"); return 0; }
            char *buf = arena_alloc((int)sl + 1, 1);
            if (!buf) { fail(-20, "wctj: out of memory\n"); return 0; }
            decode_utf8(img + p, sl, buf, (int)sl + 1);
            c->cp[i].s = buf;
            c->cp[i].slen = sl;
            p += (int)sl;
            break;
        }
        case 3: case 4: c->cp[i].v = rd4(img + p); p += 4; break;
        case 5: case 6: {
            c->cp[i].v = rd4(img + p);          /* 高 32 位 */
            c->cp[i].v2 = rd4(img + p + 4);     /* 低 32 位 */
            p += 8; ++i; break;                 /* 占两个常量位 */
        }
        case 7: case 8: case 16: case 19: case 20:
            c->cp[i].a = rd2(img + p); p += 2; break;
        case 9: case 10: case 11: case 12: case 17: case 18:
            c->cp[i].a = rd2(img + p); c->cp[i].b = rd2(img + p + 2); p += 4; break;
        case 15:
            c->cp[i].a = (ju2)img[p]; c->cp[i].b = rd2(img + p + 1); p += 3; break;
        default:
            fail(-14, "wctj: unknown constant pool tag\n");
            return 0;
        }
    }
    /* access, this, super, interfaces */
    p += 2;
    ju2 this_idx = rd2(img + p); p += 2;
    ju2 super_idx = rd2(img + p); p += 2;
    c->name = cp_class_name(c, this_idx);
    c->super = (super_idx && super_idx < (ju2)c->cp_count && c->cp[super_idx].tag == 7)
                 ? cp_class_name(c, super_idx) : 0;
    ju2 ifaces = rd2(img + p); p += 2 + (int)ifaces * 2;

    /* 字段表 */
    ju2 nf = rd2(img + p); p += 2;
    for (int i = 0; i < nf; ++i) {
        ju2 acc = rd2(img + p); p += 2;
        ju2 ni = rd2(img + p); p += 2;
        ju2 di = rd2(img + p); p += 2;
        ju2 attrs = rd2(img + p); p += 2;
        const char *fname = cp_utf8(c, ni);
        const char *fdesc = cp_utf8(c, di);
        int is_static = (acc & 0x0008) != 0;
        int slots = (fdesc[0] == 'J' || fdesc[0] == 'D') ? 2 : 1;
        if (c->nfields < MAX_FIELDS) {
            Field *f = &c->fields[c->nfields];
            f->name = fname; f->desc = fdesc;
            f->is_static = is_static;
            f->slots = slots;
            f->slot = is_static ? c->stat_slots : c->inst_slots;
            if (is_static) c->stat_slots += slots; else c->inst_slots += slots;
            /* ConstantValue 属性：静态常量初值 */
            for (int a = 0; a < attrs; ++a) {
                ju2 an = rd2(img + p); p += 2;
                ju4 al = rd4(img + p); p += 4;
                if (jstreq(cp_utf8(c, an), "ConstantValue") && al >= 2) {
                    ju2 vi = rd2(img + p);
                    if (vi && vi < (ju2)c->cp_count) {
                        Cp *v = &c->cp[vi];
                        if (v->tag == 3) c->stat_vals[f->slot] = (ji8)(int)v->v;
                        else if (v->tag == 5) c->stat_vals[f->slot] = (ji8)((ju8)v->v << 32 | (ju8)v->v2);
                        else if (v->tag == 8) c->stat_vals[f->slot] = -1; /* 字符串，运行期填 */
                        else c->stat_vals[f->slot] = (ji8)(int)v->v;
                    }
                }
                p += (int)al;
            }
            ++c->nfields;
        } else {
            p += (int)0;
            for (int a = 0; a < attrs; ++a) { p += 2; ju4 al = rd4(img + p); p += 4 + (int)al; }
        }
    }

    /* 方法表 */
    ju2 nm = rd2(img + p); p += 2;
    for (int i = 0; i < nm; ++i) {
        ju2 acc = rd2(img + p); p += 2;
        ju2 ni = rd2(img + p); p += 2;
        ju2 di = rd2(img + p); p += 2;
        ju2 attrs = rd2(img + p); p += 2;
        Method *m = 0;
        if (c->nmethods < MAX_METHODS) {
            m = &c->methods[c->nmethods++];
            m->name = cp_utf8(c, ni);
            m->desc = cp_utf8(c, di);
            m->access = (int)acc;
            m->code = 0; m->code_len = 0; m->nex = 0;
            m->max_stack = 8; m->max_locals = 8;
        }
        for (int a = 0; a < attrs; ++a) {
            ju2 an = rd2(img + p); p += 2;
            ju4 al = rd4(img + p); p += 4;
            int end = p + (int)al;
            if (m && jstreq(cp_utf8(c, an), "Code")) {
                m->max_stack = (int)rd2(img + p);
                m->max_locals = (int)rd2(img + p + 2);
                ju4 cl = rd4(img + p + 4);
                m->code = img + p + 8;
                m->code_len = (int)cl;
                int q = p + 8 + (int)cl;
                ju2 exn = rd2(img + q); q += 2;
                for (int e = 0; e < exn && e < 16; ++e) {
                    m->ex[e].start = rd2(img + q);
                    m->ex[e].end = rd2(img + q + 2);
                    m->ex[e].handler = rd2(img + q + 4);
                    m->ex[e].catch_type = rd2(img + q + 6);
                    q += 8;
                    ++m->nex;
                }
            } else if (jstreq(cp_utf8(c, an), "BootstrapMethods")) {
                ju2 n = rd2(img + p);
                int q = p + 2;
                for (int b = 0; b < n && b < 16; ++b) {
                    c->bsm[b].ref = rd2(img + q); q += 2;
                    ju2 na = rd2(img + q); q += 2;
                    c->bsm[b].nargs = na;
                    for (int k = 0; k < na && k < 16; ++k) { c->bsm[b].args[k] = rd2(img + q); q += 2; }
                    ++c->nbsm;
                }
            }
            p = end;
        }
    }
    /* 类级属性：BootstrapMethods 在这里（不是方法属性） */
    if (p + 2 <= len) {
        ju2 cattrs = rd2(img + p); p += 2;
        for (int a = 0; a < cattrs && p + 6 <= len; ++a) {
            ju2 an = rd2(img + p); p += 2;
            ju4 al = rd4(img + p); p += 4;
            int cend = p + (int)al;
            if (jstreq(cp_utf8(c, an), "BootstrapMethods")) {
                ju2 nb = rd2(img + p);
                int q = p + 2;
                for (int b = 0; b < nb && b < 16 && q + 4 <= cend; ++b) {
                    c->bsm[b].ref = rd2(img + q); q += 2;
                    ju2 na2 = rd2(img + q); q += 2;
                    c->bsm[b].nargs = na2;
                    for (int k = 0; k < na2 && k < 16 && q + 2 <= cend; ++k) { c->bsm[b].args[k] = rd2(img + q); q += 2; }
                    ++c->nbsm;
                }
            }
            p = cend;
        }
    }

    return 1;
}

static KClass *find_class(const char *name) {
    for (int i = 0; i < nclasses; ++i)
        if (jstreq(classes[i].name, name)) return &classes[i];
    return 0;
}

static KClass *load_class(const char *name) {
    KClass *ex = find_class(name);
    if (ex) return ex->ok ? ex : 0;
    if (nclasses >= MAX_CLASSES) return 0;
    KClass *c = &classes[nclasses++];
    jmemset(c, 0, sizeof(*c));
    c->name = name;
    /* 路径：<cls_dir>/<name>.class，包名 / 转目录分隔符（FAT 用 \） */
    char path[220];
    int n = 0;
    if (cls_dir) { while (cls_dir[n] && n < 120) { path[n] = cls_dir[n]; ++n; } }
    if (n && path[n - 1] != '\\' && path[n - 1] != '/') path[n++] = g_sep;
    for (const char *q = name; *q && n < 200; ++q) path[n++] = (*q == '/') ? g_sep : *q;
    path[n] = 0;
    char full[240];
    for (int i = 0; i <= n; ++i) full[i] = path[i];
    int fl = n;
    const char *ext = ".class";
    for (int i = 0; ext[i] && fl < 235; ++i) full[fl++] = ext[i];
    full[fl] = 0;

    unsigned char *buf = 0;
    int blen = 0;
    if (!H->read_class(HC, full, &buf, &blen) || !buf || blen <= 0) {
        char msg[280];
        int k = 0;
        const char *a = "wctj: cannot find class ";
        while (*a && k < 200) msg[k++] = *a++;
        for (int i = 0; name[i] && k < 240; ++i) msg[k++] = name[i];
        msg[k++] = ' '; msg[k++] = '('; 
        for (int i = 0; full[i] && k < 258; ++i) msg[k++] = full[i];
        msg[k++] = ')'; msg[k++] = '\n'; msg[k] = 0;
        fail(-2, msg);
        return 0;
    }
    c->path = 0;
    if (!parse_class(c, buf, blen)) return 0;
    /* 计算实例槽位（继承父类） */
    int inherited = 0;
    if (c->super && !jstreq(c->super, "java/lang/Object")) {
        KClass *sc = load_class(c->super);
        if (sc) inherited = sc->inst_slots;
    }
    for (int i = 0; i < c->nfields; ++i) {
        Field *f = &c->fields[i];
        if (!f->is_static) { f->slot += inherited; }
    }
    c->inst_slots += inherited;
    c->ok = 1;
    return c;
}

/* ------------------------------------------------------------------ */
/* 对象 / 数组 / 字符串                                                */
/* ------------------------------------------------------------------ */
static Obj *obj_new(int type, KClass *c) {
    if (nobjs >= MAX_OBJ) { fail(-21, "wctj: object table full\n"); return 0; }
    Obj *o = &objs[nobjs];
    jmemset(o, 0, sizeof(*o));
    o->type = type;
    o->cls = c ? (int)(c - classes) : -1;
    if (type == 1) {
        int n = c ? c->inst_slots : 0;
        if (n < 1) n = 1;
        o->nslots = n;
        o->slots = (ji8 *)arena_alloc(n * (int)sizeof(ji8), 8);
        if (!o->slots) return 0;
    }
    ++nobjs;
    return o;
}

static int obj_ref(Obj *o) { return (int)(o - objs) + 1; }
static Obj *ref_obj(ji8 v) {
    int i = (int)v - 1;
    if (i < 0 || i >= nobjs) return 0;
    return &objs[i];
}

static Obj *new_string(const char *s, int len) {
    Obj *o = obj_new(2, 0);
    if (!o) return 0;
    if (len < 0) len = 0;
    if (len > MAX_STRING) len = MAX_STRING;
    o->sbuf = (char *)arena_alloc(len + 1, 1);
    if (!o->sbuf) return 0;
    for (int i = 0; i < len; ++i) o->sbuf[i] = s[i];
    o->sbuf[len] = 0;
    o->slen = len;
    return o;
}

static Obj *new_array(int elem, int len, int acls) {
    if (len < 0) len = 0;
    if (len > 100000) len = 100000;
    Obj *o = obj_new(3, 0);
    if (!o) return 0;
    o->aelem = (ju1)elem;
    o->alen = len;
    o->acls = acls;
    int bytes = len * (elem ? elem : 8);
    if (bytes > 0) {
        o->adata = (ji8 *)arena_alloc(bytes, 8);
        if (!o->adata) return 0;
    }
    return o;
}

/* ------------------------------------------------------------------ */
/* 描述符                                                              */
/* ------------------------------------------------------------------ */
static int desc_slots_of(char t) { return (t == 'J' || t == 'D') ? 2 : 1; }

static int desc_arg_slots(const char *d) {
    int n = 1;                       /* this */
    if (!d) return n;
    int i = 1;
    while (d[i] && d[i] != ')') {
        if (d[i] == 'L') { while (d[i] && d[i] != ';') ++i; ++i; n += 1; }
        else if (d[i] == '[') { while (d[i] == '[') ++i; if (d[i] == 'L') { while (d[i] && d[i] != ';') ++i; } ++i; n += 1; }
        else { n += desc_slots_of(d[i]); ++i; }
    }
    return n;
}

static int desc_ret_type(const char *d) {
    if (!d) return 'V';
    while (*d && *d != ')') ++d;
    if (!*d) return 'V';
    ++d;
    return *d;
}

/* ------------------------------------------------------------------ */
/* 打印工具                                                            */
/* ------------------------------------------------------------------ */
static char *g_out;
static int g_out_len;
static int g_out_cap;

static void out_flush(void) {
    if (!g_out_len) return;
    g_out[g_out_len] = 0;
    if (H && H->print) H->print(HC, g_out);
    g_out_len = 0;
}

static void out_put(const char *s, int len) {
    if (len < 0) len = jstrlen(s);
    if (!g_out) return;
    if (g_out_len + len + 2 >= g_out_cap) out_flush();
    if (len >= g_out_cap - 2) {
        /* 超长单行：分段写 */
        int done = 0;
        while (done < len) {
            int chunk = len - done;
            if (chunk > g_out_cap - 2) chunk = g_out_cap - 2;
            for (int i = 0; i < chunk; ++i) g_out[i] = s[done + i];
            g_out_len = chunk;
            out_flush();
            done += chunk;
        }
        return;
    }
    for (int i = 0; i < len; ++i) g_out[g_out_len++] = s[i];
}

static void out_s(const char *s) { out_put(s, jstrlen(s)); }
static void out_c(char c) { out_put(&c, 1); }

static void out_ll(ji8 v) {
    char b[32];
    int n = 0;
    unsigned long long u;
    if (v < 0) { out_c('-'); u = (unsigned long long)(-v); } else u = (unsigned long long)v;
    if (!u) { out_c('0'); return; }
    while (u && n < 31) { b[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n) out_c(b[--n]);
}

/* 双精度：位拆分 + 整数算法，最多 6 位小数（避免使用 FPU） */
static void out_double_bits(ju8 bits) {
    int sign = (int)(bits >> 63);
    int exp = (int)((bits >> 52) & 0x7FF);
    ju8 mant = bits & 0xFFFFFFFFFFFFFULL;
    if (exp == 0x7FF) { out_s(mant ? "NaN" : (sign ? "-Infinity" : "Infinity")); return; }
    if (sign) out_c('-');
    if (exp == 0 && mant == 0) { out_s("0.0"); return; }
    /* value = mant * 2^(exp-1075) */
    int e2 = exp - 1075;
    if (exp) mant |= 0x10000000000000ULL;
    /* 整数部分 */
    if (e2 >= 0 && e2 < 63) {
        ji8 iv = (ji8)(mant << e2);
        out_ll(iv);
        out_s(".0");
        return;
    }
    if (e2 < 0 && e2 > -60) {
        ji8 ip = (ji8)(mant >> (-e2));
        ji8 frac = (ji8)(mant & (((ju8)1 << (-e2)) - 1));
        /* 小数：frac / 2^(-e2)，取 6 位 */
        out_ll(ip);
        out_c('.');
        ji8 denom = (ji8)1 << (-e2);
        for (int d = 0; d < 6; ++d) {
            frac *= 10;
            ji8 digit = frac / denom;
            if (digit > 9) digit = 9;
            out_c((char)('0' + (int)digit));
            frac -= digit * denom;
        }
        return;
    }
    out_s("1e");
    out_ll(e2);
}

/* ------------------------------------------------------------------ */
/* 字符串原生                                                          */
/* ------------------------------------------------------------------ */
static void str_of(Obj *o, const char **s, int *len) {
    if (!o || o->type != 2) { *s = ""; *len = 0; return; }
    *s = o->sbuf ? o->sbuf : "";
    *len = o->slen;
}

static Obj *sb_get(Obj *sb) { return sb; }

static void sb_append_str(Obj *sb, const char *s, int len) {
    if (!sb) return;
    if (sb->slen + len > MAX_STRING) len = MAX_STRING - sb->slen;
    if (len <= 0) return;
    char *nb = (char *)arena_alloc(sb->slen + len + 1, 1);
    if (!nb) return;
    for (int i = 0; i < sb->slen; ++i) nb[i] = sb->sbuf[i];
    for (int i = 0; i < len; ++i) nb[sb->slen + i] = s[i];
    nb[sb->slen + len] = 0;
    sb->sbuf = nb;
    sb->slen += len;
}

/* ------------------------------------------------------------------ */
/* 原生方法                                                            */
/* ------------------------------------------------------------------ */
static KClass *cls_of(Obj *o) {
    if (!o || o->cls < 0 || o->cls >= nclasses) return 0;
    return &classes[o->cls];
}

static const char *obj_class_name(Obj *o) {
    if (!o) return "null";
    if (o->type == 2) return "java/lang/String";
    if (o->type == 3) return "array";
    KClass *c = cls_of(o);
    return c ? c->name : "java/lang/Object";
}

static void print_object(Obj *o) {
    if (!o) { out_s("null"); return; }
    if (o->type == 2) { out_put(o->sbuf ? o->sbuf : "", o->slen); return; }
    if (o->type == 3) {
        out_s("[");
        int max = o->alen > 24 ? 24 : o->alen;
        for (int i = 0; i < max; ++i) {
            if (i) out_s(", ");
            if (o->aelem == 0) print_object(ref_obj(o->adata[i]));
            else if (o->aelem == 1) out_ll((ji8)((signed char *)o->adata)[i]);
            else if (o->aelem == 2) out_ll((ji8)((short *)o->adata)[i]);
            else if (o->aelem == 4) out_ll((ji8)((int *)o->adata)[i]);
            else out_ll(o->adata[i]);
        }
        if (o->alen > max) out_s(", ...");
        out_s("]");
        return;
    }
    out_s(obj_class_name(o));
    out_c('@');
    out_ll((ji8)(1 + (o - objs)));
}

/* 调用对象的 toString（Java 语义），返回字符串对象引用 */
static ji8 obj_to_string(Obj *o) {
    if (!o) return 0;
    if (o->type == 2) return (ji8)obj_ref(o);
    KClass *c = cls_of(o);
    if (c) {
        KClass *owner = 0;
        Method *m = resolve_virtual(c, "toString", "()Ljava/lang/String;", &owner);
        if (m) {
            ji8 a1[1];
            ji8 r = 0;
            a1[0] = (ji8)obj_ref(o);
            if (exec_method(owner, m, a1, 1, &r) == 0 && r) return r;
        }
    }
    g_out_len = 0;
    print_object(o);
    Obj *ns = new_string(g_out, g_out_len);
    g_out_len = 0;
    return ns ? (ji8)obj_ref(ns) : 0;
}

/* 把槽里的值按描述符打印 */
static void print_typed(char t, ji8 v);

static void print_typed(char t, ji8 v) {
    switch (t) {
    case 'I': case 'S': case 'B': out_ll((ji8)(int)v); break;
    case 'J': out_ll(v); break;
    case 'Z': out_s((int)v ? "true" : "false"); break;
    case 'C': {
        char c = (char)(int)v;
        if (c >= 32 && c < 127) out_c(c); else out_s("?");
        break;
    }
    case 'D': out_double_bits((ju8)v); break;
    case 'F': out_double_bits((ju8)v); break;
    default: {
        Obj *so = ref_obj(obj_to_string(ref_obj(v)));
        if (so && so->type == 2) out_put(so->sbuf, so->slen);
        else print_object(ref_obj(v));
        break;
    }
    }
}

/* 返回 1 表示是原生方法且已处理（结果写 result） */
/* ------------------------------------------------------------------ */

#ifdef WCTJ_TRACE
static void tr(const char *a, const char *b, const char *c, int n) {
    char t[200]; int k = 0;
    while (*a && k < 180) t[k++] = *a++;
    t[k++] = ' '; while (b && *b && k < 190) t[k++] = *b++;
    t[k++] = ' '; while (c && *c && k < 195) t[k++] = *c++;
    t[k++] = ' '; t[k++] = (char)('0' + (n % 10)); t[k++] = '\n'; t[k] = 0;
    H->print(HC, t);
}
#else
#define tr(a,b,c,n) ((void)0)
#endif

static ji8 *g_locals;
static ji8 *g_stack;
static int g_sp;

static int exec_method(KClass *c, Method *m, ji8 *args, int nargs, ji8 *result);

static Method *find_method(KClass *c, const char *name, const char *desc) {
    for (int i = 0; i < c->nmethods; ++i)
        if (jstreq(c->methods[i].name, name) && jstreq(c->methods[i].desc, desc))
            return &c->methods[i];
    return 0;
}

/* 虚方法解析：沿父类链查找 */
static Method *resolve_virtual(KClass *c, const char *name, const char *desc, KClass **owner) {
    KClass *k = c;
    for (int depth = 0; k && depth < 8; ++depth) {
        Method *m = find_method(k, name, desc);
        if (m) { if (owner) *owner = k; return m; }
        if (!k->super) break;
        k = find_class(k->super);
    }
    return 0;
}

static void push(ji8 v) { if (g_sp < MAX_SLOTS) g_stack[g_sp++] = v; }
static ji8 pop(void) { return g_sp > 0 ? g_stack[--g_sp] : 0; }

/* invokedynamic：只支持 StringConcatFactory.makeConcatWithConstants */
/* invokedynamic：支持 StringConcatFactory.makeConcatWithConstants / makeConcat */
static int do_indy(KClass *c, ju2 idx, ji8 *result, int *handled) {
    *handled = 0;
    if (idx == 0 || idx >= (ju2)c->cp_count || c->cp[idx].tag != 18) return 0;
    ju2 bsm_i = c->cp[idx].a;
    if (bsm_i >= (ju2)c->nbsm) return 0;
    ju2 ref = c->bsm[bsm_i].ref;
    if (ref == 0 || ref >= (ju2)c->cp_count || c->cp[ref].tag != 15) return 0;
    ju2 mref = c->cp[ref].b;
    if (mref == 0 || mref >= (ju2)c->cp_count) return 0;
    if (c->cp[mref].tag != 10 && c->cp[mref].tag != 11) return 0;
    const char *owner = cp_class_name(c, c->cp[mref].a);
    const char *bname = 0, *bdesc = 0;
    if (!dcmp(c, c->cp[mref].b, &bname, &bdesc)) return 0;
    (void)bdesc;
    if (!owner || !jstreq(owner, "java/lang/invoke/StringConcatFactory")) return 0;
    int with_consts = jstreq(bname, "makeConcatWithConstants");
    if (!with_consts && !jstreq(bname, "makeConcat")) return 0;

    /* 调用点描述符：返回值必须是 String，参数按槽位排 */
    const char *lname = 0, *ldesc = 0;
    if (!dcmp(c, c->cp[idx].b, &lname, &ldesc)) return 0;
    (void)lname;
    int nslots = desc_arg_slots(ldesc) - 1;
    if (nslots < 0) nslots = 0;
    if (g_sp < nslots) return 0;
    int base = g_sp - nslots;

    /* 收集参数类型（按槽位顺序） */
    char types[32][2];
    int ntypes = 0;
    {
        int q = 1;
        while (ldesc[q] && ldesc[q] != ')' && ntypes < 32) {
            char ty;
            if (ldesc[q] == 'L') { while (ldesc[q] && ldesc[q] != ';') ++q; ++q; ty = 'L'; }
            else if (ldesc[q] == '[') { while (ldesc[q] == '[') ++q; if (ldesc[q] == 'L') { while (ldesc[q] && ldesc[q] != ';') ++q; } ++q; ty = 'L'; }
            else { ty = ldesc[q]; ++q; }
            types[ntypes][0] = ty;
            types[ntypes][1] = (ty == 'J' || ty == 'D') ? 2 : 1;
            ++ntypes;
        }
    }

    /* recipe 与常量 */
    const char *recipe = 0;
    ju2 consts[16];
    int nconsts = 0;
    if (with_consts) {
        ju2 na = c->bsm[bsm_i].nargs;
        if (na > 0) {
            ju2 a0 = c->bsm[bsm_i].args[0];
            if (a0 && a0 < (ju2)c->cp_count) {
                if (c->cp[a0].tag == 8) recipe = cp_utf8(c, c->cp[a0].a);
                else if (c->cp[a0].tag == 1) recipe = c->cp[a0].s;
            }
            for (int i = 1; i < (int)na && i < 16; ++i) consts[nconsts++] = c->bsm[bsm_i].args[i];
        }
        if (!recipe) recipe = "";
    }

    Obj *sb = obj_new(2, 0);
    if (!sb) return 0;
    sb->sbuf = (char *)arena_alloc(1, 1);
    if (!sb->sbuf) return 0;
    sb->sbuf[0] = 0;
    sb->slen = 0;

    int slot = 0;       /* 已消费的参数字节码槽位 */
    int argi = 0;       /* 已消费的参数个数 */
    int ci = 0;

    if (!with_consts) {
        /* makeConcat：直接把所有参数拼接 */
        for (int i = 0; i < ntypes; ++i) {
            g_out_len = 0;
            if (types[i][0] == 'J') out_ll(g_stack[base + slot]);
            else print_typed(types[i][0], g_stack[base + slot]);
            sb_append_str(sb, g_out, g_out_len);
            g_out_len = 0;
            slot += types[i][1];
            ++argi;
        }
    } else {
        for (int i = 0; recipe[i]; ++i) {
            unsigned char rc = (unsigned char)recipe[i];
            if (rc == 1) {
                if (argi < ntypes) {
                    g_out_len = 0;
                    if (types[argi][0] == 'J') out_ll(g_stack[base + slot]);
                    else print_typed(types[argi][0], g_stack[base + slot]);
                    sb_append_str(sb, g_out, g_out_len);
                    g_out_len = 0;
                    slot += types[argi][1];
                    ++argi;
                }
            } else if (rc == 2) {
                if (ci < nconsts && consts[ci] && consts[ci] < (ju2)c->cp_count) {
                    Cp *k = &c->cp[consts[ci]];
                    if (k->tag == 8) {
                        const char *sv = cp_utf8(c, k->a);
                        sb_append_str(sb, sv, jstrlen(sv));
                    } else if (k->tag == 3) {
                        g_out_len = 0; out_ll((ji8)(int)k->v);
                        sb_append_str(sb, g_out, g_out_len); g_out_len = 0;
                    } else if (k->tag == 5) {
                        g_out_len = 0; out_ll((ji8)((ju8)k->v << 32 | (ju8)k->v2));
                        sb_append_str(sb, g_out, g_out_len); g_out_len = 0;
                    }
                }
                ++ci;
            } else {
                char one = (char)rc;
                sb_append_str(sb, &one, 1);
            }
        }
    }
    g_sp = base;
    *result = (ji8)obj_ref(sb);
    *handled = 1;
    return 1;
}


/* ------------------------------------------------------------------ */
/* 值语义助手（供集合类 / Arrays / Objects 使用）                      */
/* ------------------------------------------------------------------ */
static int vhash(ji8 ref)
{
    Obj *o = ref_obj(ref);
    if (!o) return 0;
    if (o->type == 2) {
        ji8 h = 0;
        for (int i = 0; i < o->slen; ++i) h = h * 31 + (ji8)(unsigned char)o->sbuf[i];
        return (int)h;
    }
    return (int)(ref * 31 + 7);
}

static int veq(ji8 a, ji8 b)
{
    if (a == b) return 1;
    Obj *oa = ref_obj(a), *ob = ref_obj(b);
    if (!oa || !ob) return 0;
    if (oa->type == 2 && ob->type == 2) {
        if (oa->slen != ob->slen) return 0;
        for (int i = 0; i < oa->slen; ++i) if (oa->sbuf[i] != ob->sbuf[i]) return 0;
        return 1;
    }
    return 0;
}

static Obj *list_new(void)
{
    Obj *l = obj_new(1, 0);
    if (!l) return 0;
    l->alen = 0;
    l->slen = 0;
    l->adata = 0;
    return l;
}

static int list_reserve(Obj *l, int need)
{
    if (!l) return 0;
    if (l->alen >= need) return 1;
    int cap = l->alen ? l->alen * 2 : 4;
    if (cap < need) cap = need;
    ji8 *nd = (ji8 *)arena_alloc(cap * 8, 8);
    if (!nd) return 0;
    for (int i = 0; i < l->slen; ++i) nd[i] = l->adata[i];
    l->adata = nd;
    l->alen = cap;
    return 1;
}

/* HashMap：线性探测，表项为 [keyRef, valRef] 交替 */
static Obj *map_new(void)
{
    Obj *m = obj_new(1, 0);
    if (!m) return 0;
    /* alen 表示"键值对"容量，adata 需要 2*alen 个槽 */
    m->alen = 8;
    m->slen = 0;
    m->adata = (ji8 *)arena_alloc(m->alen * 2 * 8, 8);
    if (!m->adata) return 0;
    for (int i = 0; i < m->alen * 2; ++i) m->adata[i] = 0;
    return m;
}

static int map_slot(Obj *m, ji8 key, int *found)
{
    int cap = m->alen;
    int idx = (vhash(key) & 0x7FFFFFFF) % cap;
    for (int probe = 0; probe < cap; ++probe) {
        int i = (idx + probe) % cap;
        ji8 k = m->adata[i * 2];
        if (!k) { *found = 0; return i; }
        if (veq(k, key)) { *found = 1; return i; }
    }
    *found = 0;
    return -1;
}

static int map_grow(Obj *m)
{
    int oldcap = m->alen;
    ji8 *old = m->adata;
    int newcap = oldcap * 2;
    ji8 *nd = (ji8 *)arena_alloc(newcap * 2 * 8, 8);
    if (!nd) return 0;
    for (int i = 0; i < newcap * 2; ++i) nd[i] = 0;
    m->adata = nd;
    m->alen = newcap;
    m->slen = 0;
    for (int i = 0; i < oldcap; ++i) {
        ji8 k = old[i * 2];
        if (!k) continue;
        int found = 0;
        int slot = map_slot(m, k, &found);
        if (slot >= 0) { nd[slot * 2] = k; nd[slot * 2 + 1] = old[i * 2 + 1]; ++m->slen; }
    }
    return 1;
}

static Obj *str_slice(Obj *src, int from, int to)
{
    if (!src) return 0;
    if (from < 0) from = 0;
    if (to > src->slen) to = src->slen;
    if (to < from) to = from;
    return new_string(src->sbuf + from, to - from);
}

static int call_native(const char *cn, const char *name, const char *desc,
                       ji8 *args, int nargs, ji8 *result, int *ret_slots) {
    *ret_slots = 0;
    result[0] = 0;

    /* ---- java/lang/Object ---- */
    if (jstreq(cn, "java/lang/Object")) {
        if (jstreq(name, "<init>")) { *ret_slots = 0; return 1; }
        if (jstreq(name, "toString")) {
            Obj *o = ref_obj(args[0]);
            g_out_len = 0;
            print_object(o);
            Obj *s = new_string(g_out, g_out_len);
            g_out_len = 0;
            result[0] = s ? obj_ref(s) : 0;
            *ret_slots = 1;
            return 1;
        }
        if (jstreq(name, "hashCode")) { result[0] = args[0]; *ret_slots = 1; return 1; }
        if (jstreq(name, "equals")) { result[0] = (args[0] == args[1]) ? 1 : 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "getClass")) { result[0] = 0; *ret_slots = 1; return 1; }
    }

    /* ---- java/lang/System ---- */
    if (jstreq(cn, "java/lang/System")) {
        if (jstreq(name, "currentTimeMillis") || jstreq(name, "nanoTime")) {
            result[0] = 0;
            *ret_slots = 1;
            return 1;
        }
        if (jstreq(name, "exit")) { *ret_slots = 0; g_err = 0; g_err_text = "wctj: System.exit\n"; return 1; }
        if (jstreq(name, "gc")) { *ret_slots = 0; return 1; }
        if (jstreq(name, "getProperty")) {
            Obj *s = new_string("WCTEOS", 6);
            result[0] = s ? obj_ref(s) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "arraycopy")) { *ret_slots = 0; return 1; }
    }

    /* ---- java/io/PrintStream ---- */
    if (jstreq(cn, "java/io/PrintStream")) {
        if (jstrneq_prefix(name, "print", 5) || jstrneq_prefix(name, "println", 7)) {
            int nl = jstrneq_prefix(name, "println", 7);
            if (args[1] != 0) {
                Obj *o = ref_obj(args[1]);
                char t = desc_ret_type(0); (void)t;
                /* 依据参数描述符打印 */
                char ty = 'L';
                const char *p = desc + 1;
                if (p[0] == 'I' || p[0] == 'S' || p[0] == 'B' || p[0] == 'J' ||
                    p[0] == 'Z' || p[0] == 'C' || p[0] == 'D' || p[0] == 'F' || p[0] == 'L' || p[0] == '[')
                    ty = p[0];
                if (ty == 'L' || ty == '[') {
                    if (!o) out_s("null");
                    else {
                        Obj *so = ref_obj(obj_to_string(o));
                        if (so && so->type == 2) out_put(so->sbuf, so->slen);
                        else out_s(obj_class_name(o));
                    }
                } else if (ty == 'J') {
                    out_ll(args[1]);
                } else {
                    print_typed(ty, args[1]);
                }
            }
            if (nl) out_c('\n');
            out_flush();
            *ret_slots = 0;
            return 1;
        }
    }

    /* ---- java/lang/String ---- */
    if (jstreq(cn, "java/lang/String")) {
        Obj *s = ref_obj(args[0]);
        const char *sp = 0; int sl = 0;
        str_of(s, &sp, &sl);
        if (jstreq(name, "length")) { result[0] = sl; *ret_slots = 1; return 1; }
        if (jstreq(name, "isEmpty")) { result[0] = sl == 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "charAt")) {
            int i = (int)args[1];
            result[0] = (i >= 0 && i < sl) ? (ji8)(unsigned char)sp[i] : -1;
            *ret_slots = 1; return 1;
        }
        if (jstreq(name, "substring") || jstreq(name, "subSequence")) {
            if (nargs == 3) {
                int b = (int)args[1], e = (int)args[2];
                if (b < 0) b = 0; if (e > sl) e = sl; if (e < b) e = b;
                Obj *n = new_string(sp + b, e - b);
                result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
            }
            if (nargs == 2) {
                int b = (int)args[1];
                if (b < 0) b = 0; if (b > sl) b = sl;
                Obj *n = new_string(sp + b, sl - b);
                result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
            }
        }
        if (jstreq(name, "toString")) { result[0] = args[0]; *ret_slots = 1; return 1; }
        if (jstreq(name, "equals")) {
            Obj *o = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(o, &q, &ql);
            int eq = (sl == ql);
            for (int i = 0; eq && i < sl; ++i) if (sp[i] != q[i]) eq = 0;
            result[0] = eq; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "equalsIgnoreCase")) {
            Obj *o = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(o, &q, &ql);
            int eq = (sl == ql);
            for (int i = 0; eq && i < sl; ++i) {
                char a = sp[i], b = q[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) eq = 0;
            }
            result[0] = eq; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "toUpperCase") || jstreq(name, "toLowerCase")) {
            int up = jstreq(name, "toUpperCase");
            Obj *n = new_string(sp, sl);
            if (n) for (int i = 0; i < sl; ++i) {
                char ch = n->sbuf[i];
                if (up && ch >= 'a' && ch <= 'z') n->sbuf[i] = (char)(ch - 32);
                if (!up && ch >= 'A' && ch <= 'Z') n->sbuf[i] = (char)(ch + 32);
            }
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "trim")) {
            int b = 0, e = sl;
            while (b < e && (sp[b] == ' ' || sp[b] == '\t' || sp[b] == '\n' || sp[b] == '\r')) ++b;
            while (e > b && (sp[e - 1] == ' ' || sp[e - 1] == '\t' || sp[e - 1] == '\n' || sp[e - 1] == '\r')) --e;
            Obj *n = new_string(sp + b, e - b);
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "indexOf")) {
            if (nargs >= 3 && nargs == 3 && desc[1] == 'I') {
                int from = (int)args[2];
                if (from < 0) from = 0;
                int found = -1;
                for (int i = from; i < sl; ++i) if (sp[i] == (char)(int)args[1]) { found = i; break; }
                result[0] = found; *ret_slots = 1; return 1;
            }
            Obj *o = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(o, &q, &ql);
            int found = -1;
            if (ql == 0) found = 0;
            for (int i = 0; i + ql <= sl && found < 0; ++i) {
                int ok = 1;
                for (int k = 0; k < ql; ++k) if (sp[i + k] != q[k]) { ok = 0; break; }
                if (ok) found = i;
            }
            result[0] = found; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "contains")) {
            Obj *o = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(o, &q, &ql);
            int found = (ql == 0);
            for (int i = 0; i + ql <= sl && !found; ++i) {
                int ok = 1;
                for (int k = 0; k < ql; ++k) if (sp[i + k] != q[k]) { ok = 0; break; }
                if (ok) found = 1;
            }
            result[0] = found; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "startsWith")) {
            Obj *o = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(o, &q, &ql);
            int ok = (ql <= sl);
            for (int k = 0; ok && k < ql; ++k) if (sp[k] != q[k]) ok = 0;
            result[0] = ok; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "concat")) {
            Obj *o = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(o, &q, &ql);
            Obj *n = new_string(sp, sl);
            if (n) sb_append_str(n, q, ql);
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "replace")) {
            char a = (char)(int)args[1], b = (char)(int)args[2];
            Obj *n = new_string(sp, sl);
            if (n) for (int i = 0; i < sl; ++i) if (n->sbuf[i] == a) n->sbuf[i] = b;
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "hashCode")) {
            ji8 h = 0;
            for (int i = 0; i < sl; ++i) h = h * 31 + (ji8)(unsigned char)sp[i];
            result[0] = (ji8)(int)h; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "valueOf")) {
            /* String.valueOf(int/long/char/boolean/Object) */
            g_out_len = 0;
            char ty = desc[1];
            if (ty == 'J') out_ll(args[0]);
            else if (ty == 'D') out_double_bits((ju8)args[0]);
            else if (ty == '[' && desc[2] == 'C') {
                Obj *arr = ref_obj(args[0]);
                if (arr && arr->type == 3) {
                    for (int i = 0; i < arr->alen; ++i) out_c((char)(((short *)arr->adata)[i]));
                } else out_s("null");
            }
            else if (ty == 'L' || ty == '[') {
                Obj *so = ref_obj(obj_to_string(ref_obj(args[0])));
                if (so && so->type == 2) out_put(so->sbuf, so->slen);
                else out_s("null");
            }
            else print_typed(ty, args[0]);
            Obj *n = new_string(g_out, g_out_len);
            g_out_len = 0;
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
    }

    /* ---- java/lang/StringBuilder ---- */
    if (jstreq(cn, "java/lang/StringBuilder") || jstreq(cn, "java/lang/StringBuffer")) {
        Obj *sb = ref_obj(args[0]);
        if (jstreq(name, "<init>")) {
            if (nargs >= 2 && desc[1] == 'L') {
                Obj *o = ref_obj(args[1]);
                const char *q = 0; int ql = 0;
                str_of(o, &q, &ql);
                if (sb) { sb->sbuf = 0; sb->slen = 0; sb->type = 2; sb_append_str(sb, q, ql); }
            } else if (sb) { sb->type = 2; sb->sbuf = (char *)arena_alloc(1, 1); if (sb->sbuf) sb->sbuf[0] = 0; }
            *ret_slots = 0; return 1;
        }
        if (jstreq(name, "append")) {
            if (sb) {
                char ty = 'L';
                const char *p = desc + 1;
                if (*p) ty = *p;
                if (ty == 'L' || ty == '[') {
                    Obj *o = ref_obj(args[1]);
                    if (!o) sb_append_str(sb, "null", 4);
                    else {
                        Obj *so = ref_obj(obj_to_string(o));
                        if (so && so->type == 2) sb_append_str(sb, so->sbuf, so->slen);
                        else sb_append_str(sb, obj_class_name(o), jstrlen(obj_class_name(o)));
                    }
                } else if (ty == 'J') {
                    g_out_len = 0; out_ll(args[1]);
                    sb_append_str(sb, g_out, g_out_len); g_out_len = 0;
                } else if (ty == 'D') {
                    g_out_len = 0; out_double_bits((ju8)args[1]);
                    sb_append_str(sb, g_out, g_out_len); g_out_len = 0;
                } else {
                    g_out_len = 0;
                    print_typed(ty, args[1]);
                    sb_append_str(sb, g_out, g_out_len);
                    g_out_len = 0;
                }
            }
            result[0] = args[0]; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "toString")) {
            Obj *n = sb ? new_string(sb->sbuf ? sb->sbuf : "", sb->slen) : 0;
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "length")) { result[0] = sb ? sb->slen : 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "reverse")) {
            if (sb) for (int i = 0; i < sb->slen / 2; ++i) {
                char t = sb->sbuf[i];
                sb->sbuf[i] = sb->sbuf[sb->slen - 1 - i];
                sb->sbuf[sb->slen - 1 - i] = t;
            }
            result[0] = args[0]; *ret_slots = 1; return 1;
        }
    }

    /* ---- 异常类（Throwable 系列） ---- */
    if (is_exception_class(cn)) {
        Obj *o = ref_obj(args[0]);
        if (jstreq(name, "<init>")) {
            if (nargs >= 2 && desc[1] == 'L') {
                Obj *m2 = ref_obj(args[1]);
                if (o && m2 && m2->type == 2) o->exc_msg = m2->sbuf;
            }
            *ret_slots = 0;
            return 1;
        }
        if (jstreq(name, "getMessage") || jstreq(name, "getLocalizedMessage")) {
            Obj *ns = new_string(o && o->exc_msg ? o->exc_msg : "",
                                 o && o->exc_msg ? jstrlen(o->exc_msg) : 0);
            result[0] = ns ? (ji8)obj_ref(ns) : 0;
            *ret_slots = 1;
            return 1;
        }
        if (jstreq(name, "toString")) {
            g_out_len = 0;
            out_s(o && o->exc_cls ? o->exc_cls : cn);
            if (o && o->exc_msg && o->exc_msg[0]) { out_s(": "); out_s(o->exc_msg); }
            Obj *ns = new_string(g_out, g_out_len);
            g_out_len = 0;
            result[0] = ns ? (ji8)obj_ref(ns) : 0;
            *ret_slots = 1;
            return 1;
        }
        if (jstreq(name, "printStackTrace")) {
            g_out_len = 0;
            print_uncaught(o && o->exc_cls ? o->exc_cls : cn, o ? o->exc_msg : 0);
            out_flush();
            *ret_slots = 0;
            return 1;
        }
    }

    /* ---- java/util/Arrays ---- */
    if (jstreq(cn, "java/util/Arrays")) {
        if (jstreq(name, "toString")) {
            Obj *arr = ref_obj(args[0]);
            g_out_len = 0;
            print_object(arr);
            Obj *n = new_string(g_out, g_out_len); g_out_len = 0;
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
    }

    /* ---- java/lang/Integer ---- */
    if (jstreq(cn, "java/lang/Integer")) {
        if (jstreq(name, "parseInt") || jstreq(name, "valueOf") || jstreq(name, "intValue")) {
            if (nargs >= 1 && (desc[1] == 'L')) {
                Obj *o = ref_obj(args[0]);
                const char *q = 0; int ql = 0;
                str_of(o, &q, &ql);
                ji8 v = 0; int sign = 1, i = 0;
                if (q[0] == '-') { sign = -1; i = 1; }
                for (; i < ql; ++i) { if (q[i] < '0' || q[i] > '9') break; v = v * 10 + (q[i] - '0'); }
                result[0] = v * sign; *ret_slots = 1; return 1;
            }
            result[0] = args[0]; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "toString")) {
            g_out_len = 0; out_ll((ji8)(int)args[0]);
            Obj *n = new_string(g_out, g_out_len); g_out_len = 0;
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "MAX_VALUE")) { result[0] = 2147483647; *ret_slots = 1; return 1; }
    }

    /* ---- java/lang/Long ---- */
    if (jstreq(cn, "java/lang/Long")) {
        if (jstreq(name, "parseLong") || jstreq(name, "longValue")) {
            if (nargs >= 1 && desc[1] == 'L') {
                Obj *o = ref_obj(args[0]);
                const char *q = 0; int ql = 0;
                str_of(o, &q, &ql);
                ji8 v = 0; int sign = 1, i = 0;
                if (q[0] == '-') { sign = -1; i = 1; }
                for (; i < ql; ++i) { if (q[i] < '0' || q[i] > '9') break; v = v * 10 + (q[i] - '0'); }
                result[0] = v * sign; *ret_slots = 1; return 1;
            }
            result[0] = args[0]; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "toString")) {
            g_out_len = 0; out_ll(args[0]);
            Obj *n = new_string(g_out, g_out_len); g_out_len = 0;
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
    }

    /* ---- java/lang/Math ---- */
    if (jstreq(cn, "java/lang/Math")) {
        int two = (desc[1] == 'J' || desc[1] == 'D') ? 2 : 1;
        if (jstreq(name, "abs")) { ji8 v = args[0]; result[0] = v < 0 ? -v : v; *ret_slots = two; return 1; }
        if (jstreq(name, "max")) { result[0] = args[0] > args[two] ? args[0] : args[two]; *ret_slots = two; return 1; }
        if (jstreq(name, "min")) { result[0] = args[0] < args[two] ? args[0] : args[two]; *ret_slots = two; return 1; }
        if (jstreq(name, "sqrt")) { result[0] = 0; *ret_slots = 1; return 1; }
    }

    /* ---- java/lang/Thread ---- */
    if (jstreq(cn, "java/lang/Thread")) {
        if (jstreq(name, "sleep")) { *ret_slots = 0; return 1; }
        if (jstreq(name, "currentThread")) { result[0] = 0; *ret_slots = 1; return 1; }
    }

    /* ---- WCTEOS 扩展：wct/WctRuntime ---- */
    if (jstreq(cn, "wct/WctRuntime")) {
        if (jstreq(name, "api")) {
            /* wct.WctRuntime.api("term.write", "text") */
            Obj *m = ref_obj(args[0]);
            Obj *p = ref_obj(args[1]);
            const char *ms = 0, *ps = 0; int ml = 0, pl = 0;
            str_of(m, &ms, &ml); str_of(p, &ps, &pl);
            (void)ml;
            if (H->api_call) {
                g_out_len = 0;
                H->api_call(HC, ms, ps);
                /* api_call 直接输出，不需要再刷新 */
                g_out_len = 0;
            }
            *ret_slots = 0; return 1;
        }
        if (jstreq(name, "version") || jstreq(name, "osName")) {
            Obj *n = new_string("WCTEOS", 6);
            result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "println")) {
            Obj *o = ref_obj(args[0]);
            if (o && o->type == 2) out_put(o->sbuf, o->slen);
            out_c('\n'); out_flush();
            *ret_slots = 0; return 1;
        }
    }

    /* ---- java/util/ArrayList ---- */
    if (jstreq(cn, "java/util/ArrayList")) {
        Obj *l = ref_obj(args[0]);
        if (jstreq(name, "<init>")) {
            if (l) { l->alen = 0; l->slen = 0; l->adata = 0; }
            *ret_slots = 0; return 1;
        }
        if (jstreq(name, "size")) { result[0] = l ? (ji8)l->slen : 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "isEmpty")) { result[0] = (l && l->slen == 0) ? 1 : 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "add") && jstreq(desc, "(Ljava/lang/Object;)Z")) {
            if (l && list_reserve(l, l->slen + 1)) { l->adata[l->slen++] = args[1]; result[0] = 1; }
            else result[0] = 0;
            *ret_slots = 1; return 1;
        }
        if (jstreq(name, "get")) {
            int i = (int)args[1];
            result[0] = (l && i >= 0 && i < l->slen) ? l->adata[i] : 0;
            *ret_slots = 1; return 1;
        }
        if (jstreq(name, "set")) {
            int i = (int)args[1];
            ji8 old = 0;
            if (l && i >= 0 && i < l->slen) { old = l->adata[i]; l->adata[i] = args[2]; }
            result[0] = old; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "remove") && jstreq(desc, "(I)Ljava/lang/Object;")) {
            int i = (int)args[1];
            ji8 old = 0;
            if (l && i >= 0 && i < l->slen) {
                old = l->adata[i];
                for (int k = i; k + 1 < l->slen; ++k) l->adata[k] = l->adata[k + 1];
                --l->slen;
            }
            result[0] = old; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "clear")) { if (l) l->slen = 0; *ret_slots = 0; return 1; }
        if (jstreq(name, "contains")) {
            int hit = 0;
            if (l) for (int i = 0; i < l->slen; ++i) if (veq(l->adata[i], args[1])) { hit = 1; break; }
            result[0] = hit; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "toString")) {
            Obj *sb = obj_new(2, 0);
            if (sb) { sb->sbuf = (char *)arena_alloc(1, 1); if (sb->sbuf) { sb->sbuf[0] = 0; sb->slen = 0; } }
            if (sb) {
                sb_append_str(sb, "[", 1);
                for (int i = 0; l && i < l->slen; ++i) {
                    if (i) sb_append_str(sb, ", ", 2);
                    Obj *e = ref_obj(l->adata[i]);
                    if (!e) sb_append_str(sb, "null", 4);
                    else {
                        Obj *so = ref_obj(obj_to_string(e));
                        if (so && so->type == 2) sb_append_str(sb, so->sbuf, so->slen);
                    }
                }
                sb_append_str(sb, "]", 1);
            }
            result[0] = sb ? obj_ref(sb) : 0; *ret_slots = 1; return 1;
        }
    }

    /* ---- java/util/HashMap ---- */
    if (jstreq(cn, "java/util/HashMap")) {
        Obj *m = ref_obj(args[0]);
        if (jstreq(name, "<init>")) {
            Obj *nm = map_new();
            if (m && nm) { m->alen = nm->alen; m->slen = 0; m->adata = nm->adata; }
            *ret_slots = 0; return 1;
        }
        if (jstreq(name, "size")) { result[0] = m ? (ji8)m->slen : 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "isEmpty")) { result[0] = (m && m->slen == 0) ? 1 : 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "put")) {
            ji8 old = 0;
            if (m) {
                if ((m->slen + 1) * 2 >= m->alen) map_grow(m);
                int found = 0;
                int slot = map_slot(m, args[1], &found);
                if (slot >= 0) {
                    if (found) { old = m->adata[slot * 2 + 1]; m->adata[slot * 2 + 1] = args[2]; }
                    else { m->adata[slot * 2] = args[1]; m->adata[slot * 2 + 1] = args[2]; ++m->slen; }
                }
            }
            result[0] = old; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "get")) {
            ji8 v = 0;
            if (m) { int found = 0; int slot = map_slot(m, args[1], &found); if (slot >= 0 && found) v = m->adata[slot * 2 + 1]; }
            result[0] = v; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "containsKey")) {
            int found = 0;
            if (m) { int slot = map_slot(m, args[1], &found); (void)slot; }
            result[0] = found; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "remove")) {
            ji8 old = 0;
            if (m) {
                int found = 0;
                int slot = map_slot(m, args[1], &found);
                if (slot >= 0 && found) {
                    old = m->adata[slot * 2 + 1];
                    m->adata[slot * 2] = 0;
                    m->adata[slot * 2 + 1] = 0;
                    --m->slen;
                }
            }
            result[0] = old; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "toString")) {
            Obj *sb = obj_new(2, 0);
            if (sb) { sb->sbuf = (char *)arena_alloc(1, 1); if (sb->sbuf) { sb->sbuf[0] = 0; sb->slen = 0; } }
            if (sb) {
                sb_append_str(sb, "{", 1);
                int first = 1;
                for (int i = 0; m && i < m->alen; ++i) {
                    ji8 k = m->adata[i * 2];
                    if (!k) continue;
                    if (!first) sb_append_str(sb, ", ", 2);
                    first = 0;
                    Obj *ko = ref_obj(obj_to_string(ref_obj(k)));
                    if (ko && ko->type == 2) sb_append_str(sb, ko->sbuf, ko->slen);
                    sb_append_str(sb, "=", 1);
                    Obj *vo = ref_obj(obj_to_string(ref_obj(m->adata[i * 2 + 1])));
                    if (vo && vo->type == 2) sb_append_str(sb, vo->sbuf, vo->slen);
                }
                sb_append_str(sb, "}", 1);
            }
            result[0] = sb ? obj_ref(sb) : 0; *ret_slots = 1; return 1;
        }
    }

    /* ---- java/util/Objects ---- */
    if (jstreq(cn, "java/util/Objects")) {
        if (jstreq(name, "equals")) { result[0] = veq(args[0], args[1]); *ret_slots = 1; return 1; }
        if (jstreq(name, "hashCode")) { result[0] = vhash(args[0]); *ret_slots = 1; return 1; }
        if (jstreq(name, "toString")) {
            Obj *o = ref_obj(args[0]);
            Obj *so = o ? ref_obj(obj_to_string(o)) : 0;
            if (so && so->type == 2) result[0] = obj_ref(so);
            else { Obj *n = new_string("null", 4); result[0] = n ? obj_ref(n) : 0; }
            *ret_slots = 1; return 1;
        }
        if (jstreq(name, "requireNonNull")) { result[0] = args[0]; *ret_slots = 1; return 1; }
    }

    /* ---- java/util/Arrays 扩展 ---- */
    if (jstreq(cn, "java/util/Arrays")) {
        if (jstreq(name, "sort") && jstreq(desc, "([I)V")) {
            Obj *a = ref_obj(args[0]);
            if (a && a->type == 3) {
                int *d = (int *)a->adata;
                for (int i = 1; i < a->alen; ++i) { int v = d[i], j = i - 1; while (j >= 0 && d[j] > v) { d[j + 1] = d[j]; --j; } d[j + 1] = v; }
            }
            *ret_slots = 0; return 1;
        }
        if (jstreq(name, "sort") && jstreq(desc, "([J)V")) {
            Obj *a = ref_obj(args[0]);
            if (a && a->type == 3) {
                for (int i = 1; i < a->alen; ++i) { ji8 v = a->adata[i]; int j = i - 1; while (j >= 0 && a->adata[j] > v) { a->adata[j + 1] = a->adata[j]; --j; } a->adata[j + 1] = v; }
            }
            *ret_slots = 0; return 1;
        }
        if (jstreq(name, "binarySearch") && jstreq(desc, "([II)I")) {
            Obj *a = ref_obj(args[0]);
            int key = (int)args[1], lo = 0, hi = a ? a->alen - 1 : -1, idx = -1;
            while (a && a->type == 3 && lo <= hi) {
                int mid = (lo + hi) / 2;
                int v = ((int *)a->adata)[mid];
                if (v == key) { idx = mid; break; }
                if (v < key) lo = mid + 1; else hi = mid - 1;
            }
            result[0] = (idx >= 0) ? idx : -(lo + 1); *ret_slots = 1; return 1;
        }
    }

    /* ---- java/lang/Character / Boolean ---- */
    if (jstreq(cn, "java/lang/Character")) {
        int c = (int)args[0];
        if (jstreq(name, "isDigit")) { result[0] = (c >= '0' && c <= '9'); *ret_slots = 1; return 1; }
        if (jstreq(name, "isLetter")) { result[0] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')); *ret_slots = 1; return 1; }
        if (jstreq(name, "isWhitespace")) { result[0] = (c == ' ' || c == '\t' || c == '\n' || c == '\r'); *ret_slots = 1; return 1; }
        if (jstreq(name, "toUpperCase")) { result[0] = (c >= 'a' && c <= 'z') ? c - 32 : c; *ret_slots = 1; return 1; }
        if (jstreq(name, "toLowerCase")) { result[0] = (c >= 'A' && c <= 'Z') ? c + 32 : c; *ret_slots = 1; return 1; }
        if (jstreq(name, "toString")) { g_out_len = 0; out_c((char)c); Obj *n = new_string(g_out, g_out_len); g_out_len = 0; result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1; }
    }
    if (jstreq(cn, "java/lang/Boolean")) {
        if (jstreq(name, "toString")) { const char *v = args[0] ? "true" : "false"; Obj *n = new_string(v, jstrlen(v)); result[0] = n ? obj_ref(n) : 0; *ret_slots = 1; return 1; }
        if (jstreq(name, "valueOf")) { result[0] = args[0] ? 1 : 0; *ret_slots = 1; return 1; }
    }


    /* ---- String：split / toCharArray / valueOf(char[]) ---- */
    if (jstreq(cn, "java/lang/String")) {
        Obj *so = ref_obj(args[0]);
        const char *sp = 0; int sl = 0;
        if (so && so->type == 2) { sp = so->sbuf; sl = so->slen; }
        if (jstreq(name, "split") && jstreq(desc, "(Ljava/lang/String;)[Ljava/lang/String;")) {
            Obj *sep = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(sep, &q, &ql);
            int count = 1;
            if (ql > 0) for (int i = 0; i + ql <= sl; ) { if (sp[i] == q[0]) { int ok = 1; for (int k = 1; k < ql; ++k) if (sp[i + k] != q[k]) { ok = 0; break; } if (ok) { ++count; i += ql; continue; } } ++i; }
            Obj *arr = new_array(0, count, 0);
            int out = 0, start = 0;
            if (arr) {
                if (ql > 0) {
                    for (int i = 0; i + ql <= sl; ) {
                        int ok = 1;
                        for (int k = 0; k < ql; ++k) if (sp[i + k] != q[k]) { ok = 0; break; }
                        if (ok) {
                            Obj *piece = new_string(sp + start, i - start);
                            arr->adata[out++] = piece ? (ji8)obj_ref(piece) : 0;
                            i += ql; start = i; continue;
                        }
                        ++i;
                    }
                }
                Obj *piece = new_string(sp + start, sl - start);
                arr->adata[out++] = piece ? (ji8)obj_ref(piece) : 0;
            }
            result[0] = arr ? (ji8)obj_ref(arr) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "toCharArray")) {
            Obj *arr = new_array(2, sl, 0);
            if (arr) for (int i = 0; i < sl; ++i) ((short *)arr->adata)[i] = (short)(unsigned char)sp[i];
            result[0] = arr ? (ji8)obj_ref(arr) : 0; *ret_slots = 1; return 1;
        }
        if (jstreq(name, "valueOf") && jstreq(desc, "([C)Ljava/lang/String;")) {
            Obj *arr = ref_obj(args[0]);
            Obj *n = 0;
            if (arr && arr->type == 3) {
                int n2 = arr->alen;
                char tmp[512]; int tn = 0;
                for (int i = 0; i < n2 && tn < 511; ++i) tmp[tn++] = (char)(((short *)arr->adata)[i]);
                n = new_string(tmp, tn);
            }
            result[0] = n ? (ji8)obj_ref(n) : 0; *ret_slots = 1; return 1;
        }
    }

    /* ---- StringBuilder 扩展 ---- */
    if (jstreq(cn, "java/lang/StringBuilder") || jstreq(cn, "java/lang/StringBuffer")) {
        Obj *sb = ref_obj(args[0]);
        if (sb && jstreq(name, "insert") && jstreq(desc, "(ILjava/lang/String;)Ljava/lang/StringBuilder;")) {
            int at = (int)args[1];
            Obj *vo = ref_obj(args[2]);
            const char *q = 0; int ql = 0;
            str_of(vo, &q, &ql);
            if (at < 0) at = 0;
            if (at > sb->slen) at = sb->slen;
            int nl = sb->slen + ql;
            char *nb = (char *)arena_alloc(nl + 1, 1);
            if (nb) {
                int k = 0;
                for (int i = 0; i < at; ++i) nb[k++] = sb->sbuf[i];
                for (int i = 0; i < ql; ++i) nb[k++] = q[i];
                for (int i = at; i < sb->slen; ++i) nb[k++] = sb->sbuf[i];
                nb[k] = 0;
                sb->sbuf = nb; sb->slen = nl;
            }
            result[0] = (ji8)obj_ref(sb); *ret_slots = 1; return 1;
        }
        if (sb && jstreq(name, "deleteCharAt")) {
            int at = (int)args[1];
            if (at >= 0 && at < sb->slen) {
                for (int i = at; i + 1 < sb->slen; ++i) sb->sbuf[i] = sb->sbuf[i + 1];
                sb->sbuf[--sb->slen] = 0;
            }
            result[0] = (ji8)obj_ref(sb); *ret_slots = 1; return 1;
        }
        if (sb && jstreq(name, "delete")) {
            int b = (int)args[1], e = (int)args[2];
            if (b < 0) b = 0; if (e > sb->slen) e = sb->slen; if (e < b) e = b;
            int k = b;
            for (int i = e; i < sb->slen; ++i) sb->sbuf[k++] = sb->sbuf[i];
            sb->slen = k; sb->sbuf[k] = 0;
            result[0] = (ji8)obj_ref(sb); *ret_slots = 1; return 1;
        }
        if (sb && jstreq(name, "setLength")) {
            int n2 = (int)args[1];
            if (n2 < 0) n2 = 0;
            if (n2 > sb->slen) {
                char *nb = (char *)arena_alloc(n2 + 1, 1);
                if (nb) { for (int i = 0; i < sb->slen; ++i) nb[i] = sb->sbuf[i]; for (int i = sb->slen; i < n2; ++i) nb[i] = 0; nb[n2] = 0; sb->sbuf = nb; sb->slen = n2; }
            } else { sb->slen = n2; sb->sbuf[n2] = 0; }
            *ret_slots = 0; return 1;
        }
        if (sb && jstreq(name, "indexOf") && jstreq(desc, "(Ljava/lang/String;)I")) {
            Obj *vo = ref_obj(args[1]);
            const char *q = 0; int ql = 0;
            str_of(vo, &q, &ql);
            int found = -1;
            for (int i = 0; i + ql <= sb->slen && found < 0; ++i) {
                int ok = 1;
                for (int k = 0; k < ql; ++k) if (sb->sbuf[i + k] != q[k]) { ok = 0; break; }
                if (ok) found = i;
            }
            result[0] = found; *ret_slots = 1; return 1;
        }
    }


    (void)args; (void)nargs;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 执行一个方法                                                        */
/* ------------------------------------------------------------------ */
static int is_exception_class(const char *cn) {
    static const char *names[] = {
        "java/lang/Throwable", "java/lang/Exception", "java/lang/RuntimeException",
        "java/lang/ArithmeticException", "java/lang/NullPointerException",
        "java/lang/IndexOutOfBoundsException", "java/lang/ArrayIndexOutOfBoundsException",
        "java/lang/IllegalArgumentException", "java/lang/IllegalStateException",
        "java/lang/NumberFormatException", "java/lang/UnsupportedOperationException",
        0
    };
    for (int i = 0; names[i]; ++i) if (jstreq(cn, names[i])) return 1;
    return 0;
}

/* 触发 Java 异常：命中异常表就跳转 handler，否则返回 0 */
static int raise_exception(const char *cls, const char *msg, KClass *c, Method *m,
                           int at, int *new_pc, ji8 *exc_ref) {
    Obj *e = obj_new(5, 0);
    if (!e) return 0;
    e->exc_cls = cls;
    e->exc_msg = msg;
    *exc_ref = (ji8)obj_ref(e);
    for (int i = 0; i < m->nex; ++i) {
        if (at >= (int)m->ex[i].start && at < (int)m->ex[i].end) {
            g_sp = 0;
            push(*exc_ref);
            *new_pc = (int)m->ex[i].handler;
            return 1;
        }
    }
    (void)c;
    return 0;
}

static void print_uncaught(const char *cls, const char *msg) {
    g_out_len = 0;
    out_s("Exception in thread \"main\" ");
    out_s(cls);
    if (msg && msg[0]) { out_s(": "); out_s(msg); }
    out_c('\n');
    out_flush();
}

static int g_depth;

static int exec_method(KClass *c, Method *m, ji8 *args, int nargs, ji8 *result) {
    if (!m) return -1;
    if (!m->code) {
        int rs = 0;
        ji8 r = 0;
        if (call_native(c->name, m->name, m->desc, args, nargs, &r, &rs)) {
            if (result) *result = r;
            return 0;
        }
        return -2;
    }
    if (g_depth >= MAX_DEPTH) { fail(-22, "wctj: stack overflow (deep recursion)\n"); return -1; }

    ji8 locals[MAX_SLOTS];
    ji8 stack[MAX_SLOTS];
    for (int zi = 0; zi < MAX_SLOTS; ++zi) { locals[zi] = 0; stack[zi] = 0; }

    for (int i = 0; i < nargs && i < MAX_SLOTS; ++i) locals[i] = args[i];
#ifdef WCTJ_TRACE
    { char t[80]; int k=0; const char *pre="ARGS n="; while(*pre) t[k++]=*pre++; t[k++]=(char)('0'+nargs); t[k++]=' '; t[k++]='a'; t[k++]='0'; t[k++]='='; 
      ji8 v = nargs>0?args[0]:0; char digits[24]; int dn=0; if(!v) digits[dn++]='0'; else { ji8 uv=v<0?-v:v; while(uv&&dn<20){digits[dn++]=(char)('0'+uv%10); uv/=10;} } if(v<0)t[k++]='-';
      while(dn) t[k++]=digits[--dn]; t[k++]=' '; t[k++]='l'; t[k++]='0'; t[k++]='=';
      ji8 w = locals[0]; dn=0; if(!w) digits[dn++]='0'; else { ji8 uv=w<0?-w:w; while(uv&&dn<20){digits[dn++]=(char)('0'+uv%10); uv/=10;} } if(w<0)t[k++]='-'; while(dn) t[k++]=digits[--dn];
      t[k++]='\n'; t[k]=0; H->print(HC,t); }
#endif
    ji8 *sv_l = g_locals, *sv_s = g_stack;
    int sv_sp = g_sp;
    g_locals = locals;
    g_stack = stack;
    g_sp = 0;
    ++g_depth;

    const ju1 *code = m->code;
    int clen = m->code_len;
    int pc = 0;
    int rc = 0;
    ji8 retval = 0;

#define LSLOT(i) (locals[(i) < MAX_SLOTS ? (i) : (MAX_SLOTS - 1)])
#define ST_SLOT(i) (stack[(i) < MAX_SLOTS ? (i) : (MAX_SLOTS - 1)])

    while (pc < clen) {
        ju1 op = code[pc++];
#ifdef WCTJ_TRACE
        if (jstreq(m->name, "main")) {
            char t[64]; int k = 0; const char *pre = "OP ";
            while (*pre) t[k++] = *pre++;
            int v = pc - 1; char d[16]; int dn = 0;
            if (!v) d[dn++] = '0'; else while (v && dn < 12) { d[dn++] = (char)('0' + v % 10); v /= 10; }
            while (dn) t[k++] = d[--dn];
            t[k++] = ' '; t[k++] = 'o'; t[k++] = 'p'; t[k++] = '=';
            int hi = (op >> 4) & 15, lo = op & 15;
            t[k++] = (char)(hi < 10 ? '0' + hi : 'A' + hi - 10);
            t[k++] = (char)(lo < 10 ? '0' + lo : 'A' + lo - 10);
            t[k++] = ' '; t[k++] = 's'; t[k++] = 'p'; t[k++] = '=';
            v = g_sp; dn = 0; if (!v) d[dn++] = '0'; else while (v && dn < 12) { d[dn++] = (char)('0' + v % 10); v /= 10; }
            while (dn) t[k++] = d[--dn];
            t[k++] = '\n'; t[k] = 0;
            H->print(HC, t);
        }
#endif
        switch (op) {
        case 0x00: break;                                   /* nop */
        case 0x01: push(0); break;                           /* aconst_null */
        case 0x02: case 0x03: case 0x04: case 0x05:
        case 0x06: case 0x07: case 0x08: push(op - 0x03); break;  /* iconst_m1..5 */
        case 0x09: case 0x0A: push((ji8)op - 0x09); break;   /* lconst_0/1 */
        case 0x0B: case 0x0C: case 0x0D: push(0); push(0); break; /* fconst_0..2 */
        case 0x0E: case 0x0F: push(0); push(0); break;       /* dconst_0/1 */
        case 0x10: { push((ji8)(signed char)code[pc]); ++pc; break; } /* bipush */
        case 0x11: push((ji8)(short)rd2(code + pc)); pc += 2; break; /* sipush */
        case 0x12: {                                          /* ldc */
            ju1 idx = code[pc++];
            Cp *k = &c->cp[idx];
            if (k->tag == 3) push((ji8)(int)k->v);
            else if (k->tag == 8) {
                const char *s = cp_utf8(c, k->a);
                Obj *o = new_string(s, jstrlen(s));
                push(o ? (ji8)obj_ref(o) : 0);
            } else if (k->tag == 7) {
                const char *s = cp_class_name(c, idx);
                Obj *o = new_string(s ? s : "?", jstrlen(s ? s : "?"));
                push(o ? (ji8)obj_ref(o) : 0);
            } else if (k->tag == 4) push((ji8)(int)k->v);
            else push(0);
            break;
        }
        case 0x13: {                                          /* ldc_w */
            ju2 idx = rd2(code + pc); pc += 2;
            Cp *k = &c->cp[idx];
            if (k->tag == 3) push((ji8)(int)k->v);
            else if (k->tag == 8) {
                const char *s = cp_utf8(c, k->a);
                Obj *o = new_string(s, jstrlen(s));
                push(o ? (ji8)obj_ref(o) : 0);
            } else if (k->tag == 7) {
                const char *s = cp_class_name(c, idx);
                Obj *o = new_string(s ? s : "?", jstrlen(s ? s : "?"));
                push(o ? (ji8)obj_ref(o) : 0);
            } else if (k->tag == 4) push((ji8)(int)k->v);
            else push(0);
            break;
        }
        case 0x14: {                                          /* ldc2_w */
            ju2 idx = rd2(code + pc); pc += 2;
            Cp *k = &c->cp[idx];
            if (k->tag == 5) push((ji8)((ju8)k->v << 32 | (ju8)k->v2));
            else if (k->tag == 6) push((ji8)((ju8)k->v << 32 | (ju8)k->v2));
            else push(0);
            push(0);
            break;
        }
        case 0x15: { int ii = code[pc++]; push(LSLOT(ii)); break; } /* iload */
        case 0x16: { int ii = code[pc++]; ji8 v = LSLOT(ii); push(v); push(0); break; } /* lload */
        case 0x17: { int ii = code[pc++]; push(LSLOT(ii)); push(0); break; } /* fload */
        case 0x18: { int ii = code[pc++]; push(LSLOT(ii)); push(0); break; } /* dload */
        case 0x19: { int ii = code[pc++]; push(LSLOT(ii)); break; } /* aload */
        case 0x1A: case 0x1B: case 0x1C: case 0x1D: push(LSLOT(op - 0x1A)); break;
        case 0x1E: case 0x1F: case 0x20: case 0x21: { ji8 v = LSLOT(op - 0x1E); push(v); push(0); break; }
        case 0x22: case 0x23: case 0x24: case 0x25: push(LSLOT(op - 0x22)); push(0); break;
        case 0x26: case 0x27: case 0x28: case 0x29: push(LSLOT(op - 0x26)); push(0); break;
        case 0x2A: case 0x2B: case 0x2C: case 0x2D: push(LSLOT(op - 0x2A)); break;
        case 0x2E: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? (ji8)((int *)a->adata)[i] : 0); break; } /* iaload */
        case 0x2F: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? a->adata[i] : 0); push(0); break; } /* laload */
        case 0x30: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? a->adata[i] : 0); push(0); break; } /* faload */
        case 0x31: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? a->adata[i] : 0); push(0); break; } /* daload */
        case 0x32: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? a->adata[i] : 0); break; } /* aaload */
        case 0x33: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? (ji8)((signed char *)a->adata)[i] : 0); break; } /* baload */
        case 0x34: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? (ji8)((unsigned short *)a->adata)[i] : 0); break; } /* caload */
        case 0x35: { int i = (int)pop(); Obj *a = ref_obj(pop()); push(a && i >= 0 && i < a->alen ? (ji8)((short *)a->adata)[i] : 0); break; } /* saload */
        case 0x36: { int ii = code[pc++]; LSLOT(ii) = pop(); break; } /* istore */
        case 0x37: { int ii = code[pc++]; pop(); LSLOT(ii) = pop(); break; } /* lstore */
        case 0x38: { int ii = code[pc++]; pop(); LSLOT(ii) = pop(); break; }
        case 0x39: { int ii = code[pc++]; pop(); LSLOT(ii) = pop(); break; }
        case 0x3A: { int ii = code[pc++]; LSLOT(ii) = pop(); break; } /* astore */
        case 0x3B: case 0x3C: case 0x3D: case 0x3E: LSLOT(op - 0x3B) = pop(); break;
        case 0x3F: case 0x40: case 0x41: case 0x42: { pop(); LSLOT(op - 0x3F) = pop(); break; }
        case 0x43: case 0x44: case 0x45: case 0x46: { pop(); LSLOT(op - 0x43) = pop(); break; }
        case 0x47: case 0x48: case 0x49: case 0x4A: { pop(); LSLOT(op - 0x47) = pop(); break; }
        case 0x4B: case 0x4C: case 0x4D: case 0x4E: LSLOT(op - 0x4B) = pop(); break;
        case 0x4F: { ji8 v = pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) ((int *)a->adata)[i] = (int)v; break; }  /* iastore */
        case 0x50: { pop(); ji8 v = pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) a->adata[i] = v; break; }  /* lastore */
        case 0x51: { pop(); pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) ((int *)a->adata)[i] = 0; break; } /* fastore */
        case 0x52: { pop(); pop(); pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) a->adata[i] = 0; break; } /* dastore */
        case 0x53: { ji8 v = pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) a->adata[i] = v; break; }  /* aastore */
        case 0x54: { int v = (int)pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) ((signed char *)a->adata)[i] = (signed char)v; break; } /* bastore */
        case 0x55: { int v = (int)pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) ((unsigned short *)a->adata)[i] = (unsigned short)v; break; } /* castore */
        case 0x56: { int v = (int)pop(); int i = (int)pop(); Obj *a = ref_obj(pop()); if (a && a->type == 3 && i >= 0 && i < a->alen) ((short *)a->adata)[i] = (short)v; break; } /* sastore */
        case 0x57: --g_sp; break;                             /* pop */
        case 0x58: g_sp -= 2; if (g_sp < 0) g_sp = 0; break;   /* pop2 */
        case 0x59: { ji8 v = stack[g_sp - 1]; push(v); break; } /* dup */
        case 0x5A: { ji8 v1 = stack[g_sp - 1], v2 = stack[g_sp - 2]; push(v1); stack[g_sp - 2] = v1; stack[g_sp - 1] = v2; break; } /* dup_x1 */
        case 0x5B: { /* dup_x2 */
            ji8 v1 = stack[g_sp - 1], v2 = stack[g_sp - 2], v3 = stack[g_sp - 3];
            push(v1); stack[g_sp - 2] = v1; stack[g_sp - 3] = v2; stack[g_sp - 4] = v3; break;
        }
        case 0x5C: { ji8 a = stack[g_sp - 1], b = stack[g_sp - 2]; push(b); push(a); break; } /* dup2 */
        case 0x5D: { /* dup2_x1 */
            ji8 a = stack[g_sp - 1], b = stack[g_sp - 2], c = stack[g_sp - 3];
            push(b); push(a); stack[g_sp - 3] = b; stack[g_sp - 4] = a; stack[g_sp - 5] = c; break;
        }
        case 0x5E: { /* dup2_x2 */
            ji8 a = stack[g_sp - 1], b = stack[g_sp - 2], c = stack[g_sp - 3], d = stack[g_sp - 4];
            push(b); push(a); stack[g_sp - 3] = b; stack[g_sp - 4] = a; stack[g_sp - 5] = c; stack[g_sp - 6] = d; break;
        }
        case 0x5F: { /* swap */
            ji8 a = stack[g_sp - 1], b = stack[g_sp - 2];
            stack[g_sp - 1] = b; stack[g_sp - 2] = a; break;
        }
        case 0x60: { int b = (int)pop(), a = (int)pop(); push((ji8)(a + b)); break; }        /* iadd */
        case 0x61: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(a + b); push(0); break; } /* ladd */
        case 0x62: { pop(); pop(); push(0); break; }                                          /* fadd */
        case 0x63: { pop(); pop(); pop(); pop(); push(0); push(0); break; }                    /* dadd */
        case 0x64: { int b = (int)pop(), a = (int)pop(); push((ji8)(a - b)); break; }          /* isub */
        case 0x65: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(a - b); push(0); break; } /* lsub */
        case 0x66: { pop(); pop(); push(0); break; }                                           /* fsub */
        case 0x67: { pop(); pop(); pop(); pop(); push(0); push(0); break; }                    /* dsub */
        case 0x68: { int b = (int)pop(), a = (int)pop(); push((ji8)(a * b)); break; }          /* imul */
        case 0x69: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(a * b); push(0); break; } /* lmul */
        case 0x6A: { pop(); pop(); push(0); break; }                                           /* fmul */
        case 0x6B: { pop(); pop(); pop(); pop(); push(0); push(0); break; }                    /* dmul */
        case 0x6C: { int b = (int)pop(), a = (int)pop();
            if (!b) { ji8 er = 0; int np = 0; if (raise_exception("java/lang/ArithmeticException", "/ by zero", c, m, pc - 1, &np, &er)) { pc = np; break; } print_uncaught("java/lang/ArithmeticException", "/ by zero"); rc = -3; goto done; }
            push((ji8)(a / b)); break; }   /* idiv */
        case 0x6D: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(b ? a / b : 0); push(0); break; } /* ldiv */
        case 0x6E: { pop(); pop(); push(0); break; }                                           /* fdiv */
        case 0x6F: { pop(); pop(); pop(); pop(); push(0); push(0); break; }                    /* ddiv */
        case 0x70: { int b = (int)pop(), a = (int)pop();
            if (!b) { ji8 er = 0; int np = 0; if (raise_exception("java/lang/ArithmeticException", "/ by zero", c, m, pc - 1, &np, &er)) { pc = np; break; } print_uncaught("java/lang/ArithmeticException", "/ by zero"); rc = -3; goto done; }
            push((ji8)(a % b)); break; }   /* irem */
        case 0x71: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(b ? a % b : 0); push(0); break; } /* lrem */
        case 0x72: { pop(); pop(); push(0); break; }                                           /* frem */
        case 0x73: { pop(); pop(); pop(); pop(); push(0); push(0); break; }                    /* drem */
        case 0x74: { int v = (int)pop(); push((ji8)(-v)); break; }                              /* ineg */
        case 0x75: { pop(); ji8 v = pop(); push(-v); push(0); break; }                           /* lneg */
        case 0x76: { pop(); push(0); break; }                                                    /* fneg */
        case 0x77: { pop(); pop(); push(0); push(0); break; }                                    /* dneg */
        case 0x78: { int b = (int)pop(), a = (int)pop(); push((ji8)(a << (b & 31))); break; }    /* ishl */
        case 0x79: { int b = (int)pop(); pop(); ji8 a = pop(); push(a << (b & 63)); push(0); break; } /* lshl */
        case 0x7A: { int b = (int)pop(), a = (int)pop(); push((ji8)(a >> (b & 31))); break; }    /* ishr */
        case 0x7B: { int b = (int)pop(); pop(); ji8 a = pop(); push(a >> (b & 63)); push(0); break; } /* lshr */
        case 0x7C: { int b = (int)pop(), a = (int)pop(); push((ji8)((unsigned)a >> (b & 31))); break; } /* iushr */
        case 0x7D: { int b = (int)pop(); pop(); ji8 a = pop(); push((ji8)((ju8)a >> (b & 63))); push(0); break; } /* lushr */
        case 0x7E: { int b = (int)pop(), a = (int)pop(); push((ji8)(a & b)); break; }            /* iand */
        case 0x7F: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(a & b); push(0); break; }  /* land */
        case 0x80: { int b = (int)pop(), a = (int)pop(); push((ji8)(a | b)); break; }            /* ior */
        case 0x81: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(a | b); push(0); break; }  /* lor */
        case 0x82: { int b = (int)pop(), a = (int)pop(); push((ji8)(a ^ b)); break; }            /* ixor */
        case 0x83: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(a ^ b); push(0); break; }  /* lxor */
        case 0x84: { ju1 idx = code[pc++]; signed char d = (signed char)code[pc++]; LSLOT(idx) = (ji8)((int)LSLOT(idx) + d); break; } /* iinc */
        case 0x85: { int v = (int)pop(); push(v); push(0); break; }                               /* i2l */
        case 0x86: { int v = (int)pop(); push(v); break; }                                        /* i2f */
        case 0x87: { int v = (int)pop(); push(v); push(0); break; }                               /* i2d */
        case 0x88: { pop(); ji8 v = pop(); push((ji8)(int)v); break; }                             /* l2i */
        case 0x89: { pop(); ji8 v = pop(); push(v); break; }                                       /* l2f */
        case 0x8A: { pop(); ji8 v = pop(); push(v); push(0); break; }                              /* l2d */
        case 0x8B: { int v = (int)pop(); push(v); break; }                                         /* f2i */
        case 0x8C: { int v = (int)pop(); push(v); push(0); break; }                                /* f2l */
        case 0x8D: { int v = (int)pop(); push(v); push(0); break; }                                /* f2d */
        case 0x8E: { pop(); ji8 v = pop(); push((ji8)(int)v); break; }                              /* d2i */
        case 0x8F: { pop(); ji8 v = pop(); push(v); push(0); break; }                               /* d2l */
        case 0x90: { pop(); ji8 v = pop(); push(v); break; }                                        /* d2f */
        case 0x91: { int v = (int)pop(); push((ji8)(signed char)v); break; }                        /* i2b */
        case 0x92: { int v = (int)pop(); push((ji8)(unsigned short)v); break; }                     /* i2c */
        case 0x93: { int v = (int)pop(); push((ji8)(short)v); break; }                              /* i2s */
        case 0x94: { pop(); ji8 b = pop(); pop(); ji8 a = pop(); push(a < b ? -1 : (a > b ? 1 : 0)); break; } /* lcmp */
        case 0x95: case 0x96: { pop(); pop(); push(0); break; }                                      /* fcmp */
        case 0x97: case 0x98: { pop(); pop(); pop(); pop(); push(0); break; }                        /* dcmp */
        case 0x99: { int v = (int)pop(); { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } break; } /* ifeq */
        case 0x9A: { int v = (int)pop(); if (v == 0) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0x9B: { int v = (int)pop(); if (v >= 0) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0x9C: { int v = (int)pop(); if (v < 0) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0x9D: { int v = (int)pop(); if (v > 0) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0x9E: { int v = (int)pop(); if (v <= 0) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0x9F: { int b = (int)pop(), a = (int)pop(); if (a == b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA0: { int b = (int)pop(), a = (int)pop(); if (a != b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA1: { int b = (int)pop(), a = (int)pop(); if (a < b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA2: { int b = (int)pop(), a = (int)pop(); if (a >= b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA3: { int b = (int)pop(), a = (int)pop(); if (a > b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA4: { int b = (int)pop(), a = (int)pop(); if (a <= b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA5: { ji8 b = pop(); ji8 a = pop(); if (a == b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA6: { ji8 b = pop(); ji8 a = pop(); if (a != b) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; }
        case 0xA7: { { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } break; }  /* goto */
        case 0xA8: { { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } break; }  /* jsr（近似） */
        case 0xA9: { ju1 idx = code[pc++]; pc = (int)LSLOT(idx); break; } /* ret（近似） */
        case 0xAA: { /* tableswitch */
            int base = pc - 1;
            int pad = (4 - ((base + 1) % 4)) % 4;
            int q = pc + pad;
            ji8 def = (ji8)(int)rd4(code + q); q += 4;
            int low = (int)rd4(code + q); q += 4;
            int high = (int)rd4(code + q); q += 4;
            int key = (int)pop();
            if (key < low || key > high) pc = base + (int)def;
            else { int off = (int)rd4(code + q + (key - low) * 4); pc = base + off; }
            break;
        }
        case 0xAB: { /* lookupswitch */
            int base = pc - 1;
            int pad = (4 - ((base + 1) % 4)) % 4;
            int q = pc + pad;
            ji8 def = (ji8)(int)rd4(code + q); q += 4;
            int npairs = (int)rd4(code + q); q += 4;
            int key = (int)pop();
            int target = (int)def;
            int found = 0;
            for (int i = 0; i < npairs; ++i) {
                int k = (int)rd4(code + q + i * 8);
                int off = (int)rd4(code + q + i * 8 + 4);
                if (k == key) { target = off; found = 1; break; }
            }
            (void)found;
            pc = base + target;
            break;
        }
        case 0xAC: { retval = (ji8)(int)pop(); goto done; }
        case 0xAD: { pop(); retval = pop(); goto done; }
        case 0xAE: { pop(); pop(); retval = 0; goto done; }
        case 0xAF: { pop(); pop(); retval = 0; goto done; }
        case 0xB0: { retval = pop(); goto done; }             /* areturn */
        case 0xB1: { retval = 0; goto done; }                 /* return */
        case 0xB2: { /* getstatic */
            ju2 idx = rd2(code + pc); pc += 2;
            Cp *k = &c->cp[idx];
            const char *cn = cp_class_name(c, k->a);
            const char *fname = 0, *fdesc = 0;
            dcmp(c, k->b, &fname, &fdesc);
            if (jstreq(cn, "java/lang/System") && jstreq(fname, "out")) {
                Obj *o = obj_new(4, 0); push(o ? (ji8)obj_ref(o) : 0); break;
            }
            if (jstreq(cn, "java/lang/System") && jstreq(fname, "err")) {
                Obj *o = obj_new(4, 0); push(o ? (ji8)obj_ref(o) : 0); break;
            }
            KClass *fc = find_class(cn);
            if (!fc && !jstrneq_prefix(cn, "java/", 5) && !jstrneq_prefix(cn, "wct/", 3)) fc = load_class(cn);
            if (fc) {
                for (int i = 0; i < fc->nfields; ++i) {
                    Field *f = &fc->fields[i];
                    if (f->is_static && jstreq(f->name, fname) && jstreq(f->desc, fdesc)) {
                        push(fc->stat_vals[f->slot]);
                        break;
                    }
                }
            } else push(0);
            break;
        }
        case 0xB3: { /* putstatic */
            ju2 idx = rd2(code + pc); pc += 2;
            Cp *k = &c->cp[idx];
            const char *cn = cp_class_name(c, k->a);
            const char *fname = 0, *fdesc = 0;
            dcmp(c, k->b, &fname, &fdesc);
            ji8 v = pop();
            KClass *fc = find_class(cn);
            if (!fc && !jstrneq_prefix(cn, "java/", 5) && !jstrneq_prefix(cn, "wct/", 3)) fc = load_class(cn);
            if (fc) for (int i = 0; i < fc->nfields; ++i) {
                Field *f = &fc->fields[i];
                if (f->is_static && jstreq(f->name, fname) && jstreq(f->desc, fdesc)) { fc->stat_vals[f->slot] = v; break; }
            }
            break;
        }
        case 0xB4: { /* getfield */
            ju2 idx = rd2(code + pc); pc += 2;
            Cp *k = &c->cp[idx];
            const char *cn = cp_class_name(c, k->a);
            const char *fname = 0, *fdesc = 0;
            dcmp(c, k->b, &fname, &fdesc);
            Obj *o = ref_obj(pop());
            ji8 v = 0;
            if (o && o->slots) {
                KClass *oc = cls_of(o);
                int found = 0;
                for (int depth = 0; oc && depth < 8 && !found; ++depth) {
                    for (int i = 0; i < oc->nfields; ++i) {
                        Field *f = &oc->fields[i];
                        if (!f->is_static && jstreq(f->name, fname) && jstreq(f->desc, fdesc)) {
                            v = o->slots[f->slot]; found = 1; break;
                        }
                    }
                    oc = oc->super ? find_class(oc->super) : 0;
                }
            }
            (void)cn;
            push(v);
            if (fdesc && (fdesc[0] == 'J' || fdesc[0] == 'D')) push(0);
            break;
        }
        case 0xB5: { /* putfield */
            ju2 idx = rd2(code + pc); pc += 2;
            Cp *k = &c->cp[idx];
            const char *fname = 0, *fdesc = 0;
            dcmp(c, k->b, &fname, &fdesc);
            if (fdesc && (fdesc[0] == 'J' || fdesc[0] == 'D')) pop();
            ji8 v = pop();
            Obj *o = ref_obj(pop());
            if (o && o->slots) {
                KClass *oc = cls_of(o);
                int found = 0;
                for (int depth = 0; oc && depth < 8 && !found; ++depth) {
                    for (int i = 0; i < oc->nfields; ++i) {
                        Field *f = &oc->fields[i];
                        if (!f->is_static && jstreq(f->name, fname) && jstreq(f->desc, fdesc)) {
                            o->slots[f->slot] = v; found = 1; break;
                        }
                    }
                    oc = oc->super ? find_class(oc->super) : 0;
                }
            }
            break;
        }
        case 0xB6: case 0xB7: case 0xB9: { /* invokevirtual/special/interface */
            ju2 idx = rd2(code + pc); pc += 2;
            if (op == 0xB9) pc += 2;   /* interface: count + 0 */
            Cp *k = &c->cp[idx];
            const char *cn = cp_class_name(c, k->a);
            const char *mname = 0, *mdesc = 0;
            dcmp(c, k->b, &mname, &mdesc);
            int nargs = desc_arg_slots(mdesc);
            ji8 *a = (ji8 *)arena_alloc((nargs + 1) * (int)sizeof(ji8), 8);
            if (!a) { rc = -1; goto done; }
            for (int i = nargs - 1; i >= 0; --i) a[i] = pop();
            ji8 r = 0;
            int rslots = 0;
            /* 原生优先 */
            KClass *decl = find_class(cn);
            if (!decl && !jstrneq_prefix(cn, "java/", 5) && !jstrneq_prefix(cn, "wct/", 3))
                decl = load_class(cn);
            if (call_native(cn, mname, mdesc, a, nargs, &r, &rslots)) {
                if (rslots >= 1) push(r);
                if (rslots >= 2) push(0);
                break;
            }
            /* 虚方法：优先按接收者真实类型分派（支持 invokeinterface 与未加载的声明类） */
            KClass *owner = decl;
            Method *m2 = 0;
            if (op != 0xB7 && a[0] != 0) {
                Obj *recv = ref_obj(a[0]);
                KClass *rc_cls = cls_of(recv);
                if (rc_cls) m2 = resolve_virtual(rc_cls, mname, mdesc, &owner);
            }
            if (!m2 && decl) m2 = resolve_virtual(decl, mname, mdesc, &owner);
            if (!m2 && decl) m2 = find_method(decl, mname, mdesc);
            if (!m2) {
                /* toString/hashCode/equals 兜底 */
                if (jstreq(mname, "toString")) {
                    Obj *n = 0;
                    g_out_len = 0; print_object(ref_obj(a[0]));
                    n = new_string(g_out, g_out_len); g_out_len = 0;
                    push(n ? (ji8)obj_ref(n) : 0);
                    break;
                }
                if (jstreq(mname, "hashCode")) { push(a[0]); break; }
                if (jstreq(mname, "equals")) { push(a[0] == a[1] ? 1 : 0); break; }
                if (jstreq(mname, "<init>")) { break; }
                char msg[200];
                int kk = 0;
                const char *pre = "wctj: NoSuchMethod ";
                while (*pre && kk < 150) msg[kk++] = *pre++;
                for (int i = 0; cn && cn[i] && kk < 180; ++i) msg[kk++] = cn[i];
                msg[kk++] = '.'; 
                for (int i = 0; mname && mname[i] && kk < 190; ++i) msg[kk++] = mname[i];
                msg[kk++] = '\n'; msg[kk] = 0;
                fail(-23, msg);
                rc = -1; goto done;
            }
            rc = exec_method(owner, m2, a, nargs, &r);
            if (rc < 0) goto done;
            char rt = desc_ret_type(mdesc);
            if (rt != 'V') {
                push(r);
                if (rt == 'J' || rt == 'D') push(0);
            }
            break;
        }
        case 0xB8: { /* invokestatic */
            ju2 idx = rd2(code + pc); pc += 2;
            Cp *k = &c->cp[idx];
            const char *cn = cp_class_name(c, k->a);
            const char *mname = 0, *mdesc = 0;
            dcmp(c, k->b, &mname, &mdesc);
            int nargs = desc_arg_slots(mdesc) - 1;
            if (nargs < 0) nargs = 0;
            ji8 *a = (ji8 *)arena_alloc((nargs + 1) * (int)sizeof(ji8), 8);
            if (!a) { rc = -1; goto done; }
            for (int i = nargs - 1; i >= 0; --i) a[i] = pop();
            ji8 r = 0;
            int rslots = 0;
            KClass *decl = find_class(cn);
            tr("STATIC", cn, mname, nargs);
            if (!decl && !jstrneq_prefix(cn, "java/", 5) && !jstrneq_prefix(cn, "wct/", 3)) decl = load_class(cn);
            if (call_native(cn, mname, mdesc, a, nargs, &r, &rslots)) {
                if (rslots >= 1) push(r);
                if (rslots >= 2) push(0);
                break;
            }
            if (!decl) { rc = -1; goto done; }
            Method *m2 = find_method(decl, mname, mdesc);
            if (!m2) m2 = resolve_virtual(decl, mname, mdesc, 0);
            if (!m2) {
                if (jstreq(mname, "toString")) { push(0); break; }
                fail(-23, "wctj: NoSuchMethod (static)\n");
                rc = -1; goto done;
            }
            tr("CALL", mname, mdesc, nargs);
            rc = exec_method(decl, m2, a, nargs, &r);
            tr("RET", mname, mdesc, (int)r);
            if (rc < 0) goto done;
            char rt = desc_ret_type(mdesc);
            if (rt != 'V') { push(r); if (rt == 'J' || rt == 'D') push(0); }
            break;
        }
        case 0xBA: { /* invokedynamic */
            ju2 idx = rd2(code + pc); pc += 2;
            pc += 2;   /* 两个保留字节 */
            int handled = 0;
            ji8 indy_r = 0;
            if (!do_indy(c, idx, &indy_r, &handled) || !handled) {
                fail(-24, "wctj: invokedynamic unsupported (use javac -XDstringConcat=inline)\n");
                rc = -1; goto done;
            }
            push(indy_r);
            break;
        }
        case 0xBB: { /* new */
            ju2 idx = rd2(code + pc); pc += 2;
            const char *cn = cp_class_name(c, idx);
            KClass *k = find_class(cn);
            if (!k && !jstrneq_prefix(cn, "java/", 5) && !jstrneq_prefix(cn, "wct/", 3)) k = load_class(cn);
            if (!k && !jstrneq_prefix(cn, "java/", 5) && !jstrneq_prefix(cn, "wct/", 3)) { rc = -1; goto done; }
            Obj *o = obj_new(1, k);
            if (o && is_exception_class(cn)) { o->type = 5; o->exc_cls = cn; }
            if (o && !k && (jstreq(cn, "java/lang/StringBuilder") || jstreq(cn, "java/lang/StringBuffer"))) {
                o->type = 2; o->sbuf = (char *)arena_alloc(1, 1);
                if (o->sbuf) o->sbuf[0] = 0;
            }
            push(o ? (ji8)obj_ref(o) : 0);
            break;
        }
        case 0xBC: { /* newarray */
            ju1 atype = code[pc++];
            int len = (int)pop();
            int elem = 4;
            if (atype == 4) elem = 1;      /* boolean */
            else if (atype == 5) elem = 2; /* char */
            else if (atype == 6) elem = 4; /* float */
            else if (atype == 7) elem = 8; /* double */
            else if (atype == 8) elem = 1; /* byte */
            else if (atype == 9) elem = 2; /* short */
            else if (atype == 10) elem = 4;/* int */
            else if (atype == 11) elem = 8;/* long */
            Obj *o = new_array(elem, len, 0);
            push(o ? (ji8)obj_ref(o) : 0);
            break;
        }
        case 0xBD: { /* anewarray */
            ju2 idx = rd2(code + pc); pc += 2;
            int len = (int)pop();
            Obj *o = new_array(0, len, (int)idx);
            push(o ? (ji8)obj_ref(o) : 0);
            break;
        }
        case 0xBE: { /* arraylength */
            Obj *a = ref_obj(pop());
            push(a ? a->alen : 0);
            break;
        }
        case 0xBF: { /* athrow */
            ji8 exc = pop();
            Obj *e = ref_obj(exc);
            int handled = 0;
            for (int i = 0; i < m->nex; ++i) {
                if (pc - 1 >= (int)m->ex[i].start && pc - 1 < (int)m->ex[i].end) {
                    pc = (int)m->ex[i].handler;
                    g_sp = 0;
                    push(exc);
                    handled = 1;
                    break;
                }
            }
            if (!handled) {
                const char *cls = e ? (e->exc_cls ? e->exc_cls : obj_class_name(e)) : "java/lang/NullPointerException";
                const char *msg = e ? e->exc_msg : 0;
                if (e && !msg && e->slots) {
                    KClass *ec = cls_of(e);
                    for (int i = 0; ec && i < ec->nfields; ++i) {
                        if (jstreq(ec->fields[i].name, "detailMessage")) {
                            Obj *d = ref_obj(e->slots[ec->fields[i].slot]);
                            if (d) msg = d->sbuf;
                            break;
                        }
                    }
                }
                print_uncaught(cls, msg);
                rc = -3;
                goto done;
            }
            break;
        }
        case 0xC0: { pc += 2; break; }                        /* checkcast */
        case 0xC1: { ji8 v = pop(); push(v ? 1 : 0); break; } /* instanceof */
        case 0xC2: case 0xC3: break;                          /* monitorenter/exit */
        case 0xC4: { /* wide */
            ju1 wop = code[pc++];
            ju2 idx = rd2(code + pc); pc += 2;
            if (wop == 0x84) { short d = (short)rd2(code + pc); pc += 2; LSLOT(idx) = (ji8)((int)LSLOT(idx) + d); }
            else if (wop == 0x15) push(LSLOT(idx));
            else if (wop == 0x16) { push(LSLOT(idx)); push(0); }
            else if (wop == 0x19) push(LSLOT(idx));
            else if (wop == 0x36) LSLOT(idx) = pop();
            else if (wop == 0x37) { pop(); LSLOT(idx) = pop(); }
            else if (wop == 0x3A) LSLOT(idx) = pop();
            break;
        }
        case 0xC5: { pc += 3; /* multianewarray：退化为空数组 */ 
            ju1 dims = code[pc - 1];
            for (ju1 d = 0; d < dims; ++d) pop();
            Obj *o = new_array(0, 0, 0);
            push(o ? (ji8)obj_ref(o) : 0);
            break;
        }
        case 0xC6: { ji8 v = pop(); if (v == 0) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; } /* ifnull */
        case 0xC7: { ji8 v = pop(); if (v != 0) { int bo = (int)(short)rd2(code + pc); pc = (pc - 1) + bo; } else pc += 2; break; } /* ifnonnull */
        case 0xC8: { { int bo = (int)rd4(code + pc); pc = (pc - 1) + bo; } break; }        /* goto_w */
        default:
            {
                char msg[96];
                int k = 0;
                const char *pre = "wctj: unsupported opcode ";
                while (*pre) msg[k++] = *pre++;
                msg[k++] = '0'; msg[k++] = 'x';
                int hi = (op >> 4) & 15, lo = op & 15;
                msg[k++] = (char)(hi < 10 ? '0' + hi : 'A' + hi - 10);
                msg[k++] = (char)(lo < 10 ? '0' + lo : 'A' + lo - 10);
                msg[k++] = '\n'; msg[k] = 0;
                fail(-25, msg);
                rc = -1;
                goto done;
            }
        }
        continue;
    }
done:
    --g_depth;
    g_locals = sv_l;
    g_stack = sv_s;
    g_sp = sv_sp;
    if (result) *result = retval;
    return rc;
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */
static ju8 g_clock;

int jvm_run(const JvmHost *host, void *ctx, const char *class_path,
            int argc, char **argv) {
    g_err = 0;
    g_err_text = "";
    H = host;
    HC = ctx;
    nclasses = 0;
    nobjs = 0;
    g_depth = 0;
    g_clock = 0;

    arena_cap = MAX_ARENA;
    arena_used = 0;
    arena_base = (char *)H->alloc(HC, arena_cap);
    if (!arena_base) { fail(-20, "wctj: cannot allocate heap\n"); return -20; }

    g_out_cap = 4096;
    g_out = (char *)arena_alloc(g_out_cap, 8);
    g_out_len = 0;
    if (!g_out) { fail(-20, "wctj: cannot allocate output buffer\n"); return -20; }

    /* 路径分隔符：主类路径里出现哪个就用哪个 */
    for (const char *q = class_path; *q; ++q) if (*q == '/') { g_sep = '/'; break; }

    /* 目录：class_path 去掉最后一段 */
    char dir[200];
    int n = 0;
    int last = -1;
    for (const char *p = class_path; *p && n < 190; ++p) {
        dir[n++] = *p;
        if (*p == '\\' || *p == '/') last = n - 1;
    }
    dir[n] = 0;
    if (last >= 0) dir[last] = 0; else dir[0] = 0;
    if (dir[0]) {
        char *d = arena_alloc(last + 1, 1);
        if (d) { for (int i = 0; i < last; ++i) d[i] = dir[i]; d[last] = 0; cls_dir = d; }
    }

    /* 主类名：从路径推出 */
    const char *base = class_path;
    for (const char *p = class_path; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
    char mainname[128];
    int mn = 0;
    for (int i = 0; base[i] && mn < 120; ++i) {
        if (base[i] == '.' && jstrneq_prefix(base + i, ".class", 6)) break;
        mainname[mn++] = base[i];
    }
    mainname[mn] = 0;

    KClass *c = load_class(mainname);
    if (!c) return g_err ? g_err : -1;

    /* <clinit> */
    Method *cli = find_method(c, "<clinit>", "()V");
    if (cli) { ji8 r = 0; if (exec_method(c, cli, 0, 0, &r) < 0) return g_err ? g_err : -1; }

    Method *mainm = find_method(c, "main", "([Ljava/lang/String;)V");
    if (!mainm) {
        /* 也接受 main() */
        mainm = find_method(c, "main", "()V");
    }
    if (!mainm) { fail(-30, "wctj: main() not found\n"); return -30; }

    ji8 args[2];
    if (jstreq(mainm->desc, "([Ljava/lang/String;)V")) {
        Obj *arr = new_array(0, argc, 0);
        if (arr) for (int i = 0; i < argc; ++i) {
            Obj *s = new_string(argv[i], jstrlen(argv[i]));
            arr->adata[i] = s ? (ji8)obj_ref(s) : 0;
        }
        args[0] = arr ? (ji8)obj_ref(arr) : 0;
        args[1] = 0;
    } else {
        args[0] = 0;
    }
    ji8 r = 0;
    int rc = exec_method(c, mainm, args, 1, &r);
    out_flush();
    if (rc < 0) return g_err ? g_err : rc;
    return 0;
}
