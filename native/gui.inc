/*WCT-OS
Copyright (C) 2026  aqtwWCT95

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published
by the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>
*/

/* =========================================================
   1. 基础底层与依赖库引入
   ========================================================= */
#include "ps2.c" 
#include "gfx.h"
#include "i18n.h"

extern int theme_index;

/* =========================================================
   2. 核心宏定义与结构体定义 (必须放在最前面)
   ========================================================= */
#define WIN_FILES 0
#define WIN_TERM 1
#define WIN_EDIT 2
#define WIN_SET 3
#define WIN_TASK 4
#define WIN_ABOUT 5
#define WIN_RUN 6
#define WIN_CALC 100
#define WIN_MAX 10
#define WIN_MAX_LINES 100
#define WIN_VIEW_LINES 24
#define WIN_COLS 68
#define WIN_ENTRIES 32

typedef struct { CHAR16 name[64]; int dir; UINT64 size; } GuiEnt;

typedef struct GuiWin {
    int type, x, y, w, h, z, alive;
    char input[96]; int input_len;
    char text[WIN_MAX_LINES][WIN_COLS + 1]; int trow, tcol, scroll;
    GuiEnt entries[WIN_ENTRIES]; int entry_count, loaded;
    char edit[80][80]; int edit_lines, edit_row, edit_col;
    CHAR16 edit_path[256]; int edit_loaded;
    int sel, prop_show, prop_msg;

    void (*on_draw)(struct GuiWin *w);
    void (*on_click)(struct GuiWin *w, int lx, int ly);
    void (*on_key)(struct GuiWin *w, EFI_INPUT_KEY key);
} GuiWin;

/* =========================================================
   3. 全局状态变量
   ========================================================= */
static EFI_SIMPLE_POINTER_PROTOCOL *mouse;
static EFI_ABSOLUTE_POINTER_PROTOCOL *abs_ptr;
static UINTN gfx_original_mode;
static int mouse_x, mouse_y, mouse_down, mouse_was_down, mouse_click, mouse_moved, mouse_wheel;
static int mouse_rclick, mouse_rwas;
static int gfx_done;
static int invert_mouse = 0;

static UINT32 cursor_bg[16][10];
static int cursor_bg_x = -1, cursor_bg_y = -1;

static GuiWin wins[WIN_MAX];
static GuiWin *gfx_term_target;
static int focus_win = -1, z_top = 10;
static int dragging = -1, drag_dx, drag_dy;

static int ui_task_sel = -1;
static char ui_input[96]; static int ui_len;
static int ui_admin = 0;

static char gui_cap[8192]; 
static int gui_cap_len = 0;

/* =========================================================
   4. 核心前向声明
   ========================================================= */
static void term_putc(GuiWin *w, char c);
static void gedit_load(GuiWin *w);
static void win_close(int i);
static int win_open(int type);
static void draw_all(void);
static void handle_click(void);

/* =========================================================
   5. 主题与壁纸 (提供颜色变量)
   ========================================================= */
#include "theme.h"
#include "wallpaper.h"

/* =========================================================
   6. 基础 UI 组件
   ========================================================= */
static int ui_hit(int x, int y, int w, int h) { return mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h; }
static void ui_btn(int x, int y, int w, int h, const CHAR16 *label, int active) {
    int hot = ui_hit(x, y, w, h);
    gfx_fill(x, y, w, h, active ? gfx_accent : (hot ? gfx_top : gfx_panel));
    gfx_border(x, y, w, h, gfx_accent);
    gfx_text_w(x + (w - (int)wlen(label) * 8) / 2, y + (h - 16) / 2, label, active ? gfx_rgb(255,255,255) : gfx_text_c);
}

/* =========================================================
   7. 鼠标光标与轮询
   ========================================================= */
static void gfx_cursor(void) {
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 10; ++x) { int px = mouse_x + x, py = mouse_y + y; cursor_bg[y][x] = (px >= 0 && py >= 0 && px < (int)gfx_w && py < (int)gfx_h) ? gfx_fb[(UINTN)py * gfx_stride + px] : 0; }
    cursor_bg_x = mouse_x; cursor_bg_y = mouse_y;
    static const unsigned char arrow[16][2] = { {0x80,0},{0xC0,0},{0xE0,0},{0xF0,0},{0xF8,0},{0xFC,0},{0xFE,0},{0xFF,0},{0xFF,0x80},{0xF8,0},{0xD8,0},{0x8C,0},{0x0C,0},{0x06,0},{0x06,0},{0} };
    for (int pass = 0; pass < 2; ++pass) { UINT32 col = pass ? gfx_rgb(16, 16, 16) : gfx_rgb(255, 255, 255); for (int y = 0; y < 15; ++y) for (int x = 0; x < 9; ++x) if (arrow[y][x / 8] & (0x80 >> (x % 8))) { if (pass) gfx_pixel(mouse_x + x, mouse_y + y, col); else { for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) { int px = x + ox, py = y + oy; if (px >= 0 && py >= 0 && px < 10 && py < 16) gfx_pixel(mouse_x + px, mouse_y + py, col); } } } }
}
static void cursor_restore_bg(void) { if (cursor_bg_x < 0) return; for (int y = 0; y < 16; ++y) for (int x = 0; x < 10; ++x) { int px = cursor_bg_x + x, py = cursor_bg_y + y; if (px >= 0 && py >= 0 && px < (int)gfx_w && py < (int)gfx_h) gfx_fb[(UINTN)py * gfx_stride + px] = cursor_bg[y][x]; } cursor_bg_x = -1; }

static void mouse_poll(void) {
    mouse_click = 0; mouse_moved = 0; mouse_wheel = 0;
    if (mouse) { EFI_SIMPLE_POINTER_STATE st; if (!EFI_IS_ERROR(mouse->GetState(mouse, &st))) { int dx = st.RelativeMovementX, dy = st.RelativeMovementY; if (invert_mouse) { dx = -dx; dy = -dy; } if (dx || dy) { mouse_x += dx; mouse_y += dy; mouse_moved = 1; } mouse_down = st.LeftButton; int rb = st.RightButton ? 1 : 0; if (rb && !mouse_rwas) mouse_rclick = 1; mouse_rwas = rb; mouse_wheel = (int)st.RelativeMovementZ; } }
    else if (abs_ptr && abs_ptr->Mode) { EFI_ABSOLUTE_POINTER_STATE st; if (!EFI_IS_ERROR(abs_ptr->GetState(abs_ptr, &st))) { UINT64 mx = abs_ptr->Mode->AbsoluteMaxX, my = abs_ptr->Mode->AbsoluteMaxY; if (mx && my) { UINT64 cx = st.CurrentX, cy = st.CurrentY; if (invert_mouse) { cx = (mx > cx) ? mx - cx : 0; cy = (my > cy) ? my - cy : 0; } int nx = (int)((cx * (gfx_w - 1)) / mx), ny = (int)((cy * (gfx_h - 1)) / my); if (nx != mouse_x || ny != mouse_y) mouse_moved = 1; mouse_x = nx; mouse_y = ny; } mouse_down = (st.ActiveButtons & 1) != 0; { int rb = (st.ActiveButtons & 2) ? 1 : 0; if (rb && !mouse_rwas) mouse_rclick = 1; mouse_rwas = rb; } } }
    else if (ps2_mouse_ready()) { int dx = 0, dy = 0, btn = 0; if (ps2_mouse_poll(&dx, &dy, &btn)) { if (invert_mouse) { dx = -dx; dy = -dy; } if (dx || dy) { mouse_x += dx; mouse_y += dy; mouse_moved = 1; } mouse_down = (btn & 1) ? 1 : 0; int rb = (btn & 2) ? 1 : 0; if (rb && !mouse_rwas) mouse_rclick = 1; mouse_rwas = rb; } }
    if (mouse_x < 0) mouse_x = 0; if (mouse_y < 0) mouse_y = 0; if (mouse_x >= (int)gfx_w) mouse_x = (int)gfx_w - 1; if (mouse_y >= (int)gfx_h) mouse_y = (int)gfx_h - 1;
    if (mouse_down && !mouse_was_down) mouse_click = 1; mouse_was_down = mouse_down;
}

/* =========================================================
   8. 终端捕获
   ========================================================= */
static void gui_capture_put(const CHAR16 *s) {
    while (*s && gui_cap_len + 2 < (int)sizeof(gui_cap)) {
        CHAR16 c = *s++;
        if (c == '\r') continue;
        if (c == '\n') { gui_cap[gui_cap_len++] = '\n'; continue; }
        if (c == '\t') { gui_cap[gui_cap_len++] = ' '; continue; }
        if (c >= 32 && c <= 126) gui_cap[gui_cap_len++] = (char)c;
        else gui_cap[gui_cap_len++] = '?';
    }
    gui_cap[gui_cap_len] = 0;
}
static void gui_capture_flush(GuiWin *w) { for (int i = 0; i < gui_cap_len; ++i) term_putc(w, gui_cap[i]); gui_cap_len = 0; }
static void gfx_out_wide(const CHAR16 *s) {
    GuiWin *w = gfx_term_target ? gfx_term_target : (focus_win >= 0 ? &wins[focus_win] : NULL);
    if (!w || w->type != WIN_TERM) return;
    while (*s) {
        CHAR16 c = *s++;
        if (c == '\r' || c == '\n' || c == 8 || (c >= 32 && c <= 126)) term_putc(w, (char)c);
        else term_putc(w, '?');
    }
}

/* =========================================================
   9. 引入插件化应用与外围组件
   ========================================================= */
#include "app_api.h"   // 定义应用标准
#include "icons.h"     // 图标
#include "apps.h"      // 具体应用实现

// 🚀 核心应用注册表：未来加新应用，只需要修改 registry.h 即可！
#include "registry.h"  

#include "window.h"    // 窗口外壳（依赖注册表提取标题）
#include "drag.h"      // 幽灵框特效
#include "desktop.h"   // 桌面环境（壁纸、图标、任务栏）

/* =========================================================
   10. 窗口核心管理
   ========================================================= */
static int win_at(int x, int y) { 
    int best = -1, bz = -1; 
    for (int i = 0; i < WIN_MAX; ++i) 
        if (wins[i].alive && x >= wins[i].x && x < wins[i].x + wins[i].w && y >= wins[i].y && y < wins[i].y + wins[i].h) 
            if (wins[i].z > bz) { bz = wins[i].z; best = i; } 
    return best; 
}

static void win_close(int i) { 
    if (i >= 0 && i < WIN_MAX) { 
        wins[i].alive = 0; 
        if (focus_win == i) { 
            focus_win = -1; 
            for (int k = 0; k < WIN_MAX; ++k) 
                if (wins[k].alive) { focus_win = k; break; } 
        } 
    } 
}

static int win_open(int type) {
    // 如果窗口已存在，直接提到最顶层
    for (int i = 0; i < WIN_MAX; ++i) 
        if (wins[i].alive && wins[i].type == type) { focus_win = i; wins[i].z = ++z_top; return i; }
    
    // 🚀 核心改变：从注册表查找应用定义
    const AppDef *app = NULL;
    for (int i = 0; i < APP_COUNT; ++i) {
        if (g_apps[i]->type_id == type) { app = g_apps[i]; break; }
    }
    if (!app) return -1; // 未注册的应用

    // 创建并挂载窗口
    for (int i = 0; i < WIN_MAX; ++i) {
        if (wins[i].alive) continue;
        GuiWin *w = &wins[i]; 
        for (UINTN k = 0; k < sizeof(GuiWin); ++k) ((UINT8 *)w)[k] = 0;
        w->type = type; w->alive = 1;
        
        int n = 0; 
        for (int k = 0; k < WIN_MAX; ++k) if (wins[k].alive) ++n;
        w->x = 70 + n * 40; w->y = 90 + n * 30;
        
        // 🚀 自动挂载
        w->w = app->def_w; 
        w->h = app->def_h;
        w->on_draw = app->on_draw;
        w->on_click = app->on_click;
        w->on_key = app->on_key;

        if (w->x + w->w > (int)gfx_w) w->x = (int)gfx_w - w->w;
        if (w->y + w->h > (int)gfx_h - 34) w->y = (int)gfx_h - 34 - w->h;
        w->z = ++z_top; focus_win = i;
        return i;
    }
    return -1;
}

/* =========================================================
   11. 全局事件处理
   ========================================================= */
static void handle_click(void) {
    if (mouse_rclick) { 
        mouse_rclick = 0; 
        if (mouse_y >= (int)gfx_h - 34) win_open(WIN_TASK); 
        else { ui_desk_ctx = 1; ui_desk_x = mouse_x; ui_desk_y = mouse_y; } 
        return; 
    }
    
    int wi = win_at(mouse_x, mouse_y);
    if (wi >= 0) {
        GuiWin *w = &wins[wi]; 
        focus_win = wi; w->z = ++z_top;
        int lx = mouse_x - w->x, ly = mouse_y - w->y;
        
        if (lx >= w->w - 36 && ly >= 4 && ly <= 28) { win_close(wi); return; }
        if (ly < 30) { dragging = wi; drag_dx = lx; drag_dy = ly; return; }
        
        if (w->on_click) w->on_click(w, lx, ly);
        return;
    }
    
    if (ui_desk_ctx) { 
        int x = ui_desk_x, y = ui_desk_y; 
        for (int i = 0; i < 3; ++i) { 
            int iy = y + 28 + i * 26; 
            if (ui_hit(x + 2, iy, 186, 24)) { 
                ui_desk_ctx = 0; 
                if (i == 0) { draw_desktop(); gfx_present(); } 
                return; 
            } 
        } 
        ui_desk_ctx = 0; 
        return; 
    }
    
    if (ui_menu) {
        int w = 236, h = 9 * 26 + 44, x = 6, y = (int)gfx_h - 34 - h - 6; if (y < 44) y = 44;
        for (int i = 0; i < 9; ++i) { 
            int by = y + 34 + i * 26; 
            if (ui_hit(x + 4, by, w - 8, 24)) { 
                ui_menu = 0; 
                if (i == 0) win_open(WIN_FILES); 
                else if (i == 1) win_open(WIN_TERM); 
                else if (i == 2) win_open(WIN_SET); 
                else if (i == 3) win_open(WIN_TASK); 
                else if (i == 4) win_open(WIN_RUN); 
                else if (i == 5) win_open(WIN_ABOUT); 
                else if (i == 7) ST->RuntimeServices->ResetSystem(EFI_RESET_COLD, EFI_SUCCESS, 0, NULL); 
                else if (i == 8) ST->RuntimeServices->ResetSystem(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL); 
                return; 
            } 
        }
        ui_menu = 0; 
        return;
    }
    
    // 桌面图标点击（🚀 动态调用）
    int clicked_app = desktop_icon_click();
    if (clicked_app >= 0) {
        win_open(clicked_app);
        return;
    }
    
    // 底部任务栏
    if (mouse_y >= (int)gfx_h - 34) { 
        if (ui_hit(6, (int)gfx_h - 30, 64, 26)) { ui_menu = !ui_menu; return; } 
        int bx = 78; 
        for (int i = 0; i < WIN_MAX; ++i) { 
            if (!wins[i].alive) continue; 
            if (mouse_x >= bx && mouse_x < bx + 140) { focus_win = i; wins[i].z = ++z_top; return; } 
            bx += 146; 
        } 
    }
}

static void handle_key(EFI_INPUT_KEY key) { 
    if (key.ScanCode == 0x14) gfx_done = 1; 
    if (key.ScanCode == 0x15) invert_mouse = !invert_mouse; 
    if (focus_win >= 0 && wins[focus_win].alive) { 
        if (wins[focus_win].on_key) wins[focus_win].on_key(&wins[focus_win], key); 
    } 
}

static void draw_all(void) { 
    draw_desktop();  
    for (int z = 1; z <= z_top; ++z) 
        for (int i = 0; i < WIN_MAX; ++i) 
            if (wins[i].alive && wins[i].z == z) 
                draw_window_frame(&wins[i]); 
    if (ui_menu) ui_draw_menu(); 
    if (ui_desk_ctx) ui_desk_draw_menu(); 
    gfx_cursor(); 
}

/* =========================================================
   12. 主事件循环与入口
   ========================================================= */
static void gui_loop(void) {
    gfx_done = 0; focus_win = -1; z_top = 10; 
    apply_theme(); 
    mouse_x = (int)gfx_w / 2; mouse_y = (int)gfx_h / 2; 
    mouse_down = mouse_was_down = 0; cursor_bg_x = cursor_bg_y = -1;
    for (int i = 0; i < WIN_MAX; ++i) wins[i].alive = 0;
    
    win_open(WIN_TERM); 
    dframe_end(); 
    int dirty = 1, force_full = 1;
    
    while (!gfx_done) {
        int prev_mx = mouse_x, prev_my = mouse_y; 
        int prev_cx = cursor_bg_x, prev_cy = cursor_bg_y;
        
        mouse_poll(); 
        if (mouse_moved || mouse_click || mouse_wheel) dirty = 1; 
        int full = 0; int drag_handled = 0;
        
        if (mouse_click) { handle_click(); full = 1; }
        
        if (mouse_wheel && focus_win >= 0 && wins[focus_win].alive && wins[focus_win].type == WIN_TERM) { 
            wins[focus_win].scroll += mouse_wheel * 3; 
            int maxs = wins[focus_win].trow - (wins[focus_win].h - 80) / 16 + 1; 
            if (maxs < 0) maxs = 0; 
            if (wins[focus_win].scroll > maxs) wins[focus_win].scroll = maxs; 
            if (wins[focus_win].scroll < 0) wins[focus_win].scroll = 0; 
            full = 1; 
        }
        
        if (dragging >= 0 && wins[dragging].alive) {
            if (mouse_down) { 
                GuiWin *dw = &wins[dragging]; 
                int nx = mouse_x - drag_dx, ny = mouse_y - drag_dy; 
                if (nx < 0) nx = 0; if (ny < 0) ny = 0; 
                if (nx + dw->w > (int)gfx_w) nx = (int)gfx_w - dw->w; 
                if (ny + dw->h > (int)gfx_h - 34) ny = (int)gfx_h - 34 - dw->h; 
                if (!df_active) dframe_begin(dw->x, dw->y, dw->w, dw->h); 
                if (df_active) { 
                    if (nx != df_x || ny != df_y) { 
                        dframe_move(nx, ny, prev_cx, prev_cy); 
                        drag_handled = 1; dirty = 0; force_full = 0; 
                    } else if (mouse_x != prev_mx || mouse_y != prev_my) { 
                        cursor_restore_bg(); gfx_cursor(); gfx_present_rect(prev_cx, prev_cy, 10, 16); gfx_present_rect(mouse_x, mouse_y, 10, 16); 
                        drag_handled = 1; dirty = 0; 
                    } 
                } else { 
                    dw->x = nx; dw->y = ny; full = 1; 
                } 
            } else { 
                if (df_active) { 
                    int fx = df_x, fy = df_y; 
                    cursor_restore_bg(); dframe_restore_bg(); dframe_present(df_x, df_y); gfx_cursor(); gfx_present_rect(mouse_x, mouse_y, 10, 16); dframe_end(); 
                    wins[dragging].x = fx; wins[dragging].y = fy; 
                    full = 1; dirty = 1; 
                } 
                dragging = -1; 
            }
        }
        
        EFI_INPUT_KEY key; 
        if (!EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &key))) { 
            handle_key(key); dirty = 1; full = 1; 
        }
        
        if (!drag_handled && dirty) { 
            if (full || force_full) { 
                apply_theme(); 
                draw_all(); 
                gfx_present(); 
            } else if (mouse_x != prev_mx || mouse_y != prev_my) { 
                cursor_restore_bg(); gfx_cursor(); 
                gfx_present_rect(prev_cx, prev_cy, 10, 16); 
                gfx_present_rect(mouse_x, mouse_y, 10, 16); 
            } 
            dirty = 0; force_full = 0; 
        }
        ST->BootServices->Stall(1000);
    }
    dframe_end();
}

static void cmd_gui(void) {
    if (EFI_IS_ERROR(ST->BootServices->LocateProtocol(&gop_guid, NULL, (VOID **)&gfx))) return;
    gfx_w = gfx->Mode->Info->HorizontalResolution; 
    gfx_h = gfx->Mode->Info->VerticalResolution; 
    gfx_stride = gfx->Mode->Info->PixelsPerScanLine; 
    gfx_hw = (UINT32 *)(UINTN)gfx->Mode->FrameBufferBase;
    
    VOID *buf = NULL; 
    if (!EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, gfx_stride * gfx_h * 4, &buf)) && buf) gfx_back = (UINT32 *)buf;
    gfx_fb = gfx_back ? gfx_back : gfx_hw;
    
    EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL; 
    EFI_GUID spg = { 0x31878C87, 0x0B75, 0x11D5, { 0x9A, 0x4F, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } };
    if (!EFI_IS_ERROR(ST->BootServices->LocateProtocol(&spg, NULL, (VOID **)&sp))) mouse = sp; else mouse = NULL;
    
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL; 
    EFI_GUID apg = { 0x8D59D32B, 0xC655, 0x4AE9, { 0x9B, 0x15, 0xF2, 0x59, 0x04, 0x99, 0x2A, 0x43 } };
    if (!EFI_IS_ERROR(ST->BootServices->LocateProtocol(&apg, NULL, (VOID **)&ap))) abs_ptr = ap; else abs_ptr = NULL;
    
    if (!mouse && !abs_ptr) ps2_mouse_init();
    
    invert_mouse = 0; 
    gfx_original_mode = ST->ConOut->Mode->Mode; 
    gfx_active = 1; 
    ST->ConOut->EnableCursor(ST->ConOut, FALSE);
    
    int old_theme = theme_index; 
    gui_loop(); 
    
    theme_index = old_theme; apply_theme(); gfx_active = 0; dframe_end();
    if (gfx_back) { ST->BootServices->FreePool(gfx_back); gfx_back = NULL; }
    ST->ConOut->SetMode(ST->ConOut, gfx_original_mode); 
    ST->ConOut->EnableCursor(ST->ConOut, TRUE); 
    ST->ConOut->ClearScreen(ST->ConOut); 
    reset_color(); 
    print_banner();
}

static void cmd_exitgui(void) { outln(L"exitgui: not in GUI (run gui first)"); }