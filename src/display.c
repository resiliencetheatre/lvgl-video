#include "display.h"
#include <stdlib.h>
#if APP_USE_WAYLAND
#include "src/drivers/wayland/lv_wayland.h"
#else
#include "src/drivers/evdev/lv_evdev.h"
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif

lv_display_t *app_display_create(void)
{
#if APP_USE_WAYLAND
    /* Initial size only. XDG configure events apply the kiosk output size. */
    lv_display_t *display = lv_wayland_window_create(800, 480, "lvgl-video", NULL);
    if (!display) return NULL;
    lv_wayland_window_set_fullscreen(display, true);
    /* Apply any initial kiosk resize before the UI computes widget sizes. */
    lv_refr_now(display);
#else
    const char *device = getenv("LV_LINUX_FBDEV_DEVICE");
    lv_display_t *display = lv_linux_fbdev_create();
    if (!display) return NULL;
    if (lv_linux_fbdev_set_file(display, device ? device : "/dev/fb0") != LV_RESULT_OK) {
        lv_display_delete(display);
        return NULL;
    }
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/touchscreen");
    if (touch) lv_indev_set_display(touch, display);
#endif
    return display;
}

bool app_display_is_open(lv_display_t *display)
{
#if APP_USE_WAYLAND
    return lv_wayland_window_is_open(display);
#else
    return display != NULL;
#endif
}
