/* wctpack —— 把 .wct 的可执行段封装成 WCTX 容器（XOR 混淆 + 校验）
 * 用法: wctpack in.wct out.wct [key]
 * 说明: 这是提高反编译门槛的混淆，不是加密；密钥随文件保存。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned crc32_(const unsigned char *p, size_t n)
{
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    return c ^ 0xFFFFFFFFu;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s in.wct out.wct [hexkey]\n", argv[0]); return 2; }
    unsigned key = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0) : 0xA5C3E71Bu;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    size_t cap = 1 << 20, n = 0;
    unsigned char *b = malloc(cap);
    while (!feof(f)) {
        if (n == cap) { cap *= 2; b = realloc(b, cap); }
        n += fread(b + n, 1, cap - n, f);
    }
    fclose(f);

    if (n < 24 || memcmp(b, "WCT1", 4) != 0) {
        fprintf(stderr, "input is not a WCT1 image\n");
        return 1;
    }
    unsigned char *out = malloc(n + 12);
    memcpy(out, "WCTX", 4);
    out[4] = (unsigned char)key; out[5] = (unsigned char)(key >> 8);
    out[6] = (unsigned char)(key >> 16); out[7] = (unsigned char)(key >> 24);
    out[8] = (unsigned char)n; out[9] = (unsigned char)(n >> 8);
    out[10] = (unsigned char)(n >> 16); out[11] = (unsigned char)(n >> 24);
    for (size_t i = 0; i < n; ++i) out[12 + i] = (unsigned char)(b[i] ^ (unsigned char)(key >> ((i & 3) * 8)));

    FILE *g = fopen(argv[2], "wb");
    fwrite(out, 1, n + 12, g); fclose(g);
    printf("packed %s (%zu bytes) -> %s (%zu bytes), key=0x%08X, crc=0x%08X\n",
           argv[1], n, argv[2], n + 12, key, crc32_(b, n));
    return 0;
}
