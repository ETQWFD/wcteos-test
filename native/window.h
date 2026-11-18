#ifndef CVER_WINDOW_H
#define CVER_WINDOW_H

/* 依赖外部定义的 g_apps 和 APP_COUNT */
extern const AppDef* g_apps[];
extern const int APP_COUNT;
extern int ui_lang;

static void draw_window_frame(GuiWin *w) {
    // 修复：正确使用指针偏移来计算窗口索引，判断是否拥有焦点
    UINT32 border = ((w - wins) == focus_win) ? gfx_accent : gfx_dim;
    const CHAR16 *title = L"CVEROS";
    
    for (int i = 0; i < APP_COUNT; ++i) {
        if (g_apps[i]->type_id == w->type) {
            title = ui_lang ? g_apps[i]->title_en : g_apps[i]->title_zh;
            break;
        }
    }
    
    gfx_fill(w->x, w->y, w->w, w->h, gfx_panel); 
    gfx_border(w->x, w->y, w->w, w->h, border);
    gfx_fill(w->x + 2, w->y + 2, w->w - 4, 28, gfx_top); 
    gfx_text_w(w->x + 10, w->y + 8, title, border);
    
    gfx_fill(w->x + w->w - 34, w->y + 6, 24, 20, gfx_panel); 
    gfx_border(w->x + w->w - 34, w->y + 6, 24, 20, border); 
    gfx_text(w->x + w->w - 26, w->y + 9, "X", gfx_text_c);
    
    if (w->on_draw) w->on_draw(w);
}

#endif