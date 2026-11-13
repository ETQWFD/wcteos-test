#ifndef WCT_DRAG_H
#define WCT_DRAG_H

/* 依赖底层：gfx_fb, gfx_stride, gfx_w, gfx_h, gfx_present_rect, gfx_cursor, cursor_restore_bg, ST->BootServices */

/* 幽灵框状态 */
static int df_active = 0;
static int df_x, df_y, df_w, df_h;
static UINT32 *df_top_bg, *df_bottom_bg, *df_left_bg, *df_right_bg;

static void dframe_pixel(int x, int y, UINT32 c) { 
    if (x < 0 || y < 0 || (UINTN)x >= gfx_w || (UINTN)y >= gfx_h) return; 
    gfx_fb[(UINTN)y * gfx_stride + (UINTN)x] = c; 
}

static void dframe_save_bg(void) { 
    int x = df_x, y = df_y, w = df_w, h = df_h; 
    int x1 = x + w - 1, y1 = y + h - 1; 
    for (int i = 0; i < w; ++i) { 
        int px = x + i; 
        df_top_bg[i] = (px >= 0 && y >= 0 && px < (int)gfx_w && y < (int)gfx_h) ? gfx_fb[(UINTN)y * gfx_stride + px] : 0; 
        df_bottom_bg[i] = (px >= 0 && y1 >= 0 && px < (int)gfx_w && y1 < (int)gfx_h) ? gfx_fb[(UINTN)y1 * gfx_stride + px] : 0; 
    } 
    for (int i = 0; i < h; ++i) { 
        int py = y + i; 
        df_left_bg[i] = (x >= 0 && py >= 0 && x < (int)gfx_w && py < (int)gfx_h) ? gfx_fb[(UINTN)py * gfx_stride + x] : 0; 
        df_right_bg[i] = (x1 >= 0 && py >= 0 && x1 < (int)gfx_w && py < (int)gfx_h) ? gfx_fb[(UINTN)py * gfx_stride + x1] : 0; 
    } 
}

static void dframe_restore_bg(void) { 
    int x = df_x, y = df_y, w = df_w, h = df_h; 
    int x1 = x + w - 1, y1 = y + h - 1; 
    for (int i = 0; i < w; ++i) { 
        int px = x + i; 
        if (px >= 0 && y >= 0 && px < (int)gfx_w && y < (int)gfx_h) gfx_fb[(UINTN)y * gfx_stride + px] = df_top_bg[i]; 
        if (px >= 0 && y1 >= 0 && px < (int)gfx_w && y1 < (int)gfx_h) gfx_fb[(UINTN)y1 * gfx_stride + px] = df_bottom_bg[i]; 
    } 
    for (int i = 0; i < h; ++i) { 
        int py = y + i; 
        if (x >= 0 && py >= 0 && x < (int)gfx_w && py < (int)gfx_h) gfx_fb[(UINTN)py * gfx_stride + x] = df_left_bg[i]; 
        if (x1 >= 0 && py >= 0 && x1 < (int)gfx_w && py < (int)gfx_h) gfx_fb[(UINTN)py * gfx_stride + x1] = df_right_bg[i]; 
    } 
}

static void dframe_draw_border(void) { 
    UINT32 c = gfx_rgb(255, 255, 255); 
    int x = df_x, y = df_y, w = df_w, h = df_h; 
    int x1 = x + w - 1, y1 = y + h - 1; 
    for (int i = 0; i < w; ++i) { 
        dframe_pixel(x + i, y, c); 
        dframe_pixel(x + i, y1, c); 
    } 
    for (int i = 0; i < h; ++i) { 
        dframe_pixel(x, y + i, c); 
        dframe_pixel(x1, y + i, c); 
    } 
}

static void dframe_present(int x, int y) { 
    int w = df_w, h = df_h; 
    gfx_present_rect(x, y, w, 1); 
    gfx_present_rect(x, y + h - 1, w, 1); 
    gfx_present_rect(x, y, 1, h); 
    gfx_present_rect(x + w - 1, y, 1, h); 
}

static void dframe_end(void) { 
    if (df_top_bg) { ST->BootServices->FreePool(df_top_bg); df_top_bg = NULL; } 
    if (df_bottom_bg) { ST->BootServices->FreePool(df_bottom_bg); df_bottom_bg = NULL; } 
    if (df_left_bg) { ST->BootServices->FreePool(df_left_bg); df_left_bg = NULL; } 
    if (df_right_bg) { ST->BootServices->FreePool(df_right_bg); df_right_bg = NULL; } 
    df_active = 0; 
}

static int dframe_begin(int x, int y, int w, int h) {
    dframe_end(); 
    df_w = w; df_h = h;
    if (EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, (UINTN)w * 4, (VOID **)&df_top_bg)) || !df_top_bg) { dframe_end(); return 0; }
    if (EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, (UINTN)w * 4, (VOID **)&df_bottom_bg)) || !df_bottom_bg) { dframe_end(); return 0; }
    if (EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, (UINTN)h * 4, (VOID **)&df_left_bg)) || !df_left_bg) { dframe_end(); return 0; }
    if (EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, (UINTN)h * 4, (VOID **)&df_right_bg)) || !df_right_bg) { dframe_end(); return 0; }
    df_x = x; df_y = y; dframe_save_bg(); dframe_draw_border(); df_active = 1; dframe_present(df_x, df_y); return 1;
}

static void dframe_move(int nx, int ny, int old_cx, int old_cy) { 
    int ox = df_x, oy = df_y; 
    cursor_restore_bg(); 
    dframe_restore_bg(); 
    df_x = nx; df_y = ny; 
    dframe_save_bg(); 
    dframe_draw_border(); 
    gfx_cursor(); 
    dframe_present(ox, oy); 
    if (ox != nx || oy != ny) dframe_present(nx, ny); 
    gfx_present_rect(old_cx, old_cy, 10, 16); 
    gfx_present_rect(mouse_x, mouse_y, 10, 16); 
}

#endif // WCT_DRAG_H