#ifndef WCT_APP_API_H
#define WCT_APP_API_H

typedef struct GuiWin GuiWin;

typedef struct {
    int type_id;
    const CHAR16 *title_zh;
    const CHAR16 *title_en;
    int def_w, def_h;
    
    // 🚀 新增：应用专属图标绘制函数
    void (*icon_draw)(int x, int y);
    
    void (*on_draw)(GuiWin *w);
    void (*on_click)(GuiWin *w, int lx, int ly);
    void (*on_key)(GuiWin *w, EFI_INPUT_KEY key);
} AppDef;

#endif