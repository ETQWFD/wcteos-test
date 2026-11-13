#ifndef WCT_WALLPAPER_H
#define WCT_WALLPAPER_H

static int ui_wp = 1; // 壁纸索引

static void wct_ui_bg(void) { 
    int W = (int)gfx_w, H = (int)gfx_h; 
    if (ui_wp == 1) { // 渐变
        int rows = H / 2 + 1; 
        for (int i = 0; i < rows; ++i) { 
            int t = i * 255 / (rows > 1 ? rows - 1 : 1); 
            gfx_fill(0, i * 2, W, 2, gfx_rgb((6 * (255 - t) + 16 * t) / 255, (12 * (255 - t) + 44 * t) / 255, (30 * (255 - t) + 82 * t) / 255)); 
        } 
        return; 
    } 
    if (ui_wp == 4) { // 纸纹
        gfx_fill(0, 0, W, H, gfx_bg); 
        UINT32 a = theme_index == 5 ? gfx_rgb(214,209,198) : gfx_rgb(14, 22, 38); 
        for (int y = 0; y < H; y += 3) gfx_fill(0, y, W, 1, a); 
        for (int x = 0; x < W; x += 220) gfx_fill(x, 0, 1, H, gfx_rgb(200,193,180)); 
        return; 
    } 
    if (ui_wp == 5) { // 星夜
        for (int i = 0; i < H; ++i) { 
            int t = i * 255 / (H > 1 ? H - 1 : 1); 
            gfx_fill(0, i, W, 1, gfx_rgb((4 * (255 - t) + 20 * t) / 255, (8 * (255 - t) + 30 * t) / 255, (22 * (255 - t) + 60 * t) / 255)); 
        } 
        UINT32 seed = 12345; 
        for (int n = 0; n < 260; ++n) { 
            seed = seed * 1103515245u + 12345u; 
            int px = (int)((seed >> 8) % (UINT32)W); 
            int py = (int)((seed >> 16) % (UINT32)(H - 40)); 
            gfx_fill(px, py, 2, 2, (n % 5) ? gfx_rgb(210,214,222) : gfx_rgb(255,246,200)); 
        } 
        return; 
    } 
    // 默认网格
    gfx_fill(0, 0, W, H, gfx_bg); 
    for (int y = 0; y < H; y += 24) gfx_fill(0, y, W, 1, gfx_rgb(22, 38, 60)); 
    for (int x = 0; x < W; x += 24) gfx_fill(x, 0, 1, H, gfx_rgb(22, 38, 60)); 
}

#endif