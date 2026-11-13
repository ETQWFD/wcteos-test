#ifndef WCT_DESKTOP_H
#define WCT_DESKTOP_H

/* 依赖外部定义的 g_apps 和 APP_COUNT */
extern const AppDef* g_apps[];
extern const int APP_COUNT;
extern int ui_lang;

static int ui_menu = 0, ui_desk_ctx = 0, ui_desk_x = 0, ui_desk_y = 0;

static void ui_draw_menu(void) {
    int w = 236, h = 9 * 26 + 44, x = 6, y = (int)gfx_h - 34 - h - 6; if (y < 44) y = 44;
    gfx_fill(x, y, w, h, gfx_panel); gfx_border(x, y, w, h, gfx_accent); gfx_fill(x + 2, y + 2, w - 4, 26, gfx_top); gfx_text_w(x + 10, y + 8, L"WCT-OS", gfx_accent);
    const CHAR16 *items[9] = { T(S_FILES), T(S_TERM), T(S_SETTINGS), T(S_TASKMGR), T(S_RUN), T(S_ABOUT), T(S_LOCK), T(S_REBOOT), T(S_SHUTDOWN) };
    for (int i = 0; i < 9; ++i) { int by = y + 34 + i * 26; if (ui_hit(x + 4, by, w - 8, 24)) gfx_fill(x + 4, by, w - 8, 24, gfx_top); gfx_text_w(x + 16, by + 4, items[i], gfx_text_c); }
}

static void ui_desk_draw_menu(void) {
    if (!ui_desk_ctx) return; int x = ui_desk_x, y = ui_desk_y, w = 190, h = 30 + 3 * 26;
    gfx_fill(x, y, w, h, gfx_panel); gfx_border(x, y, w, h, gfx_accent); gfx_fill(x + 1, y + 1, w - 2, 24, gfx_top); gfx_text_w(x + 8, y + 6, T(S_DESKTOP), gfx_accent);
    const CHAR16 *it[3] = { T(S_REFRESH_DESK), T(S_NEW_FOLDER), T(S_NEW_TXT) };
    for (int i = 0; i < 3; ++i) { int iy = y + 28 + i * 26; if (ui_hit(x + 2, iy, w - 4, 24)) gfx_fill(x + 2, iy, w - 4, 24, gfx_top); gfx_text_w(x + 10, iy + 4, it[i], gfx_text_c); }
}

// 🚀 动态渲染所有注册的应用图标
static void draw_desktop_icons(void) {
    int cols = 3; 
    for (int i = 0; i < APP_COUNT; ++i) {
        int x = 28 + (i % cols) * 88;
        int y = 34 + (i / cols) * 88;
        
        int hot = ui_hit(x, y, 64, 64);
        if (hot) gfx_fill(x, y, 64, 64, theme_index == 5 ? gfx_rgb(232,227,216) : gfx_rgb(18,26,42));
        
        // 调用应用自带的图标绘制函数
        if (g_apps[i]->icon_draw) {
            g_apps[i]->icon_draw(x + 10, y + 10);
        }
        
        // 绘制动态文字标题
        const CHAR16 *name = ui_lang ? g_apps[i]->title_en : g_apps[i]->title_zh;
        UINT32 col = hot ? gfx_accent : gfx_text_c;
        int w = 0; for (const CHAR16 *q = name; *q; ++q) w += (*q <= 126) ? 8 : 16; 
        gfx_text_w(x + (64 - w) / 2, y + 46, name, col);
    }
}

// 🚀 动态检测桌面图标是否被点击
static int desktop_icon_click(void) {
    int cols = 3;
    for (int i = 0; i < APP_COUNT; ++i) {
        int x = 28 + (i % cols) * 88;
        int y = 34 + (i / cols) * 88;
        if (ui_hit(x, y, 64, 64)) {
            return g_apps[i]->type_id;
        }
    }
    return -1;
}

static void draw_desktop(void) {
    wct_ui_bg(); gfx_fill(0, 0, (int)gfx_w, 2, gfx_accent);
    
    draw_desktop_icons();
    
    gfx_text_w((int)gfx_w - 200, 6, T(S_GUI_TAG), gfx_dim);
    
    int h = (int)gfx_h, W = (int)gfx_w; 
    gfx_fill(0, h - 34, W, 34, gfx_top); gfx_fill(0, h - 34, W, 1, gfx_rgb(200,193,180));
    gfx_fill(6, h - 30, 64, 26, gfx_panel); gfx_border(6, h - 30, 64, 26, gfx_accent); gfx_text_w(22, h - 26, T(S_START), gfx_accent);
    
    int bx = 78; 
    for (int i = 0; i < WIN_MAX; ++i) { 
        if (!wins[i].alive) continue; 
        gfx_fill(bx, h - 30, 140, 26, i == focus_win ? gfx_accent : gfx_panel); 
        const CHAR16 *title = L"WIN"; 
        for (int j = 0; j < APP_COUNT; ++j) {
            if (g_apps[j]->type_id == wins[i].type) { title = ui_lang ? g_apps[j]->title_en : g_apps[j]->title_zh; break; }
        }
        gfx_text_w(bx + 8, h - 26, title, i == focus_win ? gfx_rgb(255,255,255) : gfx_text_c); 
        bx += 146; 
    }
    
    EFI_TIME t; UINT32 cap = 0; int hh = 0, mm = 0; 
    if (!EFI_IS_ERROR(ST->RuntimeServices->GetTime(&t, (void *)&cap))) { hh = t.Hour; mm = t.Minute; }
    CHAR16 buf[8] = { (CHAR16)(L'0'+(hh/10)%10), (CHAR16)(L'0'+hh%10), L':', (CHAR16)(L'0'+(mm/10)%10), (CHAR16)(L'0'+mm%10), 0 }; 
    gfx_text_w(W - 74, h - 26, buf, gfx_text_c);
}

#endif