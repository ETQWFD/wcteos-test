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
static int ins_write(void *b, void *m, UINT64 lba, UINTN cnt, void *buf) {
    INS_WR wr = *(INS_WR *)((UINT8 *)b + 32);
    return (int)wr(b, *(UINT32 *)m, lba, cnt * 512, buf);
}
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
    UINT64 pstart = 2048, psec = total - pstart;
    sec[446] = 0x00; sec[450] = 0x0C;
    *(UINT32 *)(sec + 454) = (UINT32)pstart;
    *(UINT32 *)(sec + 458) = (UINT32)psec;
    sec[510] = 0x55; sec[511] = 0xAA;
    if (!ins_write(b, m, 0, 1, sec)) return 0;
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
    if (!ins_write(b, m, pstart, 1, sec)) return 0;
    if (!ins_write(b, m, pstart + 6, 1, sec)) return 0;
    *pct = 60; ins_progress("Formatting disk...", *pct);
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    *(UINT32 *)(sec + 0) = 0x41615252;
    *(UINT32 *)(sec + 484) = 0x61417272;
    UINT32 clus = (UINT32)((psec - 32 - 2ULL * fatsz) / 1);
    *(UINT32 *)(sec + 488) = clus;
    *(UINT32 *)(sec + 492) = 3;
    *(UINT32 *)(sec + 508) = 0xAA550000;
    if (!ins_write(b, m, pstart + 1, 1, sec)) return 0;
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    *(UINT32 *)(sec + 0) = 0x0FFFFFF8; *(UINT32 *)(sec + 4) = 0x0FFFFFFF;
    for (int f = 0; f < 2; ++f) if (!ins_write(b, m, pstart + 32 + (UINT64)f * fatsz, 1, sec)) return 0;
    for (int i = 0; i < 512; ++i) sec[i] = 0;
    UINT64 root = pstart + 32 + 2ULL * fatsz;
    if (!ins_write(b, m, root, 1, sec)) return 0;
    *pct = 90; ins_progress("Verifying...", *pct);
    ins_bio = b; ins_med = m;
    return 1;
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
static void cmd_installer(void) {
    if (EFI_IS_ERROR(ST->BootServices->LocateProtocol(&gop_guid, NULL, (VOID **)&gfx))) return;
    gfx_w = gfx->Mode->Info->HorizontalResolution; gfx_h = gfx->Mode->Info->VerticalResolution;
    gfx_stride = gfx->Mode->Info->PixelsPerScanLine; gfx_hw = (UINT32 *)(UINTN)gfx->Mode->FrameBufferBase;
    VOID *buf = NULL;
    if (!EFI_IS_ERROR(ST->BootServices->AllocatePool(EFI_LOADER_DATA, gfx_stride * gfx_h * 4, &buf)) && buf) gfx_back = (UINT32 *)buf;
    gfx_fb = gfx_back ? gfx_back : gfx_hw; gfx_active = 1;
    ST->ConOut->EnableCursor(ST->ConOut, FALSE);
    EFI_HANDLE disks[16]; UINT64 mb[16];
    int n = ins_disk_count(disks, 16, mb); int sel = 0;
    EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
    ST->BootServices->LocateProtocol(&sp_guid, NULL, (VOID **)&sp);
    int mx = (int)gfx_w / 2, my = (int)gfx_h / 2;
    ins_ui(sel, n, mb, 0, n ? "Mouse or arrow keys. Enter installs." : "No target disk found.");
    for (;;) {
        EFI_INPUT_KEY k;
        if (!EFI_IS_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &k))) {
            if (k.ScanCode == 0x01 && sel > 0) --sel;
            else if (k.ScanCode == 0x02 && sel + 1 < n) ++sel;
            else if (k.UnicodeChar == 27) break;
            else if (k.UnicodeChar == '\r') {
                int pct = 0;
                if (ins_format_disk(sel, disks, mb, &pct)) { *(&pct) = 100; ins_progress("Format complete. Ready to install.", 100); ST->BootServices->Stall(1200000); }
                else { ins_progress("Format failed.", 0); ST->BootServices->Stall(1200000); }
            }
            ins_ui(sel, n, mb, 0, n ? "Mouse or arrow keys. Enter installs." : "No target disk found.");
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
        ins_ui(sel, n, mb, 0, n ? "Mouse or arrow keys. Enter installs." : "No target disk found.");
        for (int ry = 0; ry < 14; ++ry) {
            int wdt = ry < 8 ? ry + 1 : (ry < 11 ? 14 - ry : 3);
            gfx_fill(mx, my + ry, wdt, 1, gfx_rgb(20, 20, 20));
            if (wdt > 2) gfx_fill(mx + 1, my + ry, wdt - 2, 1, gfx_rgb(255, 255, 255));
        }
        gfx_present();
        ins_ui(sel, n, mb, 0, n ? "Mouse or arrow keys. Enter installs." : "No target disk found.");
    }
    gfx_active = 0;
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