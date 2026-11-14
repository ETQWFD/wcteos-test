#include "qr_fix.h"
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
along with this program.  If not, see <https://www.gnu.org/licenses/>*/

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

// 锁屏相关状态变量
static int screen_locked = 0;
static char lock_input[32]; 
static int lock_len = 0;

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
   5. 主题与壁纸
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
#include "app_api.h"   
#include "icons.h"     
#include "apps.h"      
#include "registry.h"  
#include "window.h"    
#include "drag.h"      
#include "desktop.h"   

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
    for (int i = 0; i < WIN_MAX; ++i) 
        if (wins[i].alive && wins[i].type == type) { focus_win = i; wins[i].z = ++z_top; return i; }
    
    const AppDef *app = NULL;
    for (int i = 0; i < APP_COUNT; ++i) {
        if (g_apps[i]->type_id == type) { app = g_apps[i]; break; }
    }
    if (!app) return -1; 

    for (int i = 0; i < WIN_MAX; ++i) {
        if (wins[i].alive) continue;
        GuiWin *w = &wins[i]; 
        for (UINTN k = 0; k < sizeof(GuiWin); ++k) ((UINT8 *)w)[k] = 0;
        w->type = type; w->alive = 1;
        
        int n = 0; 
        for (int k = 0; k < WIN_MAX; ++k) if (wins[k].alive) ++n;
        w->x = 70 + n * 40; w->y = 90 + n * 30;
        
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
    
    // 修复：桌面右键菜单新建功能
    if (ui_desk_ctx) { 
        int x = ui_desk_x, y = ui_desk_y; 
        for (int i = 0; i < 3; ++i) { 
            int iy = y + 28 + i * 26; 
            if (ui_hit(x + 2, iy, 186, 24)) { 
                ui_desk_ctx = 0; 
                if (i == 0) { 
                    draw_desktop(); gfx_present(); 
                } else if (i == 1) { 
                    cmd_mkdir(L"\\NewFolder"); 
                } else if (i == 2) { 
                    cmd_new(L"\\NewFile.txt"); 
                }
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
                // 修复：增加了锁屏触发
                else if (i == 6) { 
                    screen_locked = 1; 
                    lock_len = 0; lock_input[0] = 0;
                    return; 
                }
                else if (i == 7) ST->RuntimeServices->ResetSystem(EFI_RESET_COLD, EFI_SUCCESS, 0, NULL); 
                else if (i == 8) ST->RuntimeServices->ResetSystem(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL); 
                return; 
            } 
        }
        ui_menu = 0; 
        return;
    }
    
    int clicked_app = desktop_icon_click();
    if (clicked_app >= 0) {
        win_open(clicked_app);
        return;
    }
    
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

// 🚀 引入配置向导 (必须在 ui_btn 等 UI 组件之后，gui_loop 之前)
#include "setup.h"

/* =========================================================
   12. 主事件循环与入口
   ========================================================= */
static void gui_loop(void) {
    gfx_done = 0; focus_win = -1; z_top = 10; 
    apply_theme(); 
    mouse_x = (int)gfx_w / 2; mouse_y = (int)gfx_h / 2; 
    mouse_down = mouse_was_down = 0; cursor_bg_x = cursor_bg_y = -1;
    for (int i = 0; i < WIN_MAX; ++i) wins[i].alive = 0;
    
    // 🚀 读取 setting.txt 决定是否启动向导
    static UINT8 cfg[256];
    UINTN sz = read_whole_file(L"\\setting.txt", cfg, sizeof(cfg) - 1);
    if (sz > 0) {
        cfg[sz] = 0;
        for (char *c = (char *)cfg; *c; ++c) {
            if (c[0]=='l' && c[1]=='a' && c[2]=='n' && c[3]=='g' && c[4]=='=') {
                ui_lang = c[5] - '0';
            }
            if (c[0]=='t' && c[1]=='h' && c[2]=='e' && c[3]=='m' && c[4]=='=') {
                theme_index = c[6] - '0';
            }
        }
        apply_theme();
        in_setup = 0;
    } else {
        setup_lang_sel = ui_lang;
        setup_theme_sel = theme_index;
        in_setup = 1;
    }

    win_open(WIN_TERM); 
    dframe_end(); 
    int dirty = 1, force_full = 1;
    
    while (!gfx_done) {
        // 🚀 拦截配置向导
        if (in_setup) {
            mouse_poll();
            
            // 🚀 修复：鼠标移动或点击都要重绘
            if (mouse_moved || mouse_click) {
                if (mouse_click) setup_click();
                cursor_restore_bg();
                force_full = 1;
            }
            
            // 🚀 新增：支持键盘操作
            EFI_INPUT_KEY k;
            if (!EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k))) {
                if (k.UnicodeChar == '\r' || k.UnicodeChar == '\n' || k.ScanCode == 0x0D) {
                    setup_save(); // 回车直接保存
                } else if (k.UnicodeChar == '1') {
                    setup_lang_sel = 0; force_full = 1; // 按 1 中文
                } else if (k.UnicodeChar == '2') {
                    setup_lang_sel = 1; force_full = 1; // 按 2 英文
                } else if (k.ScanCode == 0x03) { // 左方向键
                    setup_theme_sel = (setup_theme_sel + 5) % 6; force_full = 1;
                } else if (k.ScanCode == 0x04) { // 右方向键
                    setup_theme_sel = (setup_theme_sel + 1) % 6; force_full = 1;
                }
            }
            
            if (force_full) {
                setup_draw();
                gfx_cursor();
                gfx_present();
                force_full = 0;
            }
            ST->BootServices->Stall(100);
            continue;
        }

        // 锁屏状态拦截
        if (screen_locked) {
            if (force_full) {
                gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(8, 10, 18));
                const CHAR16 *title = T(S_LOCKED);
                int tw = 0; for (const CHAR16 *q = title; *q; ++q) tw += (*q <= 126) ? 8 : 16;
                gfx_text_w(((int)gfx_w - tw) / 2, (int)gfx_h / 2 - 40, title, gfx_accent);
                
                int bw = 240, bh = 32;
                int bx = ((int)gfx_w - bw) / 2, by = (int)gfx_h / 2;
                gfx_fill(bx, by, bw, bh, gfx_rgb(20, 24, 34));
                gfx_border(bx, by, bw, bh, gfx_accent);
                
                CHAR16 stars[33];
                int sn = 0;
                for (int i = 0; i < lock_len && i < 32; ++i) stars[sn++] = L'*';
                stars[sn] = 0;
                gfx_text_w(bx + 8, by + 8, stars, gfx_text_c);
                gfx_fill(bx + 8 + lock_len * 8, by + 6, 2, 20, gfx_accent);
                
                const CHAR16 *hint = T(S_UNLOCK_HINT);
                int hw = 0; for (const CHAR16 *q = hint; *q; ++q) hw += (*q <= 126) ? 8 : 16;
                gfx_text_w(((int)gfx_w - hw) / 2, by + bh + 20, hint, gfx_dim);
                
                gfx_present();
                force_full = 0; dirty = 0;
            }
            
            EFI_INPUT_KEY k;
            if (!EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k))) {
                if (k.UnicodeChar == '\r' || k.UnicodeChar == '\n') {
                    lock_input[lock_len] = 0;
                    if (scmp(lock_input, "wct") == 0) {
                        screen_locked = 0; lock_len = 0; lock_input[0] = 0;
                        force_full = 1; dirty = 1;
                    } else {
                        lock_len = 0; lock_input[0] = 0;
                    }
                } else if (k.UnicodeChar == 8) {
                    if (lock_len > 0) lock_input[--lock_len] = 0;
                } else if (k.UnicodeChar >= 32 && k.UnicodeChar < 127 && lock_len < 31) {
                    lock_input[lock_len++] = (char)k.UnicodeChar;
                }
                force_full = 1;
            }
            ST->BootServices->Stall(10000);
            continue;
        }

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
        ST->BootServices->Stall(100);
    }
    dframe_end();
}


static void gfx_char16_s(int x, int y, unsigned short cp, UINT32 c, int sc) {
    int idx = zh_lookup(cp);
    if (idx < 0) { gfx_char(x, y, '?', c); return; }
    for (int r = 0; r < 16; ++r) {
        unsigned short bits = ((unsigned short)zh_bits[idx][r * 2] << 8) | zh_bits[idx][r * 2 + 1];
        for (int col = 0; col < 16; ++col)
            if (bits & (0x8000 >> col)) gfx_fill(x + col * sc, y + r * sc, sc, sc, c);
    }
}
static void gfx_text_w_s(int x, int y, const CHAR16 *s, UINT32 c, int sc) {
    while (*s) {
        if ((unsigned short)*s <= 126) { gfx_char(x, y, (unsigned char)*s, c); x += 8 * sc; }
        else { gfx_char16_s(x, y, (unsigned short)*s, c, sc); x += 16 * sc; }
        ++s;
    }
}
static void bsod_bar(int x, int y, int w, int h, int pct) {
    gfx_fill(x - 2, y - 2, w + 4, h + 4, gfx_rgb(0, 40, 100));
    gfx_fill(x, y, w, h, gfx_rgb(10, 25, 60));
    gfx_fill(x, y, w * pct / 100, h, gfx_rgb(160, 210, 255));
}
static void cmd_bsod(void) {
    if (EFI_IS_ERROR(ST->BootServices->LocateProtocol(&gop_guid, NULL, (VOID **)&gfx))) return;
    gfx_w = gfx->Mode->Info->HorizontalResolution;
    gfx_h = gfx->Mode->Info->VerticalResolution;
    gfx_stride = gfx->Mode->Info->PixelsPerScanLine;
    gfx_hw = (UINT32 *)(UINTN)gfx->Mode->FrameBufferBase;
    VOID *buf = NULL;
    if (!EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, gfx_stride * gfx_h * 4, &buf)) && buf) gfx_back = (UINT32 *)buf;
    gfx_fb = gfx_back ? gfx_back : gfx_hw;
    gfx_active = 1;
    ST->ConOut->EnableCursor(ST->ConOut, FALSE);

    UINT32 bg = gfx_rgb(0, 62, 140);
    UINT32 white = gfx_rgb(245, 250, 255);
    UINT32 dim = gfx_rgb(185, 210, 240);
    gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, bg);
    gfx_text(56, 46, "wcteos :(", white);
    gfx_text_w_s(56, 84, L"正在分析错误", white, 2);

    int barx = (int)gfx_w - 500, bary = 90, barw = 300, barh = 26;
    int cx = (int)gfx_w / 2;
    gfx_text_w(cx - 210, (int)gfx_h / 2 - 70, L"系统出错，已中止运行。", white);
    gfx_text_w(cx - 210, (int)gfx_h / 2 - 36, L"错误代码: WCT-BSOD-0x00000001", dim);
    gfx_text_w(cx - 210, (int)gfx_h / 2 - 12, L"问题位置: 内核 / 图形系统", dim);
    gfx_text_w(cx - 260, (int)gfx_h / 2 + 24, L"读完错误信息之后自动重启，蓝屏是为了保护文件数据。", dim);

    int mod = 5, n = QR_FIX_N, qw = n * mod;
    int qx = (int)gfx_w - qw - 64, qy = (int)gfx_h - qw - 56;
    gfx_fill(qx - 12, qy - 12, qw + 24, qw + 24, gfx_rgb(198, 224, 252));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (qr_fix[y][x]) gfx_fill(qx + x * mod, qy + y * mod, mod, mod, gfx_rgb(0, 38, 96));

    for (int p = 0; p <= 100; p += 4) {
        bsod_bar(barx, bary, barw, barh, p);
        gfx_text(barx + barw + 12, bary + 5, "COLLECTING", dim);
        gfx_present();
        ST->BootServices->Stall(70000);
    }
    ST->RuntimeServices->ResetSystem(1, EFI_SUCCESS, 0, NULL);
    for (;;) ST->BootServices->Stall(1000000);
}


typedef struct { UINT32 MediaId; UINT8 RemovableMedia; UINT8 MediaPresent; UINT8 LogicalPartition; UINT8 WriteProtected; UINT64 LastBlock; UINT32 BlockSize; } MY_MEDIA;
typedef struct { UINT32 Revision; MY_MEDIA *Media; } MY_BIO;
static EFI_GUID bio_guid = { 0x964E5B21, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
static EFI_GUID sp_guid  = { 0x31878C87, 0x0B75, 0x11D5, { 0x9A, 0x4B, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } };
static UINT64 ins_lb(void *m, int off) { return *(UINT64 *)((UINT8 *)m + off); }
static UINT32 ins_bs(void *m, int off) { return *(UINT32 *)((UINT8 *)m + off); }

static void dbg_num(UINT64 v, CHAR16 *buf, int *p) {
    CHAR16 t[24]; int n = 0;
    if (!v) t[n++] = L'0';
    while (v) { t[n++] = (CHAR16)(L'0' + (v % 10)); v /= 10; }
    while (n) buf[(*p)++] = t[--n];
}
static void dbg_dump(void *m) {
    CHAR16 b[200]; int p = 0;
    const CHAR16 *tags[6] = { L"l8=", L"b16=", L"l16=", L"b24=", L"l24=", L"b32=" };
    int lo[6] = {8, 8, 16, 16, 24, 24}, bo[6] = {16, 20, 24, 28, 32, 36};
    for (int i = 0; i < 6; ++i) {
        for (int k = 0; tags[i][k]; ++k) b[p++] = tags[i][k];
        if (i & 1) dbg_num(ins_bs(m, bo[i]), b, &p); else dbg_num(ins_lb(m, lo[i]), b, &p);
        b[p++] = L' ';
    }
    b[p++] = L'\r'; b[p++] = L'\n'; b[p] = 0;
    ST->ConOut->OutputString(ST->ConOut, b);
}
static int ins_disk_count(EFI_HANDLE *out, int max, UINT64 *mb) {
    EFI_HANDLE *buf = NULL; UINTN n = 0; int k = 0;
    if (EFI_IS_ERROR(ST->BootServices->LocateHandleBuffer(2, &bio_guid, NULL, &n, &buf)) || !buf) return 0;
    for (UINTN i = 0; i < n && k < max; ++i) {
        void *b = NULL;
        if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(buf[i], &bio_guid, &b)) || !b) continue;
        void *m = *(void **)((UINT8 *)b + 8);
        if (!m) continue;
        if (((UINT8 *)m)[6]) continue;
        UINT32 bs = ins_bs(m, 12);
        UINT64 lb = ins_lb(m, 24);
        UINT64 bytes = (bs >= 256 && bs <= 8192) ? (lb + 1) * (UINT64)bs : 0;
        if (bytes < 400ULL * 1024 * 1024) continue;
        if (!bytes) continue;
        out[k] = buf[i]; mb[k] = bytes / (1024ULL * 1024); k++;
    }
    if (buf) ST->BootServices->FreePool(buf);
    return k;
}
static const char *g_build_str = "BUILD=" BUILD_HASH;
static void ins_ui(int sel, int n, UINT64 *mb, int hot, const char *status) {
    UINT32 bg = gfx_rgb(9, 30, 60), card = gfx_rgb(245, 248, 252), line = gfx_rgb(210, 222, 238);
    gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, bg);
    for (int i = 0; i < (int)gfx_h / 2; ++i) gfx_fill(0, i, (int)gfx_w, 1, gfx_rgb(9, 30 + i / 12, 60 + i / 8));
    int cx = (int)gfx_w / 2 - 380, cy = 90, cw = 760, ch = (int)gfx_h - 200;
    gfx_fill(cx, cy, cw, ch, card);
    gfx_fill(cx, cy, cw, 46, gfx_rgb(16, 84, 150));
    gfx_text_w(cx + 22, cy + 12, L"wcteos 安装程序", gfx_rgb(255, 255, 255));
    gfx_text(cx + cw - 34, cy + 12, "X", gfx_rgb(220, 235, 250));
    gfx_text(cx + 22, cy + 66, "Select target disk", gfx_rgb(40, 60, 90));
    gfx_text(cx + cw - 150, cy + 66, (char *)g_build_str, gfx_rgb(90, 110, 140));
    for (int i = 0; i < n; ++i) {
        int y = cy + 96 + i * 52;
        int on = (i == sel);
        gfx_fill(cx + 22, y, cw - 44, 44, on ? gfx_rgb(206, 226, 250) : gfx_rgb(255, 255, 255));
        gfx_border(cx + 22, y, cw - 44, 44, on ? gfx_rgb(16, 84, 150) : line);
        char t[64]; int q = 0;
        t[q++] = 'D'; t[q++] = 'i'; t[q++] = 's'; t[q++] = 'k'; t[q++] = ' ';
        t[q++] = (char)('0' + (i % 10)); t[q++] = ' '; t[q++] = ' '; t[q++] = ' ';
        UINT64 v = mb[i]; char d[24]; int dn = 0;
        if (!v) { d[dn++] = '0'; }
        while (v) { d[dn++] = (char)('0' + (v % 10)); v /= 10; }
        while (dn) t[q++] = d[--dn];
        t[q++] = ' '; t[q++] = 'M'; t[q++] = 'B'; t[q] = 0;
        gfx_text(cx + 40, y + 14, t, gfx_rgb(25, 40, 60));
    }
    int by = cy + ch - 92;
    gfx_fill(cx + 22, by, 150, 44, hot == 1 ? gfx_rgb(0, 110, 190) : gfx_rgb(16, 84, 150));
    gfx_text_w(cx + 62, by + 13, L"安装", gfx_rgb(255, 255, 255));
    gfx_fill(cx + 190, by, 150, 44, hot == 2 ? gfx_rgb(90, 100, 116) : gfx_rgb(120, 132, 148));
    gfx_text_w(cx + 230, by + 13, L"退出", gfx_rgb(255, 255, 255));
    if (status) gfx_text(cx + 22, by + 58, (char *)status, gfx_rgb(70, 90, 115));
    gfx_present();
}
static void ins_progress(const char *label, int pct) {
    UINT32 bg = gfx_rgb(9, 30, 60);
    gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, bg);
    int cx = (int)gfx_w / 2 - 380, cy = (int)gfx_h / 2 - 90;
    gfx_fill(cx, cy, 760, 180, gfx_rgb(245, 248, 252));
    gfx_text(cx + 24, cy + 24, (char *)label, gfx_rgb(30, 50, 80));
    int bw = 712;
    gfx_fill(cx + 24, cy + 70, bw, 26, gfx_rgb(220, 228, 238));
    int half = bw / 2;
    if (pct <= 50) gfx_fill(cx + 24, cy + 70, half * pct / 50, 26, gfx_rgb(16, 120, 200));
    else { gfx_fill(cx + 24, cy + 70, half, 26, gfx_rgb(40, 150, 90)); gfx_fill(cx + 24 + half, cy + 70, half * (pct - 50) / 50, 26, gfx_rgb(16, 120, 200)); }
    gfx_fill(cx + 24 + half - 2, cy + 64, 4, 38, gfx_rgb(150, 165, 185));
    char pc[16]; pc[0] = (char)('0' + pct / 100); pc[1] = (char)('0' + (pct / 10) % 10); pc[2] = '%'; pc[3] = 0;
    if (pct < 100) { pc[0] = (char)('0' + (pct / 10) % 10); pc[1] = '%'; pc[2] = 0; }
    gfx_text(cx + 24, cy + 110, pc, gfx_rgb(60, 80, 105));
    gfx_text(cx + 24, cy + 136, "Do not power off", gfx_rgb(120, 135, 155));
    gfx_present();
}

typedef EFI_STATUS (*INS_WR)(void *, UINT32, UINT64, UINTN, void *);
static void *ins_bio;
static void *ins_med;
static UINT64 ins_last_err;
static int ins_write(void *b, void *m, UINT64 lba, UINTN cnt, void *buf) {
    INS_WR wr = *(INS_WR *)((UINT8 *)b + 32);
    EFI_STATUS st = wr(b, *(UINT32 *)m, lba, cnt * 512, buf);
    ins_last_err = (UINT64)st;
    return (int)st;
}
static UINT64 fs_err;
static void *fs_b; static void *fs_m; static UINT64 fs_part; static UINT32 fs_fatsz; static EFI_HANDLE fs_dh;
static int ins_format_disk(int idx, EFI_HANDLE *disks, UINT64 *mb, int *pct) {
    void *b = NULL, *m = NULL;
    if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(disks[idx], &bio_guid, &b)) || !b) return 0;
    m = *(void **)((UINT8 *)b + 8);
    if (!m) return 0;
    UINT64 lb = ins_lb(m, 24);
    UINT64 total = lb + 1;
    if (total < 2048 + 65536) return 0;
    UINT8 sec[512];
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    /* Legacy BIOS 启动时给出明确提示，而不是停在 "Booting from hard disk..." */
    {
        static const UINT8 mbr_hint[] = {
            0xBE,0x14,0x7C,0xB4,0x0E,0xAC,0x84,0xC0,0x74,0x04,0xCD,0x10,0xEB,0xF7,0xB4,0x00,
            0xCD,0x16,0xCD,0x18,0x77,0x63,0x74,0x65,0x6F,0x73,0x20,0x50,0x72,0x6F,0x20,0x69,
            0x73,0x20,0x61,0x20,0x55,0x45,0x46,0x49,0x2D,0x6F,0x6E,0x6C,0x79,0x20,0x73,0x79,
            0x73,0x74,0x65,0x6D,0x2C,0x20,0x42,0x49,0x4F,0x53,0x20,0x62,0x6F,0x6F,0x74,0x20,
            0x69,0x73,0x20,0x6E,0x6F,0x74,0x20,0x73,0x75,0x70,0x70,0x6F,0x72,0x74,0x65,0x64,
            0x2E,0x0D,0x0A,0x45,0x6E,0x61,0x62,0x6C,0x65,0x20,0x55,0x45,0x46,0x49,0x20,0x62,
            0x6F,0x6F,0x74,0x20,0x69,0x6E,0x20,0x42,0x49,0x4F,0x53,0x20,0x28,0x64,0x69,0x73,
            0x61,0x62,0x6C,0x65,0x20,0x43,0x53,0x4D,0x20,0x2F,0x20,0x4C,0x65,0x67,0x61,0x63,
            0x79,0x29,0x2C,0x20,0x74,0x68,0x65,0x6E,0x20,0x73,0x65,0x6C,0x65,0x63,0x74,0x0D,
            0x0A,0x74,0x68,0x65,0x20,0x62,0x6F,0x6F,0x74,0x20,0x65,0x6E,0x74,0x72,0x79,0x20,
            0x70,0x72,0x65,0x66,0x69,0x78,0x65,0x64,0x20,0x77,0x69,0x74,0x68,0x20,0x55,0x45,
            0x46,0x49,0x3A,0x20,0x66,0x6F,0x72,0x20,0x74,0x68,0x69,0x73,0x20,0x64,0x65,0x76,
            0x69,0x63,0x65,0x2E,0x0D,0x0A,0x50,0x72,0x65,0x73,0x73,0x20,0x61,0x6E,0x79,0x20,
            0x6B,0x65,0x79,0x20,0x74,0x6F,0x20,0x63,0x6F,0x6E,0x74,0x69,0x6E,0x75,0x65,0x2E,
            0x2E,0x2E,0x0D,0x0A,0x00,
        };
        for (UINTN i = 0; i < sizeof(mbr_hint) && i < 446; ++i) sec[i] = mbr_hint[i];
    }
    UINT64 pstart = 2048, psec = total - pstart;
    sec[446] = 0x00; sec[450] = 0xEF;
    *(UINT32 *)(sec + 454) = (UINT32)pstart;
    *(UINT32 *)(sec + 458) = (UINT32)psec;
    sec[510] = 0x55; sec[511] = 0xAA;
    if (ins_write(b, m, 0, 1, sec) != 0) return 0;
    { void (*fl)(void *) = *(void (**)(void *))((UINT8 *)b + 40); if (fl) fl(b); }
    *pct = 30; ins_progress("Formatting disk...", *pct);
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    sec[0] = 0xEB; sec[1] = 0x58; sec[2] = 0x90; sec[3] = 'W'; sec[4] = 'C'; sec[5] = 'T'; sec[6] = 'E'; sec[7] = 'O'; sec[8] = 'S'; sec[9] = ' '; sec[10] = ' ';
    *(UINT16 *)(sec + 11) = 512; sec[13] = 1; *(UINT16 *)(sec + 14) = 32; sec[16] = 2;
    sec[21] = 0xF8; *(UINT16 *)(sec + 24) = 63; *(UINT16 *)(sec + 26) = 255;
    *(UINT32 *)(sec + 28) = (UINT32)pstart;
    *(UINT32 *)(sec + 32) = (UINT32)psec;
    UINT32 fatsz = (UINT32)(psec / 128) + 4;
    *(UINT32 *)(sec + 36) = fatsz;
    *(UINT32 *)(sec + 44) = 2; *(UINT16 *)(sec + 48) = 1; *(UINT16 *)(sec + 50) = 6;
    sec[64] = 0x80; sec[66] = 0x29;
    *(UINT32 *)(sec + 67) = 0x57435421;
    const char *lab = "WCTEOS     "; for (int i = 0; i < 11; ++i) sec[71 + i] = lab[i];
    const char *fs = "FAT32   "; for (int i = 0; i < 8; ++i) sec[82 + i] = fs[i];
    sec[510] = 0x55; sec[511] = 0xAA;
    if (ins_write(b, m, pstart, 1, sec) != 0) return 0;
    if (ins_write(b, m, pstart + 6, 1, sec) != 0) return 0;
    *pct = 60; ins_progress("Formatting disk...", *pct);
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    *(UINT32 *)(sec + 0) = 0x41615252;
    *(UINT32 *)(sec + 484) = 0x61417272;
    *(UINT32 *)(sec + 488) = 0x61417272;
    UINT32 clus = (UINT32)((psec - 32 - 2ULL * fatsz) / 1);
    *(UINT32 *)(sec + 492) = clus - 1;
    *(UINT32 *)(sec + 496) = 3;
    *(UINT32 *)(sec + 508) = 0xAA550000;
    if (ins_write(b, m, pstart + 1, 1, sec) != 0) return 0;
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    *(UINT32 *)(sec + 0) = 0x0FFFFFF8; *(UINT32 *)(sec + 4) = 0x0FFFFFFF; *(UINT32 *)(sec + 8) = 0x0FFFFFFF;
    for (int f = 0; f < 2; ++f) if (ins_write(b, m, pstart + 32 + (UINT64)f * fatsz, 1, sec) != 0) return 0;
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    const char *vl = "WCTEOS     "; for (int i = 0; i < 11; ++i) sec[i] = vl[i]; sec[11] = 0x08;
    UINT64 root = pstart + 32 + 2ULL * fatsz;
    if (ins_write(b, m, root, 1, sec) != 0) return 0;
    *pct = 90; ins_progress("Verifying...", *pct);
    ins_bio = b; ins_med = m; fs_dh = disks[idx];
    fs_b = b; fs_m = m; fs_part = pstart; fs_fatsz = fatsz;
    { void (*fl2)(void *) = *(void (**)(void *))((UINT8 *)b + 40); if (fl2) fl2(b); }
    return 1;
}


static UINT64 fs_lba(UINT32 c) { return fs_part + 32ull + 2ull * fs_fatsz + (c - 2); }
static int fs_rd(UINT64 lba, void *buf) { EFI_STATUS(*rd)(void*, UINT32, UINT64, UINTN, void*) = *(void**)((UINT8*)fs_b + 24); return (int)rd(fs_b, *(UINT32*)fs_m, lba, 512, buf); }
static int fs_wr(UINT64 lba, void *buf) { return ins_write(fs_b, fs_m, lba, 1, buf); }
static int fs_fat(UINT32 c, UINT32 val) {
    UINT8 sec[512];
    UINT32 off = c / 128, idx = c % 128;
    for (int f = 0; f < 2; ++f) {
        UINT64 lba = fs_part + 32ull + (UINT64)f * fs_fatsz + off;
        if (fs_rd(lba, sec) != 0) return 0;
        *(UINT32 *)(sec + idx * 4) = val;
        if (fs_wr(lba, sec) != 0) return 0;
    }
    return 1;
}
static void fs_dirent(UINT8 *e, const char *n, UINT8 attr, UINT32 clus, UINT32 size) {
    for (int i = 0; i < 32; ++i) e[i] = 0;
    for (int i = 0; i < 11; ++i) e[i] = ' ';
    if (n[0] == '.' && n[1] == 0) { e[0] = '.'; }
    else if (n[0] == '.' && n[1] == '.' && n[2] == 0) { e[0] = '.'; e[1] = '.'; }
    else {
        int k = 0;
        while (*n && *n != '.' && k < 8) e[k++] = (UINT8)*n++;
        if (*n == '.') ++n;
        k = 8;
        while (*n && k < 11) e[k++] = (UINT8)*n++;
    }
    e[11] = attr;
    *(UINT16 *)(e + 20) = (UINT16)(clus >> 16);
    *(UINT16 *)(e + 26) = (UINT16)(clus & 0xFFFF);
    *(UINT32 *)(e + 28) = size;
}
static int fs_mkdir(UINT32 self, UINT32 parent, const char *name, UINT32 child) {
    UINT8 sec[512];
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    fs_dirent(sec, ".", 0x10, self, 0);
    fs_dirent(sec + 32, "..", 0x10, parent == 2 ? 0 : parent, 0);
    fs_dirent(sec + 64, name, 0x10, child, 0);
    return fs_wr(fs_lba(self), sec);
}
static int fs_stage;
static int ins_copy_pkgs(EFI_FILE_PROTOCOL *dstroot);
static int fs_install_files(int *pct) {
    if (!fs_dh || !ROOT) return 0;
    typedef EFI_STATUS (EFIAPI *DISC)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
    typedef EFI_STATUS (EFIAPI *CONN)(EFI_HANDLE, EFI_HANDLE *, VOID *, BOOLEAN);
    ((DISC)ST->BootServices->DisconnectController)(fs_dh, NULL, NULL);
    ((CONN)ST->BootServices->ConnectController)(fs_dh, NULL, NULL, 1);
    ST->BootServices->Stall(500000);
    fs_stage = 1;
    EFI_HANDLE *hs = NULL; UINTN nh = 0;
    if (EFI_IS_ERROR(ST->BootServices->LocateHandleBuffer(2, &simple_fs_guid, NULL, &nh, &hs))) return 0;
    fs_stage = 2;
    EFI_FILE_PROTOCOL *root = NULL;
    for (UINTN i = 0; i < nh && !root; ++i) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *vfs = NULL;
        if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(hs[i], &simple_fs_guid, (VOID **)&vfs)) || !vfs) continue;
        EFI_FILE_PROTOCOL *r2 = NULL;
        if (EFI_IS_ERROR(vfs->OpenVolume(vfs, &r2)) || !r2) continue;
        if (r2 == ROOT) { continue; }
        EFI_FILE_PROTOCOL *probe = NULL;
        if (EFI_IS_ERROR(r2->Open(r2, &probe, L"EFI", 1, 0)) || !probe) { root = r2; }
        else { probe->Close(probe); r2->Close(r2); }
    }
    if (hs) ST->BootServices->FreePool(hs);
    fs_stage = 3;
    if (!root) return 0;
    EFI_FILE_PROTOCOL *sf = NULL;
    if (EFI_IS_ERROR(ROOT->Open(ROOT, &sf, L"EFI\\BOOT\\BOOTX64.EFI", 1, 0)) || !sf) { fs_stage = 4; return 0; }
    fs_stage = 5;
    EFI_FILE_PROTOCOL *de = NULL, *db = NULL, *df = NULL;
    UINT64 mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
    if (EFI_IS_ERROR(root->Open(root, &de, L"EFI", mode, EFI_FILE_DIRECTORY)) || !de) { fs_stage = 6; root->Close(root); return 0; }
    fs_stage = 7;
    if (EFI_IS_ERROR(de->Open(de, &db, L"BOOT", mode, EFI_FILE_DIRECTORY)) || !db) { fs_stage = 8; return 0; }
    fs_stage = 9;
    if (EFI_IS_ERROR(db->Open(db, &df, L"BOOTX64.EFI", mode, 0x20)) || !df) { fs_stage = 10; return 0; }
    fs_stage = 11;
    UINT64 real = 0; UINT32 tick = 0;
    for (;;) {
        UINT8 buf[512]; UINTN n = 512;
        for (int i = 0; i < 512; ++i) buf[i] = 0;
        EFI_STATUS rs = sf->Read(sf, &n, buf);
        if (EFI_IS_ERROR(rs) || n == 0) break;
        UINTN w = n;
        if (EFI_IS_ERROR(df->Write(df, &w, buf)) || w != n) { fs_stage = 12; return 0; }
        real += n;
        if (++tick >= 128) {
            tick = 0;
            *pct = 30 + (int)((real * 50) / 200000); if (*pct > 85) *pct = 85;
            ins_progress("Installing system files...", *pct);
        }
    }
    fs_stage = 13;
    if (!real) return 0;
    df->Flush(df); df->Close(df); db->Close(db); de->Close(de);
    ins_copy_pkgs(root);
    root->Close(root); sf->Close(sf);
    ST->BootServices->Stall(300000);
    return 1;
}


/* 把安装介质 \wct\pkgs 下的所有包复制到目标盘同名目录 */
static int ins_copy_pkgs(EFI_FILE_PROTOCOL *dstroot) {
    EFI_FILE_PROTOCOL *src = NULL;
    if (EFI_IS_ERROR(ROOT->Open(ROOT, &src, L"wct", 1, 0)) || !src) return 0;
    EFI_FILE_PROTOCOL *srcp = NULL;
    int ok = 0;
    if (!EFI_IS_ERROR(src->Open(src, &srcp, L"pkgs", 1, 0)) && srcp) {
        EFI_FILE_PROTOCOL *dw = NULL, *dwp = NULL;
        UINT64 mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
        dstroot->Open(dstroot, &dw, L"wct", mode, EFI_FILE_DIRECTORY);
        if (dw) {
            dw->Open(dw, &dwp, L"pkgs", mode, EFI_FILE_DIRECTORY);
            if (dwp) {
                static UINT8 info[2048];
                UINTN sz;
                EFI_GUID gi = { 0x09576E92, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
                srcp->SetPosition(srcp, 0);
                for (;;) {
                    sz = sizeof(info);
                    if (EFI_IS_ERROR(srcp->Read(srcp, &sz, info)) || sz == 0) break;
                    EFI_FILE_INFO *fi = (EFI_FILE_INFO *)info;
                    if (fi->FileName[0] == L'.') continue;
                    if (fi->Attribute & EFI_FILE_DIRECTORY) continue;
                    EFI_FILE_PROTOCOL *sf = NULL, *df = NULL;
                    if (EFI_IS_ERROR(srcp->Open(srcp, &sf, fi->FileName, 1, 0)) || !sf) continue;
                    if (!EFI_IS_ERROR(dwp->Open(dwp, &df, fi->FileName, mode, 0x20)) && df) {
                        static UINT8 buf[4096];
                        for (;;) {
                            UINTN n = sizeof(buf);
                            if (EFI_IS_ERROR(sf->Read(sf, &n, buf)) || n == 0) break;
                            UINTN w = n;
                            if (EFI_IS_ERROR(df->Write(df, &w, buf))) break;
                        }
                        df->Flush(df); df->Close(df);
                        ok = 1;
                    }
                    sf->Close(sf);
                }
                dwp->Close(dwp);
            }
            dw->Close(dw);
        }
        srcp->Close(srcp);
    }
    src->Close(src);
    return ok;
}

static int ins_load_core(void) {
    EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
    if (!g_loaded) return 0;
    if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(g_loaded->DeviceHandle, &device_path_guid, (VOID **)&dp)) || !dp) return 0;
    static UINT8 node[320]; UINT8 *p = node; UINTN used = 0;
    while (used < 200 && ((UINT8 *)dp)[0] != 0x7F) {
        UINT16 l = (UINT16)(((UINT8 *)dp)[2] | (((UINT8 *)dp)[3] << 8));
        if (!l || l > 120) break;
        for (UINT16 i = 0; i < l; ++i) p[used++] = ((UINT8 *)dp)[i];
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)dp + l);
    }
    p[used] = 0x04; p[used + 1] = 0x04; UINT16 l2 = 4 + 24; p[used + 2] = (UINT8)l2; p[used + 3] = (UINT8)(l2 >> 8);
    const char *fp = "\\wct\\install.efi"; int q = 0;
    while (fp[q]) { p[used + 4 + q * 2] = (UINT8)fp[q]; p[used + 5 + q * 2] = 0; ++q; }
    p[used + 4 + q * 2] = 0; p[used + 5 + q * 2] = 0;
    p[used + 4 + (q + 1) * 2] = 0x7F; p[used + 5 + (q + 1) * 2] = 0xFF;
    p[used + 6 + (q + 1) * 2] = 0x04; p[used + 7 + (q + 1) * 2] = 0x00;
    EFI_HANDLE ih = NULL;
    if (EFI_IS_ERROR(ST->BootServices->LoadImage(FALSE, g_image, (EFI_DEVICE_PATH_PROTOCOL *)node, NULL, 0, &ih)) || !ih) return 0;
    ins_progress("Installing wcteos Pro...", 86);
    ST->BootServices->StartImage(ih, NULL, NULL);
    return 1;
}
static char ins_stat[48] = "Mouse or arrow keys. Enter installs.";

static void ins_chain_test(char *out) {
    EFI_STATUS s1 = 0xAA, s2 = 0xAA, s3 = 0xAA, s4 = 0xAA;
    UINT8 head[2] = { 0, 0 }; UINTN got = 0;
    EFI_FILE_PROTOCOL *r = ROOT, *d1 = NULL, *d2 = NULL, *f = NULL;
    if (!r) { s1 = 0xEE; goto done; }
    s1 = r->Open(r, &d1, L"EFI", 1, 0);
    if (EFI_IS_ERROR(s1) || !d1) goto done;
    s2 = d1->Open(d1, &d2, L"BOOT", 1, 0);
    if (EFI_IS_ERROR(s2) || !d2) goto done;
    s3 = d2->Open(d2, &f, L"BOOTX64.EFI", 1, 0);
    if (EFI_IS_ERROR(s3) || !f) goto done;
    got = 2; s4 = f->Read(f, &got, head);
done:
    { int q = 0; out[q++] = 'A'; out[q++] = '=';
    out[q++] = (char)('0' + (s1 & 15)); out[q++] = ' ';
    out[q++] = 'B'; out[q++] = '=';
    out[q++] = (char)('0' + (s2 & 15)); out[q++] = ' ';
    out[q++] = 'C'; out[q++] = '=';
    out[q++] = (char)('0' + (s3 & 15)); out[q++] = ' ';
    out[q++] = 'D'; out[q++] = '=';
    out[q++] = (char)('0' + (s4 & 15)); out[q++] = ' ';
    out[q++] = 'G'; out[q++] = '=';
    out[q++] = (char)('0' + (got % 10)); out[q++] = ' ';
    out[q++] = 'M'; out[q++] = '=';
    out[q++] = head[0] == 0x4D && head[1] == 0x5A ? '1' : '0';
    out[q] = 0; }
}

static EFI_GUID nv_guid = { 0x8BE4DF61, 0x93CA, 0x11d2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } };
static int ins_register_boot(EFI_HANDLE dh) {
    if (!ST->RuntimeServices || !ST->RuntimeServices->SetVariable) return 0;
    if (dh) {
        typedef EFI_STATUS (EFIAPI *DISC)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
        typedef EFI_STATUS (EFIAPI *CONN)(EFI_HANDLE, EFI_HANDLE *, VOID *, BOOLEAN);
        ((DISC)ST->BootServices->DisconnectController)(dh, NULL, NULL);
        ((CONN)ST->BootServices->ConnectController)(dh, NULL, NULL, 1);
        ST->BootServices->Stall(300000);
    }
    EFI_HANDLE *hs = NULL; UINTN nh = 0;
    if (EFI_IS_ERROR(ST->BootServices->LocateHandleBuffer(2, &simple_fs_guid, NULL, &nh, &hs))) return 0;
    static UINT8 path[640]; UINTN u = 0; int found = 0;
    for (UINTN hi = 0; hi < nh && !found; ++hi) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL, *vf = NULL;
        if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(hs[hi], &simple_fs_guid, (VOID **)&vf)) || !vf) continue;
        fs = vf;
        EFI_FILE_PROTOCOL *root = NULL, *f = NULL;
        if (EFI_IS_ERROR(fs->OpenVolume(fs, &root)) || !root) continue;
        if (root == ROOT) continue;
        if (EFI_IS_ERROR(root->Open(root, &f, L"EFI\\BOOT\\BOOTX64.EFI", 1, 0)) || !f) continue;
        f->Close(f);
        EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
        if (EFI_IS_ERROR(ST->BootServices->HandleProtocol(hs[hi], &device_path_guid, (VOID **)&dp)) || !dp) continue;
        UINT8 *pp = (UINT8 *)dp; UINTN k = 0; int iscd = 0;
        while (k < 520 && !(pp[0] == 0x7F && pp[1] == 0xFF)) {
            UINT16 l = (UINT16)(pp[2] | (pp[3] << 8));
            if (!l || l > 160 || k + l > 520) break;
            if (pp[0] == 0x04 && pp[1] == 0x02) iscd = 1;
            for (UINT16 i = 0; i < l; ++i) path[k + i] = pp[i];
            k += l; pp += l;
        }
        if (k < 4 || iscd) continue;
        const CHAR16 *fp16 = L"\\EFI\\BOOT\\BOOTX64.EFI";
        UINTN q = 0; while (fp16[q]) ++q;
        UINTN fl = 4 + (q + 1) * 2;
        if (k + fl + 4 > sizeof(path)) continue;
        path[k] = 0x04; path[k + 1] = 0x04; path[k + 2] = (UINT8)fl; path[k + 3] = (UINT8)(fl >> 8);
        for (UINTN i = 0; i <= q; ++i) { path[k + 4 + i * 2] = (UINT8)fp16[i]; path[k + 5 + i * 2] = 0; }
        k += fl;
        path[k] = 0x7F; path[k + 1] = 0xFF; path[k + 2] = 0x04; path[k + 3] = 0x00; k += 4;
        u = k; found = 1;
    }
    if (hs) ST->BootServices->FreePool(hs);
    if (!found) return 0;
    typedef EFI_STATUS (EFIAPI *NVSET)(CHAR16 *, EFI_GUID *, UINT32, UINTN, VOID *);
    typedef EFI_STATUS (EFIAPI *NVGET)(CHAR16 *, EFI_GUID *, UINT32 *, UINTN *, VOID *);
    NVSET setv = (NVSET)ST->RuntimeServices->SetVariable;
    NVGET getv = (NVGET)ST->RuntimeServices->GetVariable;
    static UINT8 opt[700]; UINTN o = 6;
    opt[0] = 1; opt[1] = 0; opt[2] = 0; opt[3] = 0;
    opt[4] = (UINT8)u; opt[5] = (UINT8)(u >> 8);
    const CHAR16 *desc = L"wcteos Pro";
    for (int i = 0; i <= 11; ++i) { opt[o++] = (UINT8)desc[i]; opt[o++] = 0; }
    for (UINTN i = 0; i < u; ++i) opt[o++] = path[i];
    if (EFI_IS_ERROR(setv(L"Boot0000", &nv_guid, 7, o, opt))) return 0;
    UINT16 bo[64]; UINTN bsz = sizeof(bo); UINT32 at = 7;
    EFI_STATUS gs = getv ? getv(L"BootOrder", &nv_guid, &at, &bsz, bo) : 1;
    if (EFI_IS_ERROR(gs) || bsz < 2 || bsz > 120) { bo[0] = 0; bsz = 2; }
    else if (bo[0] != 0) {
        for (UINTN i = bsz / 2; i > 0; --i) bo[i] = bo[i - 1];
        bo[0] = 0; bsz += 2;
    }
    setv(L"BootOrder", &nv_guid, 7, bsz, bo);
    return 1;
}

static void cmd_installer(void) {
    if (EFI_IS_ERROR(ST->BootServices->LocateProtocol(&gop_guid, NULL, (VOID **)&gfx))) return;
    gfx_w = gfx->Mode->Info->HorizontalResolution; gfx_h = gfx->Mode->Info->VerticalResolution;
    gfx_stride = gfx->Mode->Info->PixelsPerScanLine; gfx_hw = (UINT32 *)(UINTN)gfx->Mode->FrameBufferBase;
    VOID *buf = NULL;
    if (!EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, gfx_stride * gfx_h * 4, &buf)) && buf) gfx_back = (UINT32 *)buf;
    gfx_fb = gfx_back ? gfx_back : gfx_hw; gfx_active = 1;
    ST->ConOut->EnableCursor(ST->ConOut, FALSE);
    int msel = 0;
    for (;;) {
        gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(12, 30, 64));
        gfx_text(60, 70, "wcteos Pro Setup", gfx_rgb(255, 255, 255));
        gfx_text(60, 110, "Up/Down to select, Enter to continue, Esc to quit.", gfx_rgb(200, 210, 230));
        for (int i = 0; i < 2; ++i) {
            int y = 200 + i * 64;
            gfx_fill(60, y, (int)gfx_w - 120, 48, msel == i ? gfx_rgb(40, 110, 200) : gfx_rgb(24, 48, 90));
            gfx_border(60, y, (int)gfx_w - 120, 48, msel == i ? gfx_rgb(255, 255, 255) : gfx_rgb(60, 90, 140));
            gfx_text(80, y + 16, i == 0 ? "1. Graphical setup (recommended)" : "2. Command-line setup", gfx_rgb(255, 255, 255));
        }
        gfx_text(60, (int)gfx_h - 40, "BUILD=" BUILD_HASH, gfx_rgb(120, 140, 170));
        gfx_present();
        EFI_INPUT_KEY k;
        while (EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k))) ST->BootServices->Stall(30000);
        if (k.UnicodeChar == 27) { gfx_active = 0; return; }
        if (k.ScanCode == 0x01) msel = 0;
        else if (k.ScanCode == 0x02) msel = 1;
        else if (k.UnicodeChar == '\r' || k.UnicodeChar == '\n' || k.ScanCode == 0x0D) {
            if (msel == 1) {
                gfx_active = 0; ST->ConOut->EnableCursor(ST->ConOut, TRUE);
                ST->ConOut->OutputString(ST->ConOut, L"\r\nCommand-line setup\r\n  1) select disk: disk-info\r\n  2) format: format-disk <n>\r\n  3) install: install-files <n>\r\n");
                return;
            }
            break;
        }
    }
    for (;;) {
        gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(245, 247, 250));
        gfx_fill(0, 0, (int)gfx_w, 64, gfx_rgb(20, 60, 120));
        gfx_text(40, 24, "Setup - License Agreement", gfx_rgb(255, 255, 255));
        gfx_text(40, 110, "wcteos Pro 1.0 - evaluation and use terms", gfx_rgb(30, 40, 60));
        gfx_text(40, 150, "1. This software is provided as-is, without warranty.", gfx_rgb(60, 70, 90));
        gfx_text(40, 178, "2. Installing will erase the selected target disk.", gfx_rgb(60, 70, 90));
        gfx_text(40, 206, "3. Back up your data before continuing.", gfx_rgb(60, 70, 90));
        gfx_text(40, 260, "Press Enter to accept, Esc to exit.", gfx_rgb(20, 60, 120));
        gfx_text(40, (int)gfx_h - 40, "BUILD=" BUILD_HASH, gfx_rgb(120, 130, 150));
        gfx_present();
        EFI_INPUT_KEY k2;
        while (EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k2))) ST->BootServices->Stall(30000);
        if (k2.UnicodeChar == 27) { gfx_active = 0; return; }
        if (k2.UnicodeChar == '\r' || k2.UnicodeChar == '\n' || k2.ScanCode == 0x0D) break;
    }
    EFI_HANDLE disks[16]; UINT64 mb[16];
    int n = ins_disk_count(disks, 16, mb); int sel = 0;
    EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
    ST->BootServices->LocateProtocol(&sp_guid, NULL, (VOID **)&sp);
    int mx = (int)gfx_w / 2, my = (int)gfx_h / 2;
    ins_ui(sel, n, mb, 0, n ? ins_stat : "No target disk found.");
    int page = 0;
    for (;;) {
        EFI_INPUT_KEY k;
        if (page == 1) {
            gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(245, 247, 250));
            gfx_fill(0, 0, (int)gfx_w, 64, gfx_rgb(20, 60, 120));
            gfx_text(40, 24, "Setup - Confirm", gfx_rgb(255, 255, 255));
            gfx_text(40, 140, "All data on the selected disk will be erased.", gfx_rgb(180, 40, 40));
            gfx_text(40, 180, "Enter = erase and install    Esc = back", gfx_rgb(20, 60, 120));
            gfx_text(40, (int)gfx_h - 40, "BUILD=" BUILD_HASH, gfx_rgb(120, 130, 150));
            gfx_present();
            if (!EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k))) {
                if (k.UnicodeChar == 27) { page = 0; }
                else if (k.UnicodeChar == '\r' || k.UnicodeChar == '\n' || k.ScanCode == 0x0D) {
                    int pct = 0;
                    if (ins_format_disk(sel, disks, mb, &pct)) {
                        if (fs_install_files(&pct)) {
                            pct = 100;
                            gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(245, 247, 250));
                            gfx_fill(0, 0, (int)gfx_w, 64, gfx_rgb(20, 60, 120));
                            gfx_text(40, 24, "Setup - Complete", gfx_rgb(255, 255, 255));
                            gfx_text(40, 140, "Installation finished. Remove setup media and reboot.", gfx_rgb(30, 40, 60));
                            /* NVRAM entry skipped: rely on UEFI ESP autoscan */
                            gfx_present();
                            ST->BootServices->Stall(3000000);
                        } else { ins_progress("Copy failed.", pct); ST->BootServices->Stall(3000000); }
                    } else { ins_progress("Format failed.", 0); ST->BootServices->Stall(3000000); }
                    break;
                }
            }
            continue;
        }
        if (!EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k))) {
            if (k.ScanCode == 0x01 && sel > 0) --sel;
            else if (k.ScanCode == 0x02 && sel + 1 < n) ++sel;
            else if (k.UnicodeChar == 27) break;
            else if (k.UnicodeChar == '\r' || k.UnicodeChar == '\n' || k.ScanCode == 0x0D) {
                page = 1;
            }
            ins_ui(sel, n, mb, 0, n ? ins_stat : "No target disk found.");
        }
        if (sp && sp->Mode) {
            EFI_SIMPLE_POINTER_STATE st;
            if (!EFI_IS_ERROR(sp->GetState(sp, &st))) {
                mx += st.RelativeMovementX / 8; my += st.RelativeMovementY / 8;
                if (mx < 0) mx = 0; if (my < 0) my = 0;
                if (mx > (int)gfx_w - 1) mx = (int)gfx_w - 1; if (my > (int)gfx_h - 1) my = (int)gfx_h - 1;
                if (st.LeftButton || st.RightButton) {
                    for (int i = 0; i < n; ++i) if (mx > 112 && mx < 892 && my > 186 + i * 52 && my < 230 + i * 52) sel = i;
                    if (my > (int)gfx_h - 200 - 92 + 90 && my < (int)gfx_h - 200 - 92 + 134) {
                        if (mx > 112 + 40 && mx < 112 + 172) {
                            int pct = 0;
                            if (ins_format_disk(sel, disks, mb, &pct)) { pct = 100; ins_progress("Format complete. Ready to install.", 100); ST->BootServices->Stall(1200000); }
                            else { ins_progress("Format failed.", 0); ST->BootServices->Stall(1200000); }
                        }
                        else if (mx > 112 + 190 && mx < 112 + 340) break;
                    }
                }
            }
        }
        ins_ui(sel, n, mb, 0, n ? ins_stat : "No target disk found.");
        for (int ry = 0; ry < 14; ++ry) {
            int wdt = ry < 8 ? ry + 1 : (ry < 11 ? 14 - ry : 3);
            gfx_fill(mx, my + ry, wdt, 1, gfx_rgb(20, 20, 20));
            if (wdt > 2) gfx_fill(mx + 1, my + ry, wdt - 2, 1, gfx_rgb(255, 255, 255));
        }
        gfx_present();
    }
    gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(0, 0, 0));
    gfx_present();
    gfx_active = 0;
    ST->ConOut->ClearScreen(ST->ConOut);
    if (gfx_back) { ST->BootServices->FreePool(gfx_back); gfx_back = NULL; }
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

static int st_exists(const CHAR16 *path);
static void st_install(CHAR16 *name);
static void st_remove(CHAR16 *name);
static void cmd_gstore(void) {
    if (EFI_IS_ERROR(ST->BootServices->LocateProtocol(&gop_guid, NULL, (VOID **)&gfx))) return;
    gfx_w = gfx->Mode->Info->HorizontalResolution; gfx_h = gfx->Mode->Info->VerticalResolution;
    gfx_stride = gfx->Mode->Info->PixelsPerScanLine; gfx_hw = (UINT32 *)(UINTN)gfx->Mode->FrameBufferBase;
    VOID *buf = NULL;
    if (!EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, gfx_stride * gfx_h * 4, &buf)) && buf) gfx_back = (UINT32 *)buf;
    gfx_fb = gfx_back ? gfx_back : gfx_hw; gfx_active = 1;
    ST->ConOut->EnableCursor(ST->ConOut, FALSE);
    static char names[16][64]; int n = 0;
    EFI_FILE_PROTOCOL *d = NULL;
    if (open_path(L"\\wct\\pkgs", &d, EFI_FILE_MODE_READ, 0)) {
        static UINT8 b2[2048];
        while (n < 16) {
            UINTN sz = sizeof(b2);
            if (EFI_IS_ERROR(d->Read(d, &sz, b2)) || !sz) break;
            EFI_FILE_INFO *fi = (EFI_FILE_INFO *)b2;
            if (!fi->FileName[0]) break;
            if (fi->FileName[0] == '.') continue;
            int i = 0;
            for (; fi->FileName[i] && i < 60; ++i) names[n][i] = (char)(fi->FileName[i] < 128 ? fi->FileName[i] : '?');
            names[n][i] = 0; ++n;
        }
        d->Close(d);
    }
    int sel = 0;
    for (;;) {
        gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(245, 247, 250));
        gfx_fill(0, 0, (int)gfx_w, 64, gfx_rgb(20, 60, 120));
        gfx_text(40, 24, "wcteos Store", gfx_rgb(255, 255, 255));
        gfx_text(40, 90, "Up/Down select   Enter install/remove   Esc quit", gfx_rgb(60, 70, 90));
        if (!n) gfx_text(40, 150, "No packages found in \\wct\\pkgs", gfx_rgb(120, 60, 60));
        for (int i = 0; i < n; ++i) {
            int y = 140 + i * 40;
            gfx_fill(40, y, (int)gfx_w - 80, 32, sel == i ? gfx_rgb(40, 110, 200) : gfx_rgb(230, 234, 240));
            gfx_text(56, y + 8, names[i], sel == i ? gfx_rgb(255, 255, 255) : gfx_rgb(30, 40, 60));
            CHAR16 tgt[256]; int k = 0;
            const CHAR16 *pre = L"\\wct\\apps\\";
            while (pre[k]) { tgt[k] = pre[k]; ++k; }
            for (int j = 0; names[i][j] && k < 250; ++j) tgt[k++] = (CHAR16)names[i][j];
            tgt[k] = 0;
            int is_w = 0;
            {
                int L = 0; while (names[i][L]) ++L;
                if (L >= 5 && names[i][L-5]=='.' && (names[i][L-4]=='w'||names[i][L-4]=='W')
                    && names[i][L-3]=='c' && names[i][L-2]=='d' && names[i][L-1]=='e') is_w = 1;
            }
            EFI_FILE_PROTOCOL *probe_f = NULL;
            int inst = 0;
            if (is_w) {
                if (open_path(L"\\wct\\java", &probe_f, EFI_FILE_MODE_READ, 0)) { probe_f->Close(probe_f); inst = 1; }
            } else {
                inst = st_exists(tgt);
            }
            gfx_text((int)gfx_w - 200, y + 8, inst ? "[installed]" : "[available]",
                     sel == i ? gfx_rgb(255, 255, 255) : gfx_rgb(80, 90, 110));
        }
        gfx_text(40, (int)gfx_h - 40, "BUILD=" BUILD_HASH, gfx_rgb(120, 130, 150));
        gfx_present();
        EFI_INPUT_KEY k;
        while (EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k))) ST->BootServices->Stall(30000);
        if (k.UnicodeChar == 27 || k.ScanCode == 0x17) break;
        if (k.ScanCode == 0x01 && sel > 0) --sel;
        else if (k.ScanCode == 0x02 && sel + 1 < n) ++sel;
        else if ((k.UnicodeChar == '\r' || k.UnicodeChar == '\n' || k.ScanCode == 0x0D) && n) {
            CHAR16 nm[80]; int q = 0;
            for (; names[sel][q] && q < 78; ++q) nm[q] = (CHAR16)names[sel][q];
            nm[q] = 0;
            CHAR16 tgt[256]; int k2 = 0;
            const CHAR16 *pre = L"\\wct\\apps\\";
            while (pre[k2]) { tgt[k2] = pre[k2]; ++k2; }
            for (int j = 0; nm[j] && k2 < 250; ++j) tgt[k2++] = nm[j];
            tgt[k2] = 0;
            int sel_is_wcde = 0;
            {
                UINTN L = 0; while (nm[L]) ++L;
                if (L >= 5 && nm[L-5]==L'.' && (nm[L-4]==L'w'||nm[L-4]==L'W')
                    && nm[L-3]==L'c' && nm[L-2]==L'd' && nm[L-1]==L'e') sel_is_wcde = 1;
            }
            if (sel_is_wcde) {
                EFI_FILE_PROTOCOL *pf = NULL;
                if (open_path(L"\\wct\\java", &pf, EFI_FILE_MODE_READ, 0) && pf) { pf->Close(pf); st_remove(nm); }
                else st_install(nm);
            } else if (st_exists(tgt)) st_remove(nm);
            else st_install(nm);
        }
    }
    gfx_fill(0, 0, (int)gfx_w, (int)gfx_h, gfx_rgb(0, 0, 0));
    gfx_present();
    gfx_active = 0;
    ST->ConOut->ClearScreen(ST->ConOut);
    ST->ConOut->SetMode(ST->ConOut, ST->ConOut->Mode->Mode);
    if (gfx_back) { ST->BootServices->FreePool(gfx_back); gfx_back = NULL; }
}

