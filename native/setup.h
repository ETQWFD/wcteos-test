#ifndef CVER_SETUP_H
#define CVER_SETUP_H

static int in_setup = 0;          
static int setup_lang_sel = 0;    
static int setup_theme_sel = 1;   

// 🚀 新增：保存配置逻辑
static void setup_save(void) {
    char data[64];
    int n = 0;
    scopy(data, sizeof(data), "lang=");
    n = 5;
    data[n++] = (char)('0' + setup_lang_sel);
    data[n++] = '\n';
    scopy(data + n, sizeof(data) - n, "theme=");
    n += 6;
    data[n++] = (char)('0' + setup_theme_sel);
    data[n++] = '\n';
    data[n] = 0;
    
    write_bytes(L"\\setting.txt", data, n);
    
    ui_lang = setup_lang_sel;
    theme_index = setup_theme_sel;
    apply_theme();
    in_setup = 0; 
}

static void setup_draw(void) {
    int W = (int)gfx_w, H = (int)gfx_h;
    gfx_fill(0, 0, W, H, gfx_rgb(12, 15, 22));
    int pw = 520, ph = 360;
    int px = (W - pw) / 2, py = (H - ph) / 2;
    
    gfx_fill(px, py, pw, ph, gfx_panel);
    gfx_border(px, py, pw, ph, gfx_accent);
    
    gfx_text_w(px + 160, py + 30, L"CVEROS 初始配置向导", gfx_accent);
    gfx_text_w(px + 130, py + 55, L"Welcome to CVEROS Setup Wizard", gfx_dim);
    
    gfx_text_w(px + 40, py + 110, L"语言 / Language:", gfx_text_c);
    ui_btn(px + 180, py + 100, 100, 32, L"中文", setup_lang_sel == 0);
    ui_btn(px + 290, py + 100, 100, 32, L"English", setup_lang_sel == 1);
    
    gfx_text_w(px + 40, py + 180, L"主题 / Theme:", gfx_text_c);
    const CHAR16 *t_names[6] = { L"紫", L"绿", L"矩阵", L"琥珀", L"蓝", L"纸片" };
    UINT32 t_colors[6] = { 
        gfx_rgb(205,125,255), gfx_rgb(55,220,115), gfx_rgb(0,255,0), 
        gfx_rgb(255,190,60), gfx_rgb(100,210,255), gfx_rgb(52,86,138) 
    };
    
    for (int i = 0; i < 6; ++i) {
        int bx = px + 180 + (i % 3) * 95;
        int by = py + 165 + (i / 3) * 50;
        int active = (setup_theme_sel == i);
        int hot = ui_hit(bx, by, 85, 40);
        
        gfx_fill(bx, by, 85, 40, active ? gfx_accent : (hot ? gfx_top : gfx_panel));
        gfx_border(bx, by, 85, 40, gfx_accent);
        gfx_fill(bx + 5, by + 5, 16, 16, t_colors[i]);
        gfx_text_w(bx + 26, by + 10, t_names[i], active ? gfx_rgb(255,255,255) : gfx_text_c);
    }
    
    ui_btn(px + 210, py + ph - 60, 100, 36, L"完成 / Enter", 1);
}

static void setup_click(void) {
    int W = (int)gfx_w, H = (int)gfx_h;
    int pw = 520, ph = 360;
    int px = (W - pw) / 2, py = (H - ph) / 2;
    int lx = mouse_x - px;
    int ly = mouse_y - py;
    
    if (ly < 0 || ly >= ph || lx < 0 || lx >= pw) return;
    
    if (ly >= 100 && ly < 132) {
        if (lx >= 180 && lx < 280) setup_lang_sel = 0;
        if (lx >= 290 && lx < 390) setup_lang_sel = 1;
    }
    
    if (ly >= 165 && ly < 255) {
        int c = (lx - 180) / 95;
        int r = (ly - 165) / 50;
        if (c >= 0 && c < 3 && r >= 0 && r < 2) {
            setup_theme_sel = r * 3 + c;
        }
    }
    
    if (ly >= ph - 60 && ly < ph - 24 && lx >= 210 && lx < 310) {
        setup_save(); // 🚀 调用抽离出的保存逻辑
    }
}
#endif // CVER_SETUP_H