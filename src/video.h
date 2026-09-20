#ifndef APP_VIDEO_H
#define APP_VIDEO_H
#include "lvgl.h"
#include <gst/app/gstappsink.h>

typedef struct {
    lv_obj_t *image;
    lv_image_dsc_t frame;
    uint8_t pixels[640 * 480 * 2];
} AppVideo;
/* Nonblocking; called only from the LVGL thread. */
gboolean app_video_update(AppVideo *view, GstAppSink *sink);
#endif
