#ifndef WCT_ICONS_H
#define WCT_ICONS_H

static void ico_folder(int x, int y) { 
    gfx_fill(x + 4, y + 8, 34, 24, gfx_rgb(214,178,96)); 
    gfx_fill(x + 4, y + 4, 16, 8, gfx_rgb(230,196,118)); 
    gfx_border(x + 4, y + 4, 34, 28, gfx_rgb(140,110,50)); 
    gfx_fill(x + 6, y + 14, 30, 2, gfx_rgb(180,146,74)); 
}

static void ico_term(int x, int y) { 
    gfx_fill(x + 4, y + 4, 34, 30, gfx_rgb(28,30,36)); 
    gfx_border(x + 4, y + 4, 34, 30, gfx_rgb(90,94,104)); 
    gfx_fill(x + 9, y + 12, 3, 4, gfx_rgb(210,214,222)); 
    gfx_fill(x + 12, y + 15, 5, 3, gfx_rgb(210,214,222)); 
    gfx_fill(x + 9, y + 22, 12, 2, gfx_rgb(150,156,168)); 
}

static void ico_edit(int x, int y) { 
    gfx_fill(x + 7, y + 3, 26, 32, gfx_rgb(246,244,238)); 
    gfx_border(x + 7, y + 3, 26, 32, gfx_rgb(150,146,136)); 
    for (int i = 0; i < 5; ++i) gfx_fill(x + 11, y + 8 + i * 5, 18, 2, gfx_rgb(120,120,120)); 
}

static void ico_set(int x, int y) { 
    int xs[3] = { 6, 10, 14 }; 
    for (int i = 0; i < 3; ++i) { 
        int yy = y + 10 + i * 8; 
        gfx_fill(x + 6, yy, 30, 2, gfx_rgb(140,136,126)); 
        gfx_fill(x + xs[i], yy - 2, 6, 6, gfx_accent); 
        gfx_border(x + xs[i], yy - 2, 6, 6, gfx_rgb(40,60,100)); 
    } 
}

static void ico_task(int x, int y) { 
    gfx_fill(x + 5, y + 4, 32, 30, gfx_rgb(40,44,52)); 
    gfx_fill(x + 9, y + 20, 5, 10, gfx_rgb(120,200,140)); 
    gfx_fill(x + 16, y + 14, 5, 16, gfx_rgb(230,190,90)); 
    gfx_fill(x + 23, y + 9, 5, 21, gfx_rgb(120,160,230)); 
}

static void ico_info(int x, int y) { 
    gfx_fill(x + 8, y + 4, 26, 30, gfx_rgb(226,222,212)); 
    gfx_border(x + 8, y + 4, 26, 30, gfx_accent); 
    gfx_fill(x + 20, y + 9, 3, 8, gfx_accent); 
    gfx_fill(x + 19, y + 10, 5, 3, gfx_accent); 
    gfx_fill(x + 20, y + 20, 3, 9, gfx_accent); 
}
static void ico_calc(int x, int y) {
    // 灰色计算器外壳
    gfx_fill(x + 6, y + 2, 30, 34, gfx_rgb(180, 180, 180));
    gfx_border(x + 6, y + 2, 30, 34, gfx_rgb(80, 80, 80));
    // 绿色液晶屏
    gfx_fill(x + 10, y + 6, 22, 8, gfx_rgb(120, 200, 120));
    // 按键布局 3x3
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            gfx_fill(x + 10 + c * 7, y + 18 + r * 5, 5, 4, gfx_rgb(50, 50, 50));
        }
    }
}
#endif // WCT_ICONS_H