#ifndef WCT_GFX_H
#define WCT_GFX_H

#include "font8x16.h"
#include "wct_zh.h"

/* ---------- 图形全局状态 ---------- */
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx;
static UINT32 *gfx_fb, *gfx_hw, *gfx_back;
static UINTN gfx_w, gfx_h, gfx_stride;

/* ---------- 底层绘图原语 ---------- */
static UINT32 gfx_rgb(UINT8 r, UINT8 g, UINT8 b) { 
    UINT32 fmt = gfx->Mode->Info->PixelFormat; 
    if (fmt == 0) return ((UINT32)b << 16) | ((UINT32)g << 8) | r; 
    return ((UINT32)r << 16) | ((UINT32)g << 8) | b; 
}

static void gfx_pixel(int x, int y, UINT32 c) { 
    if (x < 0 || y < 0 || (UINTN)x >= gfx_w || (UINTN)y >= gfx_h) return; 
    gfx_fb[(UINTN)y * gfx_stride + (UINTN)x] = c; 
}

static void gfx_fill(int x, int y, int w, int h, UINT32 c) {
    if (x < 0) { w += x; x = 0; } 
    if (y < 0) { h += y; y = 0; } 
    if (x + w > (int)gfx_w) w = (int)gfx_w - x; 
    if (y + h > (int)gfx_h) h = (int)gfx_h - y; 
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) { 
        UINT32 *p = gfx_fb + (UINTN)(y + yy) * gfx_stride + x; 
        for (int xx = 0; xx < w; ++xx) p[xx] = c; 
    }
}

static void gfx_border(int x, int y, int w, int h, UINT32 c) { 
    gfx_fill(x, y, w, 2, c); gfx_fill(x, y + h - 2, w, 2, c); 
    gfx_fill(x, y, 2, h, c); gfx_fill(x + w - 2, y, 2, h, c); 
}

static void gfx_char(int x, int y, unsigned char ch, UINT32 c) { 
    if (ch < 32 || ch > 126) ch = '?'; 
    for (int r = 0; r < 16; ++r) { 
        unsigned char bits = font8x16[ch][r]; 
        for (int col = 0; col < 8; ++col) 
            if (bits & (0x80 >> col)) gfx_pixel(x + col, y + r, c); 
    } 
}

static void gfx_text(int x, int y, const char *s, UINT32 c) { 
    while (*s) gfx_char(x, y, (unsigned char)*s++, c), x += 8; 
}

static void gfx_text_n(int x, int y, const char *s, int max, UINT32 c) { 
    while (*s && max-- > 0) gfx_char(x, y, (unsigned char)*s++, c), x += 8; 
}

static void gfx_char16(int x, int y, unsigned short cp, UINT32 c) { 
    int idx = zh_lookup(cp); 
    if (idx < 0) { gfx_char(x, y, '?', c); return; } 
    for (int r = 0; r < 16; ++r) { 
        unsigned short bits = ((unsigned short)zh_bits[idx][r * 2] << 8) | zh_bits[idx][r * 2 + 1]; 
        for (int col = 0; col < 16; ++col) 
            if (bits & (0x8000 >> col)) gfx_pixel(x + col, y + r, c); 
    } 
}

static void gfx_text_w(int x, int y, const CHAR16 *s, UINT32 c) { 
    while (*s) { 
        if ((unsigned short)*s <= 126) { gfx_char(x, y, (unsigned char)*s, c); x += 8; } 
        else { gfx_char16(x, y, (unsigned short)*s, c); x += 16; } 
        ++s; 
    } 
}

static void gfx_present_rect(int x, int y, int w, int h) {
    if (!gfx_back || !gfx_hw) return; 
    if (x < 0) { w += x; x = 0; } 
    if (y < 0) { h += y; y = 0; } 
    if (x + w > (int)gfx_w) w = (int)gfx_w - x; 
    if (y + h > (int)gfx_h) h = (int)gfx_h - y; 
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) 
        memcpy(gfx_hw + (UINTN)(y + yy) * gfx_stride + x, 
               gfx_back + (UINTN)(y + yy) * gfx_stride + x, (UINTN)w * 4);
}

static void gfx_present(void) { 
    if (!gfx_back || !gfx_hw) return; 
    for (UINTN y = 0; y < gfx_h; ++y) 
        memcpy(gfx_hw + y * gfx_stride, gfx_back + y * gfx_stride, gfx_w * 4); 
}

#endif // WCT_GFX_H