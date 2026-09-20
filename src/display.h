#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H
#include "lvgl.h"
/* The backend owns native resources; the UI owns widgets. */
lv_display_t *app_display_create(void);
bool app_display_is_open(lv_display_t *display);
#endif
