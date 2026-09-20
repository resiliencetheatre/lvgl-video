#include "display.h"
#include "ui.h"
#include "camera.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void stop(int signal_number)
{
    (void)signal_number;
    running = 0;
}

int main(void)
{
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    lv_init();

    lv_display_t *display = app_display_create();
    if (!display) {
        fprintf(stderr, "Unable to initialize lvgl-video display\n");
        lv_deinit();
        return EXIT_FAILURE;
    }
    app_ui_create(lv_display_get_screen_active(display));

    while (running && app_display_is_open(display)) {
        /* Both display drivers supply LVGL's monotonic tick source. */
        app_camera_poll();
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms == LV_NO_TIMER_READY || delay_ms > 20) delay_ms = 20;
        if (delay_ms < 1) delay_ms = 1;
        usleep(delay_ms * 1000);
    }

    app_camera_stop();
    lv_deinit();
    return EXIT_SUCCESS;
}
