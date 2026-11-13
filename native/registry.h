#ifndef WCT_REGISTRY_H
#define WCT_REGISTRY_H

/* 
 * WCT-OS 应用注册中心
 * 如果你想添加新软件，请务必在 apps.h 中实现它并实例化 AppDef，
 * 然后在这里加上 &APP_你的应用名 即可！
 */
static const AppDef* g_apps[] = {
    &APP_FILES, 
    &APP_TERM, 
    &APP_EDIT, 
    &APP_SET, 
    &APP_TASK, 
    &APP_ABOUT, 
    &APP_RUN,
    &APP_CALC    
};

const int APP_COUNT = sizeof(g_apps) / sizeof(g_apps[0]);

#endif // WCT_REGISTRY_H