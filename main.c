/*
 * lvgl-app, small user interface demo with lvgl graphics library
 * 
 * MIT License
 * 
 * Copyright (c) 2025 Resilience Theatre
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * 
 * Includes ini library from https://github.com/univrsal/mini.c
 * * BSD-2-Clause license
 * 
 * Disable console output:
 * echo 0 > /sys/class/vtconsole/vtcon1/bind
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include "mini.h"
#include "log.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/evdev/lv_evdev.h"
#include "lvgl/src/widgets/scale/lv_scale.h"
#include "lvgl/src/widgets/line/lv_line.h"
#include "lvgl/src/widgets/label/lv_label.h"
#include "lvgl/src/stdlib/lv_string.h"


#define FIFO_IN  "/tmp/fifo_in"
#define FIFO_OUT "/tmp/fifo_out"
#define BRIGHTNESS_SYSFS_PATH "/sys/devices/platform/rpi_backlight/backlight/rpi_backlight/brightness"
#define FB_BLANK_PATH "/sys/class/graphics/fb0/blank"
#define TIMEOUT_SECONDS 60
#define NUM_SWITCHES 4
#define SWITCH_BACKLIGHT 1
#define SWITCH_BACKLIGHT_WAKEUP 2
#define SWITCH_PTT_TO_MACSEC 3
#define SWITCH_PTT_TO_WAN 4
#define RX_PATH "/sys/class/net/macsec0/statistics/rx_bytes"
#define TX_PATH "/sys/class/net/macsec0/statistics/tx_bytes"
#define SCALE_MAX_MBIT 100  // 100 Mbit/s

atomic_long last_touch_time;
atomic_bool backlight_off = false;
int g_backlight_timeout=0;
char timestamp[16];
lv_obj_t *uptime_label = NULL;
lv_obj_t *switch_objects[NUM_SWITCHES];
static lv_obj_t * message_log_ta = NULL;
static lv_obj_t * slider_label;
lv_obj_t *label_status_macsec_ip_address;
lv_obj_t *macsec_keyed_led;
lv_obj_t *macsec_routing_led;
lv_obj_t *wifi_led;
// macsec0 speed metes
static lv_obj_t *scale_tx, *scale_rx;
static lv_obj_t *needle_tx, *needle_rx;
static lv_obj_t *value_label_tx, *value_label_rx;

static void brightness_slider_event_callback(lv_event_t * e);
void show_notification(const char *msg);
void update_tx_rx_gauges(unsigned long tx_bps, unsigned long rx_bps);

typedef struct {
    const char *ini_key;
} text_context_t;

typedef struct {
    int button_id;
    lv_obj_t *target_screen; // unused
} button_data_t;

typedef struct {
    const char * name;
    lv_obj_t * label;
    int switch_id;
} switch_context_t;


int get_kernel_version(char *out, size_t out_size) {
    struct utsname uts;
    if (uname(&uts) != 0) {
        perror("uname");
        return -1;
    }

    snprintf(out, out_size, "%s %s %s",
             uts.sysname,
             uts.release,
             uts.version);

    return 0;
}

int get_mac_address(const char *iface_name, char *mac_out) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("ioctl");
        close(fd);
        return -1;
    }
    close(fd);

    unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
    snprintf(mac_out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return 0;
}

void run_route_script(void)
{
    int ret = system("/opt/macpipe/route.sh");

    if (ret == -1) {
        LV_LOG_ERROR("Failed to execute script");
    } else {
        int exit_status = WEXITSTATUS(ret);
        if (exit_status == 0) {
            LV_LOG_INFO("route.sh executed successfully");
            lv_led_on(macsec_routing_led);
        } else {
            LV_LOG_WARN("route.sh exited with status %d", exit_status);
            lv_led_off(macsec_routing_led);
        }
    }
}

int get_ip_address(const char *iface_name, char *ip_out) {
    struct ifaddrs *ifaddr, *ifa;
    int found = -1;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, iface_name) == 0) {
            
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            if (inet_ntop(AF_INET, &sa->sin_addr, ip_out, 16)) {
                found = 0;
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
    return found;
}

/* macsec0 statistic */
typedef struct {
    unsigned long tx;
    unsigned long rx;
} tx_rx_data_t;

static void lvgl_update_gauges_cb(void *param) {
    tx_rx_data_t *data = (tx_rx_data_t *)param;
    update_tx_rx_gauges(data->tx, data->rx);
    free(data);
}

static void create_single_scale(lv_obj_t *parent,
                                lv_obj_t **scale_out,
                                lv_obj_t **needle_out,
                                lv_obj_t **value_label_out,
                                const char *label_text)
{
    lv_obj_t *vcont = lv_obj_create(parent);
    lv_obj_set_size(vcont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(vcont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vcont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(vcont, 5, 0);
    lv_obj_clear_flag(vcont, LV_OBJ_FLAG_SCROLLABLE);    
    lv_obj_set_style_bg_opa(vcont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vcont, 0, 0);
    lv_obj_set_style_shadow_width(vcont, 0, 0);
    lv_obj_t *scale = lv_scale_create(vcont);
    lv_obj_set_size(scale, 200, 200);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_opa(scale, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scale, lv_palette_lighten(LV_PALETTE_BLUE, 3), 0);
    lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(scale, true, 0);

    lv_scale_set_label_show(scale, true);
    lv_scale_set_total_tick_count(scale, 11);
    lv_scale_set_major_tick_every(scale, 2);
    lv_scale_set_range(scale, 0, SCALE_MAX_MBIT);
    lv_scale_set_angle_range(scale, 270);
    lv_scale_set_rotation(scale, 135);
    lv_obj_set_style_length(scale, 6, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 12, LV_PART_INDICATOR);

    lv_obj_t *needle = lv_line_create(scale);
    lv_obj_set_style_line_width(needle, 6, LV_PART_MAIN);
    lv_obj_set_style_line_color(needle, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(needle, true, LV_PART_MAIN);

    lv_obj_t *value_label = lv_label_create(scale);
    lv_label_set_text(value_label, "0 Mbit/s");
    lv_obj_align(value_label, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *caption = lv_label_create(vcont);
    lv_label_set_text(caption, label_text);
    lv_obj_set_style_text_font(caption, LV_FONT_DEFAULT, 0);

    *scale_out = scale;
    *needle_out = needle;
    *value_label_out = value_label;
}

void create_tx_rx_gauges(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, 600, 250);
    lv_obj_center(container);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_style_bg_color(container, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_50, 0);

    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container, 0, 0); // 10
    lv_obj_set_style_pad_column(container, 20, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 0, 0);

    create_single_scale(container, &scale_tx, &needle_tx, &value_label_tx, "TX Mbit/s");
    create_single_scale(container, &scale_rx, &needle_rx, &value_label_rx, "RX Mbit/s");
}

void update_tx_rx_gauges(unsigned long tx_bps, unsigned long rx_bps)
{
    // Convert bytes per second to Mbit/s:
    // 1 byte = 8 bits
    double tx_mbit = (tx_bps * 8.0) / 1e6;
    double rx_mbit = (rx_bps * 8.0) / 1e6;

    if (tx_mbit > SCALE_MAX_MBIT) tx_mbit = SCALE_MAX_MBIT;
    if (rx_mbit > SCALE_MAX_MBIT) rx_mbit = SCALE_MAX_MBIT;

    lv_scale_set_line_needle_value(scale_tx, needle_tx, 60, (int)tx_mbit);
    lv_scale_set_line_needle_value(scale_rx, needle_rx, 60, (int)rx_mbit);

    static char buf_tx[32], buf_rx[32];
    snprintf(buf_tx, sizeof(buf_tx), "%.1f Mbit/s", tx_mbit);
    snprintf(buf_rx, sizeof(buf_rx), "%.1f Mbit/s", rx_mbit);

    lv_label_set_text(value_label_tx, buf_tx);
    lv_label_set_text(value_label_rx, buf_rx);
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static unsigned long get_bytes(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    unsigned long bytes = 0;
    fscanf(fp, "%lu", &bytes);
    fclose(fp);
    return bytes;
}

int get_macsec0_bytes(unsigned long *rx_bytes, unsigned long *tx_bytes) {
    if (!file_exists(RX_PATH) || !file_exists(TX_PATH))
        return -1;
    *rx_bytes = get_bytes(RX_PATH);
    *tx_bytes = get_bytes(TX_PATH);
    return 0;
}

void delay_100ms() {
    struct timespec ts = {0, 100 * 1000000};
    nanosleep(&ts, NULL);
}

void *network_monitoring_thread(void *arg)
{
    unsigned long last_rx = 0, last_tx = 0;
    unsigned long rx, tx;

    while (get_macsec0_bytes(&last_rx, &last_tx) != 0) {
        printf("macsec0 not available yet\n");
        delay_100ms();
    }

    while (1) {
        delay_100ms();

        if (get_macsec0_bytes(&rx, &tx) == 0) {
            unsigned long rx_bps = (rx - last_rx) * 10;
            unsigned long tx_bps = (tx - last_tx) * 10;
            //  Exponential Moving Average (EMA)
            #define ALPHA 0.3  // Smoothing factor: 0.1–0.3 is typical
            static double ema_rx = 0, ema_tx = 0;
            static int ema_initialized = 0;
            if (!ema_initialized) {
                ema_rx = rx_bps;
                ema_tx = tx_bps;
                ema_initialized = 1;
            } else {
                ema_rx = (1 - ALPHA) * ema_rx + ALPHA * rx_bps;
                ema_tx = (1 - ALPHA) * ema_tx + ALPHA * tx_bps;
            }

            tx_rx_data_t *data = malloc(sizeof(tx_rx_data_t));
            if (data) {
                // Without ema
                // data->tx = tx_bps;
                // data->rx = rx_bps;
                // With ema
                data->tx = (unsigned long)ema_tx;
                data->rx = (unsigned long)ema_rx;
                lv_async_call(lvgl_update_gauges_cb, data);
            }
            last_rx = rx;
            last_tx = tx;
        } else {
            printf("macsec0 not available\n");
        }
    }
    return NULL;
}


static void update_macsec_ip_cb(lv_timer_t *timer) {
    char ip[16];
    char label_text[64];

    if (get_ip_address("macsec0", ip) == 0) {
        snprintf(label_text, sizeof(label_text), "macsec0 IP address: %s", ip);
        lv_label_set_text(label_status_macsec_ip_address, label_text);
        lv_timer_del(timer);
        lv_led_on(macsec_keyed_led);
        run_route_script();
        lv_led_on(macsec_keyed_led);
    }
}

void get_current_timestamp(void)
{
    time_t now = time(NULL);
    struct tm * t = localtime(&now);
    if (t) {
        snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d]", t->tm_hour, t->tm_min, t->tm_sec);
    } else {
        snprintf(timestamp, sizeof(timestamp), "[00:00:00]");
    }
}

void on_touch_event()
{
    atomic_store(&last_touch_time, time(NULL));

    if (atomic_exchange(&backlight_off, false)) {
        // Unblank the display
        FILE *blank_fp = fopen(FB_BLANK_PATH, "w");
        if (blank_fp) {
            fprintf(blank_fp, "0\n");  // 0 = unblank
            fclose(blank_fp);
            printf("Display turned ON (fb0/blank = 0) on touch.\n");
        } else {
            perror("Failed to write to fb0/blank to turn on display");
        }

        // Restore brightness from INI
        int brightness_value = 128;
        mini_t *ini = mini_try_load("./lvgl.ini");
        brightness_value = mini_get_int(ini, "lvgl", "backlight_brightness", 128);
        mini_free(ini);

        FILE *bp = fopen(BRIGHTNESS_SYSFS_PATH, "w");
        if (bp) {
            fprintf(bp, "%d\n", brightness_value);
            fclose(bp);
            printf("Brightness restored to %d\n", brightness_value);
        } else {
            perror("Failed to restore brightness");
        }
    }
}

static void global_input_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        on_touch_event();
    }
}

static void update_uptime_label(void *param)
{
    int uptime = *((int *)param);
    free(param);  // prevent memory leak

    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;

    if (days > 0) {
        lv_label_set_text_fmt(uptime_label, "Uptime: %d days %02d:%02d:%02d", days, hours, minutes, seconds);
    } else {
        lv_label_set_text_fmt(uptime_label, "Uptime: %02d:%02d:%02d", hours, minutes, seconds);
    }
}


void *screen_timeout_thread(void *arg)
{
    static time_t boot_time = 0;

    while (1) {
        time_t now = time(NULL);

        if (boot_time == 0)
            boot_time = now;

        time_t last_touch = atomic_load(&last_touch_time);
        double elapsed = difftime(now, last_touch);

        // Evaluate only if set from settings page
        if (g_backlight_timeout) 
        {
            if (!atomic_load(&backlight_off) && elapsed >= TIMEOUT_SECONDS) {
                FILE *fp = fopen(FB_BLANK_PATH, "w");
                if (fp) {
                    fprintf(fp, "1\n");  // 1 = blank screen
                    fclose(fp);
                    atomic_store(&backlight_off, true);
                    printf("Display turned OFF (fb0/blank = 1) due to inactivity.\n");
                } else {
                    perror("Failed to write to fb0/blank to turn off display");
                }
            }
        }

        // Uptime
        double uptime_sec = 0.0;
        FILE *fp = fopen("/proc/uptime", "r");
        if (fp) {
            fscanf(fp, "%lf", &uptime_sec);
            fclose(fp);
        } else {
            perror("Failed to read /proc/uptime");
            uptime_sec = 0;
        }
        int *uptime_copy = malloc(sizeof(int));
        *uptime_copy = (int)uptime_sec;
        lv_async_call(update_uptime_label, uptime_copy);
        
        sleep(1);
    }

    return NULL;
}

// Settings screen button event callback
static void button_event_callback(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target_obj(e);

    if(code == LV_EVENT_CLICKED) {
        button_data_t *data = (button_data_t *)lv_event_get_user_data(e);

        if(data) {
            int id = data->button_id;

            if(id == 0) {
                printf("Power off button\n");
                sync();
                system("poweroff");
            }
            else if(id == 1) {
                printf("button id: 1 \n");
            }
        }
    }
}



// FIFO functions
void fifo_init(void)
{
    // Check and create FIFO_IN if it doesn't exist
    struct stat st;
    if (stat(FIFO_IN, &st) != 0) {
        if (mkfifo(FIFO_IN, 0666) != 0) {
            perror("mkfifo FIFO_IN failed");
        } else {
            printf("Created FIFO_IN at %s\n", FIFO_IN);
        }
    } else if (!S_ISFIFO(st.st_mode)) {
        fprintf(stderr, "%s exists but is not a FIFO\n", FIFO_IN);
    }

    // Check and create FIFO_OUT if it doesn't exist
    if (stat(FIFO_OUT, &st) != 0) {
        if (mkfifo(FIFO_OUT, 0666) != 0) {
            perror("mkfifo FIFO_OUT failed");
        } else {
            printf("Created FIFO_OUT at %s\n", FIFO_OUT);
        }
    } else if (!S_ISFIFO(st.st_mode)) {
        fprintf(stderr, "%s exists but is not a FIFO\n", FIFO_OUT);
    }
}

void append_log_message(const char * msg)
{
    if (!message_log_ta) return;
    const char * current_text = lv_textarea_get_text(message_log_ta);
    static char buffer[1024]; // Make sure it's big enough
    get_current_timestamp();
    snprintf(buffer, sizeof(buffer), "%s\n%s %s", current_text, timestamp, msg);
    lv_textarea_set_text(message_log_ta, buffer);
}

void lv_append_log_cb(void * msg)
{
    append_log_message((const char *)msg);
    free(msg);  // free copied message
}

void *fifo_reader_thread(void *arg)
{
    int fd = open(FIFO_IN, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open fifo_in");
        return NULL;
    }

    char line[256];
    while (1) {
        ssize_t len = read(fd, line, sizeof(line) - 1);
        if (len > 0) {
            line[len] = '\0';

            // Strip newline characters
            for (char *p = line; *p; ++p) {
                if (*p == '\n' || *p == '\r') {
                    *p = '\0';
                    break;
                }
            }

            char * msg_copy = strdup(line);
            if (msg_copy)
                lv_async_call(lv_append_log_cb, msg_copy);
        } else {
            usleep(10000); // 10ms sleep
        }
    }

    close(fd);
    return NULL;
}

/* lvgl functionality */
const char *getenv_default(const char *name, const char *dflt)
{
    return getenv(name) ?: dflt;
}

void lv_linux_disp_init(void)
{
    printf("lvgl-app demo for lvgl usage \n");
    const char *device = getenv_default("LV_LINUX_FBDEV_DEVICE", "/dev/fb0");
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, device);
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event1");
    lv_indev_set_display(touch, disp);
    // Add global event callback to evdev input
    lv_indev_add_event_cb(touch, global_input_event_cb, LV_EVENT_PRESSED, NULL);
}

// Scrollable text area
lv_obj_t * create_message_log_view(lv_obj_t * parent)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, lv_pct(100), 180);          // 650
    lv_textarea_set_text(ta, "");                   // Start with empty text
    lv_textarea_set_max_length(ta, 1024);           // Optional max length: TODO FIX BUFFER
    lv_textarea_set_cursor_click_pos(ta, true);     // Enable placing cursor by click
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_AUTO); 
    lv_textarea_set_accepted_chars(ta, NULL);       // Accept all characters
    lv_textarea_set_one_line(ta, false);            // Allow multiline
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_LEFT, 0); // Align text to left
    lv_obj_set_style_pad_all(ta, 5, 0);                     // Padding inside text area

    return ta;
}

void lv_message_log(void)
{
    message_log_ta = create_message_log_view(lv_screen_active());
    lv_obj_align(message_log_ta, LV_ALIGN_TOP_LEFT, 5, 25);
    lv_textarea_set_text(message_log_ta, "");
}


// Message entry
static void message_entry_event_callback(lv_event_t * e);
static lv_obj_t * kb;

void lv_message_entry(void)
{
    /* Create the one-line mode text area */
    lv_obj_t * text_ta = lv_textarea_create(lv_screen_active());
    lv_textarea_set_one_line(text_ta, true);
    lv_textarea_set_password_mode(text_ta, false);
    lv_obj_set_width(text_ta, lv_pct(65));
    lv_obj_add_event_cb(text_ta, message_entry_event_callback, LV_EVENT_ALL, NULL);
    lv_obj_align(text_ta, LV_ALIGN_BOTTOM_LEFT, 5, -215);

    /* Create a label and position it above the text box */
    lv_obj_t * oneline_label = lv_label_create(lv_screen_active());
    lv_label_set_text(oneline_label, "Message:");
    lv_obj_align_to(oneline_label, text_ta, LV_ALIGN_OUT_TOP_LEFT, 5, 0);

    /* Create a keyboard */
    kb = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(kb,  LV_HOR_RES, (LV_VER_RES / 2) - 40 );
    lv_keyboard_set_textarea(kb, text_ta); /*Focus it on one of the text areas to start*/
    
    /*The keyboard will show Arabic characters if they are enabled */
#if LV_USE_ARABIC_PERSIAN_CHARS && LV_FONT_DEJAVU_16_PERSIAN_HEBREW
    lv_obj_set_style_text_font(kb, &lv_font_dejavu_16_persian_hebrew, 0);
    lv_obj_set_style_text_font(text_ta, &lv_font_dejavu_16_persian_hebrew, 0);
    lv_obj_set_style_text_font(pwd_ta, &lv_font_dejavu_16_persian_hebrew, 0);
#endif

}

static void message_entry_event_callback(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target_obj(e);

    if(code == LV_EVENT_FOCUSED) {
        if(kb != NULL) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);      // Show keyboard
        }
    }
    else if(code == LV_EVENT_DEFOCUSED) {
        if(kb != NULL) {
            lv_keyboard_set_textarea(kb, NULL);             // Unlink keyboard
        }
    }
    else if(code == LV_EVENT_READY) {
        const char * new_msg = lv_textarea_get_text(ta);
        if (message_log_ta && new_msg && strlen(new_msg) > 0) {
            const char * old_msg = lv_textarea_get_text(message_log_ta);

            // Prepare updated log message
            char buffer[1024];
            get_current_timestamp();  // Fill the timestamp string
            snprintf(buffer, sizeof(buffer), "%s\n%s %s", old_msg, timestamp, new_msg);
            lv_textarea_set_text(message_log_ta, buffer);

            // Write new message to FIFO_OUT
            int fd = open(FIFO_OUT, O_WRONLY | O_NONBLOCK);
            if (fd >= 0) {
                
                get_current_timestamp();
                dprintf(fd, "%s %s\n",timestamp, new_msg);  // fifo want's new line
                close(fd);
            } else {
                perror("open fifo_out");
            }
        }
        // Clear input textarea
        lv_textarea_set_text(ta, "");
    }
}

/* Brightness slider for settings screen */
static void brightness_slider_event_callback(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target_obj(e);
    int percent = lv_slider_get_value(slider);

    // Update label
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "Brightness: %d%%", percent);
    lv_label_set_text(slider_label, buf);
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_TOP_LEFT, 0, -10);

    // Scale 0–100% to 0–255
    int brightness_value = (percent * 255) / 100;

    // Write to sysfs
    FILE *fp = fopen(BRIGHTNESS_SYSFS_PATH, "w");
    if (fp) {
        fprintf(fp, "%d\n", brightness_value);
        fclose(fp);
    } else {
        perror("Failed to open brightness sysfs");
    }
    
    // Write to ini
    mini_t *ini_int_write = mini_try_load("./lvgl.ini");
    mini_set_int(ini_int_write, "lvgl", "backlight_brightness", brightness_value);
    mini_save(ini_int_write, MINI_FLAGS_SKIP_EMPTY_GROUPS); 
	mini_free(ini_int_write);
}

void lv_brightness_slider(lv_obj_t *screen)
{
    int brightness_value = -1;

    // Try to read from INI first
    mini_t *ini = mini_try_load("./lvgl.ini");
    brightness_value = mini_get_int(ini, "lvgl", "backlight_brightness", -1);  // -1 = not found
    mini_free(ini);

    // Fallback to sysfs if no saved value
    if (brightness_value < 0) {
        brightness_value = 128;  // default
        FILE *fp = fopen(BRIGHTNESS_SYSFS_PATH, "r");
        if (fp) {
            fscanf(fp, "%d", &brightness_value);
            fclose(fp);
        } else {
            perror("Failed to read brightness from sysfs");
        }
    }

    // Clamp value to 0–255
    if (brightness_value < 0) brightness_value = 0;
    if (brightness_value > 255) brightness_value = 255;

    // Write to sysfs to ensure value is restored to hardware
    FILE *fp = fopen(BRIGHTNESS_SYSFS_PATH, "w");
    if (fp) {
        fprintf(fp, "%d\n", brightness_value);
        fclose(fp);
    } else {
        perror("Failed to write restored brightness to sysfs");
    }

    // Scale to 0–100 slider percent
    int slider_percent = (brightness_value * 100) / 255;

    // Create slider
    lv_obj_t * slider = lv_slider_create(screen);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, slider_percent, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 5, 60);
    lv_obj_add_event_cb(slider, brightness_slider_event_callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_anim_duration(slider, 2000, 0);

    // Create label
    slider_label = lv_label_create(screen);
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "Screen brightness: %d%%", slider_percent);
    lv_label_set_text(slider_label, buf);
    lv_obj_set_style_text_color(slider_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_TOP_LEFT, 0, -10);
}




/* Switches event handler */
static void switch_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * sw = lv_event_get_target_obj(e);
    switch_context_t * ctx = lv_event_get_user_data(e);

    if(code == LV_EVENT_VALUE_CHANGED && ctx) {
        bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
        
        // Write INI file based on switch setting
        if( ctx->switch_id == SWITCH_BACKLIGHT) 
        {
            g_backlight_timeout = state;
            mini_t *ini = mini_try_load("./lvgl.ini");
            mini_set_bool(ini, "lvgl", "backlight_timeout", state);
            mini_save(ini, MINI_FLAGS_SKIP_EMPTY_GROUPS); 
            mini_free(ini);
        }
        if( ctx->switch_id == SWITCH_BACKLIGHT_WAKEUP) 
        {
            mini_t *ini = mini_try_load("./lvgl.ini");
            mini_set_bool(ini, "lvgl", "backlight_wakeup", state);
            mini_save(ini, MINI_FLAGS_SKIP_EMPTY_GROUPS); 
            mini_free(ini);
        }
        if( ctx->switch_id == SWITCH_PTT_TO_MACSEC) 
        {
            mini_t *ini = mini_try_load("./lvgl.ini");
            mini_set_bool(ini, "lvgl", "ptt_macsec", state);
            mini_save(ini, MINI_FLAGS_SKIP_EMPTY_GROUPS); 
            mini_free(ini);
        }
        if( ctx->switch_id == SWITCH_PTT_TO_WAN) 
        {
            mini_t *ini = mini_try_load("./lvgl.ini");
            mini_set_bool(ini, "lvgl", "ptt_wan", state);
            mini_save(ini, MINI_FLAGS_SKIP_EMPTY_GROUPS); 
            mini_free(ini);
        }
    }
}

// New way for settings tab row layout
void lv_add_settings_switch(lv_obj_t *screen, const char * name, int switch_id)
{
    // Create switch
    lv_obj_t * sw = lv_switch_create(screen);
    switch_objects[switch_id] = sw;

    // Create label to the right
    lv_obj_t * label = lv_label_create(screen);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_align_to(label, sw, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    // Allocate and fill context
    switch_context_t * ctx = malloc(sizeof(switch_context_t));
    ctx->name = name;
    ctx->label = label;
    ctx->switch_id = switch_id;

    // Register shared event handler with unique context
    lv_obj_add_event_cb(sw, switch_event_handler, LV_EVENT_VALUE_CHANGED, ctx);
}

static void settings_text_event_callback(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target_obj(e);

    if(code == LV_EVENT_FOCUSED) {
        if(kb != NULL) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);      // Show keyboard
        }
    }
    else if(code == LV_EVENT_DEFOCUSED) {
        if(kb != NULL) {
            lv_keyboard_set_textarea(kb, NULL);             // Unlink keyboard
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if(code == LV_EVENT_READY) {
        const char * new_msg = lv_textarea_get_text(ta);
            // TODO ADD FUNCTIONALITY HERE
        }
}



static void close_popup_cb(lv_timer_t *timer) {
    lv_obj_t *popup = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (popup) {
        lv_obj_del(popup);
    }
    lv_timer_del(timer);
}

void show_notification(const char *msg) {
    lv_obj_t *popup = lv_obj_create(lv_screen_active());
    lv_obj_set_size(popup, 280, 80);
    lv_obj_align(popup, LV_ALIGN_TOP_RIGHT, -20, 50);
    lv_obj_set_style_radius(popup, 12, 0);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x8080D0), 0);
    lv_obj_set_style_text_color(popup, lv_color_white(), 0);
    lv_obj_set_style_pad_all(popup, 12, 0);
    lv_obj_t *label = lv_label_create(popup);
    lv_label_set_text(label, msg);
    lv_obj_center(label);
    lv_timer_t *timer = lv_timer_create(close_popup_cb, 5000, popup);
}

/**
 * Restart the systemd service "macpipe.service".
 * Returns 0 on success, -1 on error.
 */
int restart_macpipe_service(void) {
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("fork");
        return -1;
    } else if (pid == 0) {
        execl("/bin/systemctl", "systemctl", "restart", "macpipe.service", NULL);
        // If execl fails:
        perror("execl");
        _exit(127);
    } else {
        // Parent process: wait for child
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return -1;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0; // Success
        } else {
            fprintf(stderr, "systemctl exited with status %d\n", WEXITSTATUS(status));
            return -1;
        }
    }
}


static void settings_text_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *kb_settings = (lv_obj_t *)lv_event_get_user_data(e);
    text_context_t *ctx = lv_obj_get_user_data(ta);  // get context

    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb_settings, ta);
        lv_obj_clear_flag(kb_settings, LV_OBJ_FLAG_HIDDEN);

        lv_area_t ta_coords;
        lv_obj_get_coords(ta, &ta_coords);
        lv_coord_t kb_top = lv_obj_get_y(kb_settings);
        if(ta_coords.y2 > kb_top) {
            lv_obj_scroll_to_y(lv_obj_get_parent(ta),
                lv_obj_get_scroll_y(lv_obj_get_parent(ta)) + (ta_coords.y2 - kb_top) + 10,
                LV_ANIM_ON);
        }
        // Hide lv_layer_sys
        lv_obj_add_flag(lv_layer_sys(), LV_OBJ_FLAG_HIDDEN);

    } else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(kb_settings, LV_OBJ_FLAG_HIDDEN);
        const char *text = lv_textarea_get_text(ta);
        // Save to lvgl.ini
        mini_t *ini_string_write = mini_try_load("./lvgl.ini");
        mini_set_string(ini_string_write, "lvgl", ctx->ini_key, text);
        mini_save(ini_string_write, MINI_FLAGS_SKIP_EMPTY_GROUPS); 
        mini_free(ini_string_write);
        // If ctx->ini_key is "word_of_day", also save to /opt/macpipe/macpipe.ini
        if (strcmp(ctx->ini_key, "word_of_day") == 0) {
            mini_t *macpipe_ini = mini_try_load("/opt/macpipe/macpipe.ini");
            mini_set_string(macpipe_ini, "settings", "shared_secret", text);
            mini_save(macpipe_ini, MINI_FLAGS_SKIP_EMPTY_GROUPS);
            mini_free(macpipe_ini);
            restart_macpipe_service();
            show_notification("macsec rekeying restarted");
        }
        // Unhide lv_layer_sys
        lv_obj_clear_flag(lv_layer_sys(), LV_OBJ_FLAG_HIDDEN);
    }
}

void lv_set_system_layer(void)
{
    lv_obj_set_style_bg_color(lv_layer_sys(), lv_color_hex(0x003a57), LV_PART_MAIN);

    // Envelope icon (existing)
    lv_obj_t *label_icon = lv_label_create(lv_layer_sys());
    lv_label_set_text(label_icon, LV_SYMBOL_ENVELOPE);

    static lv_style_t style_large;
    lv_style_init(&style_large);
    lv_style_set_text_font(&style_large, &lv_font_montserrat_20);
    lv_obj_add_style(label_icon, &style_large, LV_PART_MAIN);

    lv_obj_set_style_text_color(label_icon, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_align(label_icon, LV_ALIGN_BOTTOM_RIGHT, -4, 0);

    // Add uptime label
    uptime_label = lv_label_create(lv_layer_sys());
    lv_label_set_text(uptime_label, "Uptime: 0s");  // initial text
    lv_obj_add_style(uptime_label, &style_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(uptime_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_align(uptime_label, LV_ALIGN_BOTTOM_LEFT, 4, 0);  // bottom-left corner
}

void lv_create_tab_view(void)
{
    /* Create a Tab view object */
    lv_obj_t * tabview;
    tabview = lv_tabview_create(lv_screen_active());

    /* Add 3 tabs (the tabs are page (lv_page) and can be scrolled */
    lv_obj_t * tab1 = lv_tabview_add_tab(tabview, "Status");
    lv_obj_t * tab2 = lv_tabview_add_tab(tabview, "Out Of Band messaging");
    lv_obj_t * tab3 = lv_tabview_add_tab(tabview, "Settings");
    
    lv_tabview_set_tab_bar_size(tabview, 40);

    /* First tab */
    
        lv_obj_t * tab1_content = lv_obj_create(tab1);

        lv_obj_set_style_pad_all(tab1_content, 0, 0);
        lv_obj_set_style_pad_row(tab1_content, 0, 0);
        lv_obj_set_style_pad_column(tab1_content, 0, 0);
        lv_obj_set_style_border_width(tab1_content, 0, 0);
        lv_obj_set_style_bg_opa(tab1_content, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_gap(tab1_content, 10, 0);  // Spacing between rows
        
        lv_obj_set_size(tab1_content, lv_pct(100), lv_pct(100));
        lv_obj_set_layout(tab1_content, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tab1_content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(tab1_content, LV_DIR_VER);
        
        // Title label
        lv_obj_t * label = lv_label_create(tab1_content);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_label_set_text(label, "Security router");
        
        // Description label
        lv_obj_t * label_desc = lv_label_create(tab1_content);
        lv_obj_set_style_text_font(label_desc, &lv_font_montserrat_16, 0);
        lv_label_set_text(label_desc, "This router separates you cryptographically from LAN segment. It prevents lateral movement \ntowards your MACSEC segment while routing your traffic to LAN and back.\n\nThere is also Out Of Band messaging solution and secure Push-To-Talk for incident\nmanagement purposes when you cannot trust your primary channels.");
        
        // Status title label
        lv_obj_t * label_status_title = lv_label_create(tab1_content);
        lv_obj_set_style_text_font(label_status_title, &lv_font_montserrat_24, 0);
        lv_label_set_text(label_status_title, "Router status:");
        
        // Get mac address of wired ethernet
        char mac[18];
        char mac_label_text[64];
        char interface[]="end0";
        if (get_mac_address(interface, mac) == 0) {
            for (char *p = mac; *p; ++p) {
                *p = toupper((unsigned char)*p);
            }
            snprintf(mac_label_text, sizeof(mac_label_text), "Router mac address: %s (%s)", mac,interface);
        } else {
            show_notification("Failed to get MAC address");
        }
        lv_obj_t * label_status_mac_address = lv_label_create(tab1_content);
        lv_obj_set_style_text_font(label_status_mac_address, &lv_font_montserrat_20, 0);
        lv_label_set_text(label_status_mac_address, mac_label_text);
        
        // update macsec0 ip and macsec_keyed_led from update_macsec_ip_cb()
        label_status_macsec_ip_address = lv_label_create(tab1_content);
        lv_obj_set_style_text_font(label_status_macsec_ip_address, &lv_font_montserrat_20, 0);
        lv_label_set_text(label_status_macsec_ip_address, "Waiting IP address for macsec0 interface");
        
        // kernel version
        lv_obj_t * label_status_kernel_version = lv_label_create(tab1_content);
        lv_obj_set_style_text_font(label_status_kernel_version, &lv_font_montserrat_16, 0);
        
        char kernel_version[128];
        if (get_kernel_version(kernel_version, sizeof(kernel_version)) == 0) {
            lv_label_set_text(label_status_kernel_version, kernel_version);
        } else {
            printf("Failed to get kernel version\n");
        }
        
        // Create horizontal row container for LED's
        lv_obj_t * led_row = lv_obj_create(tab1_content);
        lv_obj_set_style_bg_opa(led_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(led_row, 0, 0);

        lv_obj_set_style_pad_left(led_row, 5, 0);
        lv_obj_set_style_pad_top(led_row, 5, 0);
        lv_obj_set_style_pad_bottom(led_row, 0, 0); // 5
        lv_obj_set_style_pad_right(led_row, 5, 0);

        lv_obj_set_layout(led_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(led_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(led_row, lv_pct(100));
        lv_obj_set_height(led_row, lv_pct(10));

        // Create LED #1 inside row
        macsec_keyed_led = lv_led_create(led_row);
        lv_led_off(macsec_keyed_led);
        lv_obj_set_style_pad_all(macsec_keyed_led, 2, 0);

        // Create label next to LED
        lv_obj_t * led_label = lv_label_create(led_row);
        lv_label_set_text(led_label, "MACSEC KEYED" );
        lv_obj_set_style_text_font(led_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_left(led_label, 0, 0); // space between LED and text
        lv_obj_set_style_pad_right(led_label, 30, 0);

        // Create LED #2 inside row
        macsec_routing_led = lv_led_create(led_row);
        lv_led_off(macsec_routing_led);
        lv_obj_set_style_pad_all(macsec_routing_led, 2, 0);
        
        // Create label next to LED
        lv_obj_t * led_label_2 = lv_label_create(led_row);
        lv_label_set_text(led_label_2, "MACSEC " LV_SYMBOL_LOOP " LAN" );
        lv_obj_set_style_text_font(led_label_2, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_left(led_label_2, 0, 0); // space between LED and text
        lv_obj_set_style_pad_right(led_label_2, 30, 0);

        // Create LED #3 inside row
        wifi_led = lv_led_create(led_row);
        lv_led_off(wifi_led);
        lv_obj_set_style_pad_all(wifi_led, 2, 0);
        
        // Create label next to LED
        lv_obj_t * led_label_3 = lv_label_create(led_row);
        lv_label_set_text(led_label_3, "WIFI " LV_SYMBOL_WIFI );
        lv_obj_set_style_text_font(led_label_3, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_left(led_label_3, 0, 0); // space between LED and text
        
        lv_obj_t * label_speed_title = lv_label_create(tab1_content);
        lv_obj_set_style_text_font(label_speed_title, &lv_font_montserrat_24, 0);
        lv_label_set_text(label_speed_title, "Network speed (macsec0):");
        
        // macsec0 speed meters
        create_tx_rx_gauges(tab1_content);

    /* Second tab */
        label = lv_label_create(tab2);
        lv_label_set_text(label, "");
        
        lv_obj_t * tab2_content = lv_obj_create(tab2);
        
        // Remove padding from the flex container
        lv_obj_set_style_pad_all(tab2_content, 0, 0);
        lv_obj_set_style_pad_row(tab2_content, 5, 0);
        lv_obj_set_style_pad_column(tab2_content, 0, 0);
        lv_obj_set_style_border_width(tab2_content, 0, 0);
        lv_obj_set_style_bg_opa(tab2_content, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_gap(tab2_content, 10, 0);
        
        lv_obj_set_size(tab2_content, lv_pct(100), lv_pct(100));
        lv_obj_set_layout(tab2_content, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tab2_content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(tab2_content, LV_DIR_VER);
        
        // Message log
        message_log_ta = create_message_log_view(tab2_content);
        lv_obj_set_height(message_log_ta, 140);
        lv_obj_set_style_text_font(message_log_ta, &lv_font_montserrat_16, 0);

        // Message entry
        lv_obj_t * text_ta = lv_textarea_create(tab2_content);
        lv_textarea_set_one_line(text_ta, true);
        lv_obj_set_width(text_ta, lv_pct(100));
        lv_textarea_set_placeholder_text(text_ta, "Type a message and press enter to send...");
        lv_obj_add_event_cb(text_ta, message_entry_event_callback, LV_EVENT_ALL, NULL);
        lv_obj_set_style_text_font(text_ta, &lv_font_montserrat_16, 0);
        
        // Force focus
        lv_obj_add_state(text_ta, LV_STATE_FOCUSED);
        
        // Create keyboard and add to the same container
        kb = lv_keyboard_create(tab2_content);
        lv_obj_set_height(kb, LV_VER_RES / 2.5);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, 0);
        // Link and show keyboard
        lv_keyboard_set_textarea(kb, text_ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        
        // padding
        lv_obj_set_style_pad_all(text_ta, 10, 0);
        lv_obj_set_style_border_width(text_ta, 1, 0);
        lv_obj_set_style_pad_all(kb, 0, 0);
        lv_obj_set_style_border_width(kb, 0, 0);

    /* Third tab */
        
        lv_obj_t * tab3_content = lv_obj_create(tab3);
        lv_obj_set_size(tab3_content, lv_pct(100), lv_pct(100));
        lv_obj_set_layout(tab3_content, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tab3_content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(tab3_content, LV_DIR_VER);
        lv_obj_set_style_pad_column(tab3_content, 0, 0);
        lv_obj_set_style_pad_row(tab3_content, 15, 0);
        lv_obj_set_style_border_width(tab3_content, 0, 0);
        lv_obj_set_style_bg_opa(tab3_content, LV_OPA_TRANSP, 0);
    
    
        // Device settings title
        lv_obj_t * label_device_settings_title = lv_label_create(tab3_content);
        lv_obj_set_style_text_font(label_device_settings_title, &lv_font_montserrat_20, 0);
        lv_label_set_text(label_device_settings_title, "Device settings");
    
        // Create horizontal row container
        lv_obj_t * brightness_row = lv_obj_create(tab3_content);
        lv_obj_set_style_bg_opa(brightness_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(brightness_row, 0, 0);
        lv_obj_set_style_pad_left(brightness_row, 5, 0);
        lv_obj_set_style_pad_top(brightness_row, 5, 0);
        lv_obj_set_style_pad_bottom(brightness_row, 5, 0);
        lv_obj_set_style_pad_right(brightness_row, 5, 0);
        lv_obj_set_layout(brightness_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(brightness_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(brightness_row, lv_pct(100));
        lv_obj_set_height(brightness_row, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(brightness_row, 0);
        
        // Brightness slider
        lv_brightness_slider(brightness_row);
        
        // Create horizontal row container for a switch
        lv_obj_t * switch_row = lv_obj_create(tab3_content);
        lv_obj_set_style_bg_opa(switch_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(switch_row, 0, 0);
        lv_obj_set_style_pad_left(switch_row, 5, 0);
        lv_obj_set_style_pad_top(switch_row, 0, 0);
        lv_obj_set_style_pad_bottom(switch_row, 0, 0);
        lv_obj_set_style_pad_right(switch_row, 5, 0);
        lv_obj_set_layout(switch_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(switch_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(switch_row, lv_pct(100));
        lv_obj_set_style_margin_top(switch_row, 0, 0);
        lv_obj_set_style_margin_bottom(switch_row, 0, 0);
        // Switch
        lv_add_settings_switch(switch_row, "Backlight timeout:\nbacklight turns off after 1 minute inactivity.",SWITCH_BACKLIGHT);
        lv_obj_set_height(switch_row, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(switch_row, 0);
        
        // Create horizontal row container for a switch
        lv_obj_t * settings_switch_row_screen_wakeup = lv_obj_create(tab3_content);
        lv_obj_set_style_bg_opa(settings_switch_row_screen_wakeup, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(settings_switch_row_screen_wakeup, 0, 0);
        lv_obj_set_style_pad_left(settings_switch_row_screen_wakeup, 5, 0);
        lv_obj_set_style_pad_top(settings_switch_row_screen_wakeup, 0, 0);
        lv_obj_set_style_pad_bottom(settings_switch_row_screen_wakeup, 0, 0);
        lv_obj_set_style_pad_right(settings_switch_row_screen_wakeup, 5, 0);
        lv_obj_set_layout(settings_switch_row_screen_wakeup, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(settings_switch_row_screen_wakeup, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(settings_switch_row_screen_wakeup, lv_pct(100));
        lv_obj_set_style_margin_top(settings_switch_row_screen_wakeup, 0, 0);
        lv_obj_set_style_margin_bottom(settings_switch_row_screen_wakeup, 0, 0);
        // Switch
        lv_add_settings_switch(settings_switch_row_screen_wakeup, "Screen wakeup on new message or\nPush-To-Talk traffic.",SWITCH_BACKLIGHT_WAKEUP);
        lv_obj_set_height(settings_switch_row_screen_wakeup, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(settings_switch_row_screen_wakeup, 0);
        
        // Device settings title
        lv_obj_t * label_macsec_settings_title = lv_label_create(tab3_content);
        lv_obj_set_style_text_font(label_macsec_settings_title, &lv_font_montserrat_20, 0);
        lv_label_set_text(label_macsec_settings_title, "MACSEC settings");
    
        // Text title
        lv_obj_t * text_label = lv_label_create(tab3_content);
        // lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_label_set_text(text_label, "Shared secret for MACSEC key delivery:");

        // Text entry field for testing
        lv_obj_t * text_setting = lv_textarea_create(tab3_content);
        lv_textarea_set_one_line(text_setting, true);
        lv_obj_set_width(text_setting, lv_pct(50));
        lv_textarea_set_placeholder_text(text_setting, "Word of day");
        // associate context with this field
        text_context_t *ctx = malloc(sizeof(text_context_t));
        ctx->ini_key = "word_of_day";  // unique key for this field
        lv_obj_set_user_data(text_setting, ctx);
        // Load initial value from INI
        mini_t *ini = mini_try_load("./lvgl.ini");
        const char *initial_text = mini_get_string(ini, "lvgl", "word_of_day", "");
        lv_textarea_set_text(text_setting, initial_text);
        mini_free(ini);
        
        // Text title label for IP address
        lv_obj_t * text_label_ip_address = lv_label_create(tab3_content);
        lv_label_set_text(text_label_ip_address, "IP address:");
        
        // Text entry field for IP address (demo placeholder)
        lv_obj_t * text_setting_ip = lv_textarea_create(tab3_content);
        lv_textarea_set_one_line(text_setting_ip, true);
        lv_obj_set_width(text_setting_ip, lv_pct(50));
        lv_textarea_set_placeholder_text(text_setting_ip, "IP Address");
        // Associate context with this field
        text_context_t *ctx_ip = malloc(sizeof(text_context_t));
        ctx_ip->ini_key = "ip_address";  // unique key for this field
        lv_obj_set_user_data(text_setting_ip, ctx_ip);
        // Load initial value from INI
        mini_t *ini_ip = mini_try_load("./lvgl.ini");
        const char *initial_ip = mini_get_string(ini_ip, "lvgl", "ip_address", "");
        lv_textarea_set_text(text_setting_ip, initial_ip);
        mini_free(ini_ip);
        
            // Create keyboard for settings, this will pop up on settings page text fields
            lv_obj_t * kb_settings = lv_keyboard_create(lv_screen_active());
            lv_obj_align(kb_settings, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_width(kb_settings, lv_pct(100));
            lv_obj_set_height(kb_settings, LV_VER_RES / 2.5);
            lv_obj_add_flag(kb_settings, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_font(kb_settings, &lv_font_montserrat_20, 0);
            lv_obj_set_style_pad_all(kb_settings, 0, 0);
            lv_obj_set_style_border_width(kb_settings, 0, 0);
            
            // Add callbacks for all text fields
            lv_obj_add_event_cb(text_setting, settings_text_event_cb, LV_EVENT_ALL, kb_settings);
            lv_obj_add_event_cb(text_setting_ip, settings_text_event_cb, LV_EVENT_ALL, kb_settings);
        
        
        // Push to talk settings title
        lv_obj_t * label_ptt_settings_title = lv_label_create(tab3_content);
        lv_obj_set_style_text_font(label_ptt_settings_title, &lv_font_montserrat_20, 0);
        lv_label_set_text(label_ptt_settings_title, "Push To Talk settings");
        
        // Create horizontal row container for a switch
        lv_obj_t * switch_row_2 = lv_obj_create(tab3_content);
        lv_obj_set_style_bg_opa(switch_row_2, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(switch_row_2, 0, 0);
        lv_obj_set_style_pad_left(switch_row_2, 5, 0);
        lv_obj_set_style_pad_top(switch_row_2, 0, 0);
        lv_obj_set_style_pad_bottom(switch_row_2, 0, 0);
        lv_obj_set_style_pad_right(switch_row_2, 5, 0);
        lv_obj_set_layout(switch_row_2, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(switch_row_2, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(switch_row_2, lv_pct(100));
        lv_obj_set_style_margin_top(switch_row_2, 0, 0);
        lv_obj_set_style_margin_bottom(switch_row_2, 0, 0);
        // Switch
        lv_add_settings_switch(switch_row_2, "Enable Push-To-Talk\nto MACSEC segment",SWITCH_PTT_TO_MACSEC);
        lv_obj_set_height(switch_row_2, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(switch_row_2, 0);
        
        // Create horizontal row container for a switch
        lv_obj_t * switch_row_3 = lv_obj_create(tab3_content);
        lv_obj_set_style_bg_opa(switch_row_3, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(switch_row_3, 0, 0);
        lv_obj_set_style_pad_left(switch_row_3, 5, 0);
        lv_obj_set_style_pad_top(switch_row_3, 0, 0);
        lv_obj_set_style_pad_bottom(switch_row_3, 0, 0);
        lv_obj_set_style_pad_right(switch_row_3, 5, 0);
        lv_obj_set_layout(switch_row_3, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(switch_row_3, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(switch_row_3, lv_pct(100));
        lv_obj_set_style_margin_top(switch_row_3, 0, 0);
        lv_obj_set_style_margin_bottom(switch_row_3, 0, 0);
        // Switch
        lv_add_settings_switch(switch_row_3, "Enable Push-To-Talk\nto WAN segment",SWITCH_PTT_TO_WAN);
        lv_obj_set_height(switch_row_3, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(switch_row_3, 0);
        
        
        // Buttons title
        lv_obj_t * label_buttons_settings_title = lv_label_create(tab3_content);
        lv_obj_set_style_text_font(label_buttons_settings_title, &lv_font_montserrat_20, 0);
        lv_label_set_text(label_buttons_settings_title, "Device control");
        
        // lv_obj_t * text_label = lv_label_create(tab3_content);
        // lv_label_set_text(text_label, "");
        
        // Set button data & create button
        static button_data_t btn_data1 = { .button_id = 0, .target_screen = NULL };
        lv_obj_t *btn = lv_button_create(tab3_content);
        lv_obj_set_size(btn, 120, 50);
        lv_obj_add_event_cb(btn, button_event_callback, LV_EVENT_ALL, &btn_data1);
        lv_obj_t *button_label = lv_label_create(btn);
        lv_label_set_text(button_label, "Poweroff");
        lv_obj_center(button_label);
        
        
        // Give some lenght to tab so text can be adjusted into view
        lv_obj_t * text_label_tmp = lv_label_create(tab3_content);
        lv_label_set_text(text_label_tmp, "\n\n\n\n\n\n\n\n");
        
        // Read ini file and restore switch states
        mini_t *ini_read = mini_try_load("./lvgl.ini");
        int backlight_timeout = mini_get_bool(ini_read, "lvgl", "backlight_timeout", 0);
        g_backlight_timeout = backlight_timeout;
        int backlight_wakeup = mini_get_bool(ini_read, "lvgl", "backlight_wakeup", 0);
        int ptt_macsec = mini_get_bool(ini_read, "lvgl", "ptt_macsec", 0);
        int ptt_wan = mini_get_bool(ini_read, "lvgl", "ptt_wan", 0);
        mini_free(ini_read);
        
        if (backlight_timeout)
            lv_obj_add_state(switch_objects[SWITCH_BACKLIGHT], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(switch_objects[SWITCH_BACKLIGHT], LV_STATE_CHECKED);

        if (backlight_wakeup)
            lv_obj_add_state(switch_objects[SWITCH_BACKLIGHT_WAKEUP], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(switch_objects[SWITCH_BACKLIGHT_WAKEUP], LV_STATE_CHECKED);
            
        if (ptt_macsec)
            lv_obj_add_state(switch_objects[SWITCH_PTT_TO_MACSEC], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(switch_objects[SWITCH_PTT_TO_MACSEC], LV_STATE_CHECKED);
            
        if (ptt_wan)
            lv_obj_add_state(switch_objects[SWITCH_PTT_TO_WAN], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(switch_objects[SWITCH_PTT_TO_WAN], LV_STATE_CHECKED);

    // Set start tab
    lv_tabview_set_active(tabview, 0, LV_ANIM_OFF);
    
    // Start polling IP address for macsec0
    lv_timer_create(update_macsec_ip_cb, 1000, NULL);
}


int main(int argc, char **argv)
{
    /* Initialize LVGL. */
    lv_init();
    /* Create fifo's */
    fifo_init();
    /* Initialize the FBDEV */
    lv_linux_disp_init();
    /* Create tab view */
    lv_create_tab_view();
    /* Uptime and icons*/
    lv_set_system_layer();
    
    /* fifo thread */
    pthread_t fifo_thread;
    pthread_create(&fifo_thread, NULL, fifo_reader_thread, NULL);
    
    /* Screen timeout thread */
    atomic_store(&last_touch_time, time(NULL));
    pthread_t timeout_thread;
    pthread_create(&timeout_thread, NULL, screen_timeout_thread, NULL);
    
    /* macsec0 speed monitor */
    pthread_t netmon_thread;
    pthread_create(&netmon_thread, NULL, network_monitoring_thread, NULL);

    /* timer handler for examples */
    while (1)
    {
        lv_timer_handler();
        usleep(5000);
    }
    return 0;
}
 
