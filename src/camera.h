#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "lvgl.h"

/* All calls run on the LVGL thread; GStreamer owns capture threads. */
void app_camera_start(lv_obj_t *image, lv_obj_t *status);
void app_camera_poll(void);
void app_camera_stop(void);

#endif
