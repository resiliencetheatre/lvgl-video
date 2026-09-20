#ifndef APP_UI_H
#define APP_UI_H
#include "lvgl.h"
#include "session.h"
void app_ui_create(lv_obj_t *screen, AppSession *session);
void app_ui_poll(void);
#endif
