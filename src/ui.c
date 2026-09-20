#include "ui.h"
#include "video.h"
#include <string.h>

static AppSession *session;
static AppVideo remote_view, local_view;
static lv_obj_t *status, *notice, *mic_label, *start_button, *stop_button;
static gboolean showing_frame;
static gint64 last_frame;

static void clear_images(void)
{
    lv_obj_add_flag(remote_view.image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(local_view.image, LV_OBJ_FLAG_HIDDEN);
    showing_frame = FALSE;
}

static void start_clicked(lv_event_t *event)
{
    (void)event;
    clear_images();
    app_session_start(session);
}

static void stop_clicked(lv_event_t *event)
{
    (void)event;
    app_session_stop(session, TRUE);
    clear_images();
}

static void mute_clicked(lv_event_t *event)
{
    (void)event;
    app_session_mute(session, !session->muted);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void app_ui_create(lv_obj_t *screen, AppSession *s)
{
    session = s;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10151e), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xe8edf4), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_set_style_pad_row(screen, 8, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    status = lv_label_create(screen);
    lv_obj_set_width(status, LV_PCT(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_label_set_text(status, "Ready");

    lv_obj_t *video = lv_obj_create(screen);
    lv_obj_set_width(video, LV_PCT(100));
    lv_obj_set_flex_grow(video, 1);
    lv_obj_remove_flag(video, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(video, lv_color_hex(0x1b2533), 0);
    lv_obj_set_style_border_width(video, 0, 0);
    lv_obj_set_style_pad_all(video, 0, 0);
    remote_view.image = lv_image_create(video);
    lv_obj_set_size(remote_view.image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(remote_view.image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(remote_view.image);
    local_view.image = lv_image_create(video);
    lv_obj_set_size(local_view.image, 160, 120);
    lv_image_set_inner_align(local_view.image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_align(local_view.image, LV_ALIGN_TOP_RIGHT, -4, 4);

    notice = lv_label_create(video);
    lv_label_set_text(notice, "Press Start");
    lv_obj_set_width(notice, LV_PCT(90));
    lv_obj_set_style_text_align(notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(notice);
    clear_images();

    lv_obj_t *controls = lv_obj_create(screen);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_PCT(100), 56);
    lv_obj_set_style_pad_column(controls, 12, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *mic = create_button(controls, "Mute mic", 0x334155, mute_clicked);
    mic_label = lv_obj_get_child(mic, 0);
    if (!s->options->peer) lv_obj_add_state(mic, LV_STATE_DISABLED);
    start_button = create_button(controls, "Start", 0x334155, start_clicked);
    stop_button = create_button(controls, "End call", 0xa83246, stop_clicked);
}

void app_ui_poll(void)
{
    app_session_poll(session);
    if (!session->pipeline) clear_images();
    if (app_video_update(&remote_view, session->remote)) {
        showing_frame = TRUE;
        last_frame = g_get_monotonic_time();
    }
    if (showing_frame && g_get_monotonic_time() - last_frame > 3 * G_USEC_PER_SEC) {
        showing_frame = FALSE;
        lv_obj_add_flag(remote_view.image, LV_OBJ_FLAG_HIDDEN);
    }
    app_video_update(&local_view, session->local);
    const char *mic_text = session->muted ? "Unmute mic" : "Mute mic";
    if (strcmp(lv_label_get_text(mic_label), mic_text)) lv_label_set_text(mic_label, mic_text);
    if (session->pipeline) {
        lv_obj_add_state(start_button, LV_STATE_DISABLED);
        lv_obj_remove_state(stop_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(start_button, LV_STATE_DISABLED);
        lv_obj_add_state(stop_button, LV_STATE_DISABLED);
    }
    char line[512];
    g_snprintf(line, sizeof(line), "%s%s%s", session->options->peer
        ? (app_session_reachable(session) ? "Peer online | " : "Peer offline | ") : "",
        showing_frame ? (session->options->peer ? "Streaming" : "Local preview") : session->status,
        session->muted && session->options->peer ? " | Mic muted" : "");
    if (strcmp(lv_label_get_text(status), line)) lv_label_set_text(status, line);
    if (showing_frame) lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN);
    else {
        if (strcmp(lv_label_get_text(notice), session->status)) lv_label_set_text(notice, session->status);
        lv_obj_remove_flag(notice, LV_OBJ_FLAG_HIDDEN);
    }
}
