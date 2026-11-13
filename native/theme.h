#ifndef WCT_THEME_H
#define WCT_THEME_H

extern int theme_index;

static UINT32 gfx_bg, gfx_top, gfx_panel, gfx_text_c, gfx_dim, gfx_accent;

static void apply_theme(void) { 
    switch (theme_index) { 
    case 0: gfx_bg=gfx_rgb(15,10,25); gfx_top=gfx_rgb(60,20,80); gfx_panel=gfx_rgb(30,25,45); gfx_accent=gfx_rgb(205,125,255); break; 
    case 1: gfx_bg=gfx_rgb(5,15,10); gfx_top=gfx_rgb(10,45,25); gfx_panel=gfx_rgb(15,32,22); gfx_accent=gfx_rgb(55,220,115); break; 
    case 2: gfx_bg=gfx_rgb(0,10,0); gfx_top=gfx_rgb(0,40,0); gfx_panel=gfx_rgb(0,25,0); gfx_accent=gfx_rgb(0,255,0); break; 
    case 3: gfx_bg=gfx_rgb(15,10,0); gfx_top=gfx_rgb(60,35,0); gfx_panel=gfx_rgb(35,25,5); gfx_accent=gfx_rgb(255,190,60); break; 
    case 5: gfx_bg=gfx_rgb(226,222,212); gfx_top=gfx_rgb(250,248,241); gfx_panel=gfx_rgb(246,243,235); gfx_accent=gfx_rgb(52,86,138); break; 
    default: gfx_bg=gfx_rgb(5,10,20); gfx_top=gfx_rgb(20,45,70); gfx_panel=gfx_rgb(15,28,45); gfx_accent=gfx_rgb(100,210,255); break; 
    } 
    if (theme_index == 5) { gfx_text_c = gfx_rgb(38,36,34); gfx_dim = gfx_rgb(120,114,106); } 
    else { gfx_text_c = gfx_rgb(235,235,245); gfx_dim = gfx_rgb(145,145,165); } 
}

#endif