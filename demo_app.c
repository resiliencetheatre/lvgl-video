#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "demo_app.h"

const uint16_t WINDOW_WIDTH = 640;
const uint16_t WINDOW_HEIGHT = 380;
const bool FULLSCREEN = false;
const bool MAXIMIZE = false;

const char *getenv_default(const char *name, const char *dflt)
{
    return getenv(name) ?: dflt;
}

#if LV_USE_LINUX_FBDEV
void lv_linux_disp_init(void)
{
    const char *device = getenv_default("LV_LINUX_FBDEV_DEVICE", "/dev/fb0");
    lv_display_t *disp = lv_linux_fbdev_create();

    lv_linux_fbdev_set_file(disp, device);
    
    printf("Creating touch: /dev/input/event2 \n");
    printf("Check with 'evtest' if needed. \n");
    printf("Disable system console output with command: \n");
    printf("echo 0 > /sys/class/vtconsole/vtcon1/bind \n");
    
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
    lv_indev_set_display(touch, disp);
}

#elif LV_USE_LINUX_DRM
void lv_linux_disp_init(void)
{
    const char *device = getenv_default("LV_LINUX_DRM_CARD", "/dev/dri/card0");
    lv_display_t *disp = lv_linux_drm_create();

    lv_linux_drm_set_file(disp, device, -1);
}

#elif LV_USE_SDL
void lv_linux_disp_init(void)
{
    lv_sdl_window_create(WINDOW_WIDTH, WINDOW_HEIGHT);
}

#elif LV_USE_WAYLAND
void lv_linux_disp_init(void)
{
    lv_display_t *disp;
    lv_group_t *g;
    disp = lv_wayland_window_create(WINDOW_WIDTH, WINDOW_HEIGHT, "LVGL Simulator", NULL);

    if (FULLSCREEN)
    {
        lv_wayland_window_set_fullscreen(disp, FULLSCREEN);
    }
    else if (MAXIMIZE)
    {
        lv_wayland_window_set_maximized(disp, MAXIMIZE);
    }

    g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(lv_wayland_get_keyboard(disp), g);
    lv_indev_set_group(lv_wayland_get_pointeraxis(disp), g);
}
#else
#error Unsupported configuration
#endif

#if LV_USE_WAYLAND
void lv_demo_run_loop(void)
{
    bool completed;

    /* Handle LVGL tasks */
    while (1)
    {

        completed = lv_wayland_timer_handler();

        if (completed)
        {
            /* wait only if the cycle was completed */
            usleep(LV_DEF_REFR_PERIOD * 1000);
        }

        /* Run until the last window closes */
        if (!lv_wayland_window_is_open(NULL))
        {
            break;
        }
    }
}
#else
void lv_demo_run_loop(void)
{
    /*Handle LVGL tasks*/
    while (1)
    {
        lv_timer_handler();
        usleep(5000);
    }
}
#endif
