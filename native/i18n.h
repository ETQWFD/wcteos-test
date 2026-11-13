#ifndef WCT_I18N_H
#define WCT_I18N_H

#include "efi.h"

/* ---------- i18n Enum ---------- */
enum {
    S_FILES, S_TERM, S_EDITOR, S_SETTINGS, S_TASKMGR, S_SYSINFO,
    S_START, S_RUN, S_ABOUT, S_LOCK, S_REBOOT, S_SHUTDOWN,
    S_PATH, S_BACK, S_PROP, S_DELETE, S_REFRESH,
    S_NAME, S_TYPE, S_FOLDER, S_FILE, S_SIZE, S_BYTES,
    S_PROTECTED, S_DELETABLE, S_DELETED, S_ACCESS_DENIED, S_DELETE_FAILED,
    S_SAVED_HINT, S_SAVE_HINT,
    S_THEME, S_PURPLE, S_GREEN, S_MATRIX, S_AMBER, S_BLUE, S_PAPER,
    S_WALLPAPER, S_SOLID, S_GRADIENT, S_GRID, S_GLF, S_PAPER_TEX, S_STARRY,
    S_GLF_OK, S_GLF_MISS, S_SYSINFO_LABEL, S_SYSINFO_LINE,
    S_LANG, S_LANG_ZH, S_LANG_EN, S_CLOSE,
    S_ABOUT_TITLE, S_KERNEL, S_ARCH, S_CPU, S_DISK_PCI, S_BUILD, S_INSTALL, S_GUI_BY,
    S_TM_NAME, S_TM_TYPE, S_TM_STATUS, S_TM_DETAIL, S_TM_PROCS, S_TM_HINT, S_TM_NEW, S_TM_KILL,
    S_LOCKED, S_UNLOCK_HINT,
    S_RUN_TITLE, S_RUN_TITLE_ADMIN, S_RUN_HINT, S_ADMIN_ON, S_ADMIN_OFF, S_OK, S_CANCEL,
    S_DESKTOP, S_REFRESH_DESK, S_NEW_FOLDER, S_NEW_TXT,
    S_GUI_TAG,
    S_COUNT
};

/* ---------- i18n 字符串表 ---------- */
static const CHAR16 *i18n_zh[S_COUNT] = {
    L"文件管理器", L"终端", L"文本编辑器", L"设置", L"任务管理器", L"系统属性",
    L"开始", L"运行", L"关于", L"锁屏", L"重启", L"关机",
    L"路径：", L"返回", L"属性", L"删除", L"刷新",
    L"名称", L"类型", L"文件夹", L"文件", L"大小", L"字节",
    L"系统文件，受保护", L"普通文件，可删除",
    L"已删除", L"拒绝访问", L"删除失败",
    L"已保存。Ctrl+S 保存", L"Ctrl+S 保存",
    L"主题颜色", L"紫", L"绿", L"矩阵绿", L"琥珀", L"蓝", L"纸片",
    L"壁纸", L"纯色", L"渐变", L"网格", L"动态", L"纸纹", L"星夜",
    L"已加载", L"未找到",
    L"系统信息", L"WCT-OS 1.0 LTS  |  WCT 原生内核  |  x86_64 UEFI",
    L"语言", L"中文", L"English", L"关闭",
    L"系统属性",
    L"内核：WCT UEFI 原生内核",
    L"架构：x86_64",
    L"处理器：", L"磁盘：", L"编译：2026-11-10  |  api: 1",
    L"安装时间：见 \\wct\\install.txt",
    L"GUI: WCT Native Window Manager",
    L"名称", L"类型", L"状态", L"详情",
    L"进程：", L"选中后按结束进程。",
    L"新建进程", L"结束进程",
    L"WCT-OS 已锁定", L"点击鼠标解锁",
    L"运行", L"运行（管理员）",
    L"wtr 终端 | wtcer 属性",
    L"管理员：开", L"管理员：关", L"确定", L"取消",
    L"桌面", L"刷新桌面", L"新建文件夹", L"新建文本文档",
    L"GUI by aqtwWCT95",
};

static const CHAR16 *i18n_en[S_COUNT] = {
    L"Files", L"Terminal", L"Editor", L"Settings", L"TaskMgr", L"System",
    L"Start", L"Run", L"About", L"Lock", L"Reboot", L"Shutdown",
    L"Path:", L"Back", L"Properties", L"Delete", L"Refresh",
    L"Name", L"Type", L"Folder", L"File", L"Size", L"bytes",
    L"Protected system file", L"Deletable",
    L"Deleted", L"Access denied", L"Delete failed",
    L"Saved. Ctrl+S save", L"Ctrl+S save",
    L"Theme", L"Purple", L"Green", L"Matrix", L"Amber", L"Blue", L"Paper",
    L"Wallpaper", L"Solid", L"Gradient", L"Grid", L"Dynamic", L"Paper", L"Starry",
    L"Loaded", L"Not found",
    L"System info", L"WCT-OS 1.0 LTS  |  WCT native kernel  |  x86_64 UEFI",
    L"Language", L"中文", L"English", L"Close",
    L"System Properties",
    L"Kernel: WCT UEFI native kernel",
    L"Arch: x86_64",
    L"CPU :", L"Disks:", L"Build: 2026-11-10  |  api: 1",
    L"Installed: see \\wct\\install.txt",
    L"GUI: WCT Native Window Manager",
    L"Name", L"Type", L"Status", L"Detail",
    L"Procs:", L"Select row to end process.",
    L"New Process", L"End Process",
    L"WCT-OS locked", L"Click mouse to unlock",
    L"Run", L"Run (Admin)",
    L"wtr terminal | wtcer about",
    L"Admin: On", L"Admin: Off", L"OK", L"Cancel",
    L"Desktop", L"Refresh Desktop", L"New Folder", L"New Text File",
    L"GUI by aqtwWCT95",
};

/* 语言标志位和宏 */
static int ui_lang = 0;
#define T(id) (ui_lang ? i18n_en[(id)] : i18n_zh[(id)])

#endif // WCT_I18N_H