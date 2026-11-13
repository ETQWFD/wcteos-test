#ifndef WCT_APPS_H
#define WCT_APPS_H

/* =========================================================
   1. 终端 (Terminal)
   ========================================================= */
static void term_putc(GuiWin *w, char c) {
    if (c == '\r') { w->tcol = 0; return; } 
    if (c == '\n') { w->text[w->trow][w->tcol] = 0; ++w->trow; w->tcol = 0; if (w->trow >= WIN_MAX_LINES) { for (int r = 1; r < WIN_MAX_LINES; ++r) memcpy(w->text[r - 1], w->text[r], WIN_COLS + 1); w->trow = WIN_MAX_LINES - 1; w->text[w->trow][0] = 0; } return; }
    if (c == 8) { if (w->tcol) w->text[w->trow][--w->tcol] = 0; return; } 
    if (c < 32 || c > 126) c = '?';
    if (w->tcol >= WIN_COLS) { w->text[w->trow][WIN_COLS] = 0; ++w->trow; w->tcol = 0; if (w->trow >= WIN_MAX_LINES) { w->trow = WIN_MAX_LINES - 1; w->text[w->trow][0] = 0; } }
    w->text[w->trow][w->tcol++] = c; w->text[w->trow][w->tcol] = 0;
}
static void term_execute(GuiWin *w) {
    w->input[w->input_len] = 0; 
    for (int i = 0; i < w->input_len; ++i) term_putc(w, w->input[i]); 
    term_putc(w, '\n');
    if (!scmp(w->input, "exit")) { win_close(focus_win); return; }
    if (!scmp(w->input, "exitgui")) { gfx_done = 1; return; }
    if (w->input_len) {
        CHAR16 wide[96]; 
        for (int i = 0; i <= w->input_len; ++i) wide[i] = (CHAR16)(unsigned char)w->input[i];
        hist_pos = hist_count; active_tty = 2; gfx_active = 1; gfx_term_target = w; gui_cap_len = 0; gui_capture = 1;
        process_command(wide);
        gui_capture = 0; gui_capture_flush(w); w->scroll = 0; gfx_term_target = NULL; active_tty = 1; term_putc(w, '\n');
    }
    w->input_len = 0; w->input[0] = 0;
}
static void term_draw(GuiWin *w) {
    gfx_fill(w->x + 2, w->y + 30, w->w - 4, w->h - 30, gfx_rgb(22, 24, 28));
    int rows = (w->h - 80) / 16; if (rows < 2) rows = 2; if (rows > WIN_VIEW_LINES) rows = WIN_VIEW_LINES;
    int start = w->trow - rows + 1 - w->scroll; if (start < 0) start = 0; if (start > w->trow) start = w->trow;
    for (int r = 0; r < rows; ++r) { int line = start + r; if (line >= 0 && line <= w->trow) gfx_text_n(w->x + 8, w->y + 40 + r * 16, w->text[line], WIN_COLS, gfx_rgb(214,220,228)); }
    int used = 0; for (int r = 0; r < rows; ++r) { int line = start + r; if (line >= 0 && line <= w->trow) used = r + 1; }
    int yy = w->y + 40 + used * 16; if (yy > w->y + w->h - 24) yy = w->y + w->h - 24;
    gfx_text(w->x + 8, yy, "tty2>", gfx_rgb(126, 200, 132)); gfx_text(w->x + 80, yy, w->input, gfx_rgb(224, 230, 238));
    gfx_fill(w->x + 80 + w->input_len * 8, yy + 14, 7, 2, gfx_rgb(224, 230, 238));
}
static void term_key(GuiWin *w, EFI_INPUT_KEY key) {
    if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n' || key.ScanCode == 0x0D) term_execute(w);
    else if (key.UnicodeChar == 8) { if (w->input_len) w->input[--w->input_len] = 0; }
    else if (key.UnicodeChar >= 32 && key.UnicodeChar < 127 && w->input_len < 95) { w->input[w->input_len++] = (char)key.UnicodeChar; w->input[w->input_len] = 0; }
}

/* =========================================================
   2. 文件管理器 (Files)
   ========================================================= */
static void win_load_files(GuiWin *w) {
    w->entry_count = 0; EFI_FILE_PROTOCOL *dir = NULL; if (!open_path(CWD, &dir, EFI_FILE_MODE_READ, 0)) { w->loaded = 1; return; }
    static UINT8 buf[2048];
    for (int guard = 0; guard < WIN_ENTRIES + 4; ++guard) {
        UINTN sz = sizeof(buf); if (EFI_IS_ERROR(dir->Read(dir, &sz, buf)) || sz == 0) break; EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf; if (!info->FileName[0]) break;
        if (weq(info->FileName, L".") || weq(info->FileName, L"..")) continue;
        GuiEnt *e = &w->entries[w->entry_count++]; wcopy(e->name, 64, info->FileName); e->dir = (info->Attribute & EFI_FILE_DIRECTORY) ? 1 : 0; e->size = info->FileSize; if (w->entry_count >= WIN_ENTRIES) break;
    }
    dir->Close(dir); w->loaded = 1;
}
static void files_draw(GuiWin *w) {
    if (!w->loaded) win_load_files(w);
    int x = w->x + 8, y = w->y + 38;
    gfx_text_w(x, y, T(S_PATH), gfx_dim); gfx_text_w(x + 56, y, CWD, gfx_text_c);
    ui_btn(x, y + 24, 64, 24, T(S_BACK), 0);
    for (int row = 0; row < w->entry_count; ++row) {
        int yy = y + 62 + row * 20; if (yy + 16 > w->y + w->h - 44) break;
        if (row == w->sel) gfx_fill(x, yy - 2, w->w - 16, 18, gfx_rgb(215,208,192));
        gfx_text_w(x + 18, yy, w->entries[row].name, w->entries[row].dir ? gfx_accent : gfx_text_c);
    }
    const CHAR16 *lbl[3] = { T(S_PROP), T(S_DELETE), T(S_REFRESH) };
    for (int i = 0; i < 3; ++i) ui_btn(w->x + 8 + i * 70, w->y + w->h - 32, 62, 22, lbl[i], 0);
}
static void files_click(GuiWin *w, int lx, int ly) {
    if (lx >= 8 && lx < 72 && ly >= 62 && ly < 86) {
        CHAR16 parent[256]; normalize_path(L"..", parent, sizeof(parent) / sizeof(parent[0]));
        wcopy(CWD, sizeof(CWD) / sizeof(CWD[0]), parent); w->loaded = 0; return;
    }
    if (ly >= 98) {
        int row = (ly - 98) / 20;
        if (row < w->entry_count) {
            w->sel = row; w->prop_show = 0; w->prop_msg = 0;
            CHAR16 next[256]; wcopy(next, 256, CWD); if (wlen(next) && next[wlen(next) - 1] != L'\\') wcat(next, 256, L"\\"); wcat(next, 256, w->entries[row].name);
            if (w->entries[row].dir) {
                EFI_FILE_PROTOCOL *chk = NULL;
                if (open_path(next, &chk, EFI_FILE_MODE_READ, 0)) { chk->Close(chk); if (wlen(next) < 255) { wcopy(CWD, 256, next); w->loaded = 0; } }
            } else {
                int elen = (int)wlen(next);
                if (elen >= 4 && weq(next + elen - 4, L".txt")) { int e = win_open(WIN_EDIT); if (e >= 0) { wcopy(wins[e].edit_path, 256, next); gedit_load(&wins[e]); } }
            }
        }
        return;
    }
    if (ly >= w->h - 34 && ly <= w->h - 8) {
        int bi = (lx - 8) / 70;
        if (bi == 1) { 
            if (w->sel >= 0 && w->sel < w->entry_count) {
                CHAR16 path[256]; wcopy(path, 256, CWD); if (wlen(path) && path[wlen(path) - 1] != L'\\') wcat(path, 256, L"\\"); wcat(path, 256, w->entries[w->sel].name);
                EFI_FILE_PROTOCOL *f = NULL;
                if (open_path(path, &f, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0)) { f->Delete(f); w->loaded = 0; w->sel = -1; }
            }
        }
        else if (bi == 2) { w->loaded = 0; w->prop_show = 0; }
    }
}

/* =========================================================
   3. 文本编辑器 (Editor)
   ========================================================= */
static void gedit_load(GuiWin *w) {
    w->edit_lines = 1; w->edit_row = w->edit_col = 0; w->edit[0][0] = 0;
    static UINT8 data[8192]; UINTN n = read_whole_file(w->edit_path, data, sizeof(data) - 1);
    if (!n) { w->edit_loaded = 1; return; }
    int line = 0, col = 0;
    for (UINTN i = 0; i < n && line < 80; ++i) { char c = (char)data[i]; if (c == '\r') continue; if (c == '\n') { w->edit[line][col] = 0; ++line; col = 0; if (line < 80) w->edit[line][0] = 0; continue; } if (col < 79) w->edit[line][col++] = c; }
    w->edit[line][col] = 0; w->edit_lines = line + 1; w->edit_loaded = 1;
}
static void gedit_draw(GuiWin *w) {
    gfx_fill(w->x + 4, w->y + 32, w->w - 8, 18, gfx_panel); gfx_text_w(w->x + 8, w->y + 36, w->edit_path, gfx_accent);
    for (int r = 0; r < w->edit_lines && r < 26; ++r) { int yy = w->y + 58 + r * 16; if (yy + 16 > w->y + w->h - 24) break; gfx_text(w->x + 8, yy, w->edit[r], gfx_text_c); if (r == w->edit_row) gfx_fill(w->x + 8 + w->edit_col * 8, yy + 14, 7, 2, gfx_accent); }
}
static int gedit_len(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void gedit_key_cb(GuiWin *w, EFI_INPUT_KEY key) {
    if (key.UnicodeChar == 19) { 
        static char out[8192]; UINTN n = 0;
        for (int r = 0; r < w->edit_lines && n + 82 < sizeof(out); ++r) {
            for (int c = 0; w->edit[r][c] && n + 1 < sizeof(out); ++c) out[n++] = w->edit[r][c];
            out[n++] = '\n';
        }
        if (write_bytes(w->edit_path, out, n)) w->edit_loaded = 2;
        return; 
    }
    if (key.ScanCode == 0x01) { if (w->edit_row > 0) --w->edit_row; w->edit_col = gedit_len(w->edit[w->edit_row]); return; }
    if (key.ScanCode == 0x02) { if (w->edit_row + 1 < w->edit_lines) ++w->edit_row; w->edit_col = gedit_len(w->edit[w->edit_row]); return; }
    if (key.ScanCode == 0x03) { if (w->edit_col > 0) --w->edit_col; return; }
    if (key.ScanCode == 0x04) { if (w->edit_col < gedit_len(w->edit[w->edit_row])) ++w->edit_col; return; }

    if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n' || key.ScanCode == 0x0D) {
        if (w->edit_lines < 80) { w->edit[w->edit_row][w->edit_col] = 0; ++w->edit_row; w->edit_col = 0; w->edit[w->edit_row][0] = 0; if (w->edit_row >= w->edit_lines) w->edit_lines = w->edit_row + 1; }
        return;
    }
    if (key.UnicodeChar == 8 && w->edit_col > 0) { w->edit[w->edit_row][--w->edit_col] = 0; return; }
    if (key.UnicodeChar >= 32 && key.UnicodeChar < 127 && w->edit_col < 79) { w->edit[w->edit_row][w->edit_col++] = (char)key.UnicodeChar; w->edit[w->edit_row][w->edit_col] = 0; }
}

/* =========================================================
   4. 设置 / 任务管理器 / 关于 / 运行框
   ========================================================= */
static void set_draw(GuiWin *w) {
    gfx_text_w(w->x + 16, w->y + 40, T(S_THEME), gfx_dim);
    const CHAR16 *names[6] = { T(S_PURPLE), T(S_GREEN), T(S_MATRIX), T(S_AMBER), T(S_BLUE), T(S_PAPER) };
    for (int i = 0; i < 6; ++i) ui_btn(w->x + 16 + i * 88, w->y + 60, 80, 26, names[i], theme_index == i);
    gfx_text_w(w->x + 16, w->y + 104, T(S_WALLPAPER), gfx_dim);
    const CHAR16 *wp[6] = { T(S_SOLID), T(S_GRADIENT), T(S_GRID), T(S_GLF), T(S_PAPER_TEX), T(S_STARRY) };
    for (int i = 0; i < 6; ++i) ui_btn(w->x + 16 + i * 94, w->y + 124, 88, 26, wp[i], ui_wp == i);
    gfx_text_w(w->x + 16, w->y + 196, T(S_LANG), gfx_dim);
    ui_btn(w->x + 16 + 72, w->y + 190, 80, 26, T(S_LANG_ZH), ui_lang == 0);
    ui_btn(w->x + 16 + 72 + 88, w->y + 190, 80, 26, T(S_LANG_EN), ui_lang == 1);
}
static void set_click(GuiWin *w, int lx, int ly) {
    for (int i = 0; i < 6; ++i) if (lx >= 16 + i * 88 && lx < 16 + i * 88 + 80 && ly >= 60 && ly < 86) { theme_index = i; apply_theme(); return; }
    for (int i = 0; i < 6; ++i) if (lx >= 16 + i * 94 && lx < 16 + i * 94 + 88 && ly >= 124 && ly < 150) { ui_wp = i; return; }
    if (lx >= 16 + 72 && lx < 16 + 72 + 80 && ly >= 190 && ly < 216) { ui_lang = 0; return; }
    if (lx >= 16 + 72 + 88 && lx < 16 + 72 + 88 + 80 && ly >= 190 && ly < 216) { ui_lang = 1; return; }
}

static void task_draw(GuiWin *w) {
    gfx_text_w(w->x + 16, w->y + 38, T(S_TM_NAME), gfx_dim); gfx_text_w(w->x + 340, w->y + 38, T(S_TM_STATUS), gfx_dim);
    int n = proc_count; if (n > 7) n = 7;
    for (int i = 0; i < n; ++i) {
        Proc *pp = &procs[i]; int ry = w->y + 60 + i * 22;
        gfx_fill(w->x + 12, ry - 4, w->w - 24, 20, i == ui_task_sel ? gfx_accent : ((i & 1) ? gfx_panel : gfx_rgb(12, 18, 32)));
        char nm[28]; int q = 0; if (pp->pid >= 10) nm[q++] = (char)('0' + (pp->pid / 10) % 10); nm[q++] = (char)('0' + pp->pid % 10); nm[q++] = ' ';
        for (int t2 = 0; pp->name[t2] && q < 27; ++t2) nm[q++] = pp->name[t2]; nm[q] = 0;
        gfx_text(w->x + 16, ry, nm, gfx_text_c); gfx_text(w->x + 340, ry, pp->state, gfx_accent);
    }
    ui_btn(w->x + 16, w->y + w->h - 44, 150, 26, T(S_TM_NEW), 0); ui_btn(w->x + 180, w->y + w->h - 44, 120, 26, T(S_TM_KILL), 0);
}
static void task_click(GuiWin *w, int lx, int ly) {
    int rows = proc_count; if (rows > 7) rows = 7;
    for (int i = 0; i < rows; ++i) if (lx >= 12 && lx < w->w - 12 && ly >= 60 + i * 22 - 4 && ly < 60 + i * 22 + 16) { ui_task_sel = i; return; }
    if (lx >= 16 && lx < 166 && ly >= w->h - 44 && ly < w->h - 18) { win_open(WIN_RUN); return; }
    if (lx >= 180 && lx < 300 && ly >= w->h - 44 && ly < w->h - 18) { if (ui_task_sel >= 0 && ui_task_sel < proc_count && procs[ui_task_sel].pid != 1) scopy(procs[ui_task_sel].state, sizeof(procs[ui_task_sel].state), "killed"); ui_task_sel = -1; return; }
}

static void about_draw(GuiWin *w) {
    gfx_text(w->x + 20, w->y + 48, "WCT-OS 1.0 LTS", gfx_accent); gfx_text_w(w->x + 20, w->y + 76, T(S_KERNEL), gfx_text_c); gfx_text_w(w->x + 20, w->y + 96, T(S_ARCH), gfx_text_c);
    gfx_text_w(w->x + 20, w->y + 116, T(S_CPU), gfx_dim); gfx_text_w(w->x + 20, w->y + 156, T(S_BUILD), gfx_dim); gfx_text_w(w->x + 20, w->y + 176, T(S_INSTALL), gfx_dim); gfx_text_w(w->x + 20, w->y + 200, T(S_GUI_BY), gfx_accent);
}

static void run_draw(GuiWin *w) {
    gfx_fill(w->x + 14, w->y + 42, w->w - 28, 28, gfx_rgb(4, 7, 14)); gfx_border(w->x + 14, w->y + 42, w->w - 28, 28, gfx_accent);
    gfx_text(w->x + 20, w->y + 48, ui_input, gfx_text_c); gfx_fill(w->x + 20 + ui_len * 8, w->y + 46, 2, 18, gfx_accent);
    ui_btn(w->x + 14, w->y + w->h - 40, 150, 26, ui_admin ? T(S_ADMIN_ON) : T(S_ADMIN_OFF), ui_admin);
    ui_btn(w->x + w->w - 200, w->y + w->h - 40, 84, 26, T(S_OK), 0); ui_btn(w->x + w->w - 108, w->y + w->h - 40, 84, 26, T(S_CANCEL), 0);
}
static void run_click(GuiWin *w, int lx, int ly) {
    if (lx >= 14 && lx < 164 && ly >= w->h - 40 && ly < w->h - 14) { ui_admin = !ui_admin; return; }
    if (lx >= w->w - 200 && lx < w->w - 116 && ly >= w->h - 40 && ly < w->h - 14) {
        CHAR16 wide[96]; for (int i = 0; i <= ui_len; ++i) wide[i] = (CHAR16)ui_input[i];
        ui_len = 0; ui_input[0] = 0; win_close(w->type);
        int t = win_open(WIN_TERM);
        if (t >= 0) { GuiWin *tw = &wins[t]; hist_pos = hist_count; active_tty = 2; gfx_active = 1; gfx_term_target = tw; gui_cap_len = 0; gui_capture = 1; process_command(wide); gui_capture = 0; gui_capture_flush(tw); gfx_term_target = NULL; active_tty = 1; term_putc(tw, '\n'); }
        return;
    }
    if (lx >= w->w - 108 && lx < w->w - 24 && ly >= w->h - 40 && ly < w->h - 14) { win_close(w->type); return; }
}

/* =========================================================
   5. 计算器 (Calculator) - 插件演示
   ========================================================= */

// 简单的表达式求值器（只支持加减乘除）
static int calc_eval(const char *s) {
    int res = 0, curr = 0;
    char op = '+';
    for (int i = 0; s[i]; ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            curr = curr * 10 + (s[i] - '0');
        }
        if (s[i] == '+' || s[i] == '-' || s[i] == '*' || s[i] == '/' || s[i] == 0) {
            if (op == '+') res += curr;
            else if (op == '-') res -= curr;
            else if (op == '*') res *= curr;
            else if (op == '/') { if (curr != 0) res /= curr; else res = 0; }
            op = s[i];
            curr = 0;
        }
    }
    return res;
}

static void calc_draw(GuiWin *w) {
    // 显示区域
    gfx_fill(w->x + 20, w->y + 45, w->w - 40, 40, gfx_rgb(240, 240, 240));
    gfx_border(w->x + 20, w->y + 45, w->w - 40, 40, gfx_rgb(100, 100, 100));
    gfx_text(w->x + 30, w->y + 58, w->input, gfx_rgb(0, 0, 0));
    
    // 闪烁光标
    if ((w->trow++ / 20) % 2 == 0) { // 利用 trow 当作简易计时器
        gfx_fill(w->x + 30 + w->input_len * 8, w->y + 56, 2, 18, gfx_rgb(0, 0, 0));
    }

    // 绘制 16 个按钮 (4x4)
    const CHAR16 *btns[16] = { 
        L"7", L"8", L"9", L"/", 
        L"4", L"5", L"6", L"*", 
        L"1", L"2", L"3", L"-", 
        L"0", L"C", L"=", L"+" 
    };
    for (int i = 0; i < 16; ++i) {
        int r = i / 4, c = i % 4;
        int bx = w->x + 20 + c * 70;
        int by = w->y + 100 + r * 50;
        ui_btn(bx, by, 60, 40, btns[i], 0);
    }
}

static void calc_click(GuiWin *w, int lx, int ly) {
    if (lx < 20 || lx >= w->w - 20 || ly < 100) return;
    int c = (lx - 20) / 70;
    int r = (ly - 100) / 50;
    if (c > 3 || r > 3) return;
    
    int idx = r * 4 + c;
    const CHAR16 *btns[16] = { 
        L"7", L"8", L"9", L"/", 
        L"4", L"5", L"6", L"*", 
        L"1", L"2", L"3", L"-", 
        L"0", L"C", L"=", L"+" 
    };

    if (idx == 13) { // 清空 (C)
        w->input_len = 0;
        w->input[0] = 0;
    } else if (idx == 14) { // 计算 (=)
        int result = calc_eval(w->input);
        // 将结果转回字符串
        int n = 0, v = result;
        if (v == 0) { w->input[n++] = '0'; }
        else {
            if (v < 0) { w->input[n++] = '-'; v = -v; }
            char tmp[16]; int t = 0;
            while (v) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
            while (t) w->input[n++] = tmp[--t];
        }
        w->input[n] = 0;
        w->input_len = n;
    } else { // 追加字符
        if (w->input_len < 20) {
            w->input[w->input_len++] = (char)btns[idx][0];
            w->input[w->input_len] = 0;
        }
    }
}


/* =========================================================
   🚀 应用实例注册 (在这里添加新应用即可)
   ========================================================= */
static const AppDef APP_FILES = { WIN_FILES, L"文件管理器", L"Files", 600, 400, ico_folder, files_draw, files_click, NULL };
static const AppDef APP_TERM  = { WIN_TERM,  L"终端",       L"Terminal", 600, 400, ico_term, term_draw, NULL, term_key };
static const AppDef APP_EDIT  = { WIN_EDIT,  L"文本编辑器", L"Editor", 600, 400, ico_edit, gedit_draw, NULL, gedit_key_cb };
static const AppDef APP_SET   = { WIN_SET,   L"设置",       L"Settings", 600, 380, ico_set, set_draw, set_click, NULL };
static const AppDef APP_TASK  = { WIN_TASK,  L"任务管理器", L"TaskMgr", 600, 360, ico_task, task_draw, task_click, NULL };
static const AppDef APP_ABOUT = { WIN_ABOUT, L"关于",       L"About", 560, 320, ico_info, about_draw, NULL, NULL };
static const AppDef APP_RUN   = { WIN_RUN,   L"运行",       L"Run", 520, 176, ico_info, run_draw, run_click, term_key };
static const AppDef APP_CALC = { 100, L"计算器", L"Calculator", 320, 400, ico_calc, calc_draw, calc_click, NULL };
#endif // WCT_APPS_H