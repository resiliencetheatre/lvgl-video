#include "session.h"
#include "video.h"
#include <string.h>

char *gtk_reference_pipeline(int video_port, int audio_port);

static GSocket *bind_socket(const char *ip, int port)
{
    GError *error = NULL;
    GInetAddress *address = g_inet_address_new_from_string(ip);
    GSocketAddress *local = g_inet_socket_address_new(address, port);
    GSocket *socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error);
    g_assert_no_error(error);
    g_assert_true(g_socket_bind(socket, local, FALSE, &error));
    g_assert_no_error(error);
    g_object_unref(address); g_object_unref(local);
    g_socket_set_blocking(socket, FALSE);
    return socket;
}

static int socket_port(GSocket *socket)
{
    GSocketAddress *address = g_socket_get_local_address(socket, NULL);
    int port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(address));
    g_object_unref(address);
    return port;
}

static void send_message(GSocket *socket, int port, const char *message)
{
    GInetAddress *ip = g_inet_address_new_from_string("127.0.0.1");
    GSocketAddress *dest = g_inet_socket_address_new(ip, port);
    g_assert_cmpint(g_socket_send_to(socket, dest, message, strlen(message), NULL, NULL), ==, strlen(message));
    g_object_unref(dest); g_object_unref(ip);
}

static gint audio_buffers;
static GstPadProbeReturn audio_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void)pad; (void)info; (void)data;
    g_atomic_int_inc(&audio_buffers);
    return GST_PAD_PROBE_OK;
}

static void no_display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area; (void)pixels;
    lv_display_flush_ready(display);
}

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);
    lv_init();
    lv_display_t *display = lv_display_create(800, 480);
    static uint8_t draw_buffer[800*48*2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, no_display_flush);
    static AppVideo view;
    view.image = lv_image_create(lv_display_get_screen_active(display));
    GSocket *reserved[3];
    int ports[3];
    for (int i=0; i<3; ++i) { reserved[i] = bind_socket("127.0.0.1", 0); ports[i] = socket_port(reserved[i]); }
    for (int i=0; i<3; ++i) g_object_unref(reserved[i]);
    AppOptions options = {.peer="127.0.0.2", .bind_address="127.0.0.1", .audio_input="default", .audio_output="default",
        .video_port=ports[0], .audio_port=ports[1], .text_port=ports[2], .rtp_mtu=1100, .test_media=TRUE};
    AppSession session;
    GError *error = NULL;
    g_assert_true(app_session_init(&session, &options, &error));
    g_assert_no_error(error);
    GSocket *control = bind_socket("127.0.0.2", ports[2]);
    send_message(control, ports[2], "GTKPIPE/1 STREAM_STARTED");
    g_usleep(10000); app_session_poll(&session);
    g_assert_null(session.pipeline);
    g_assert_nonnull(strstr(session.status, "Incoming"));
    g_assert_true(app_session_reachable(&session));
    send_message(control, ports[2], "GTKPIPE/1 TEXT Hello from GTK Pipe");
    g_usleep(10000); app_session_poll(&session);
    g_assert_cmpstr(session.text, ==, "Hello from GTK Pipe");
    g_assert_true(app_session_start(&session));
    GstElement *audio = gst_bin_get_by_name(GST_BIN(session.pipeline), "audio_playback");
    GstPad *pad = gst_element_get_static_pad(audio, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, audio_probe, NULL, NULL);
    gst_object_unref(pad); gst_object_unref(audio);
    char *description = gtk_reference_pipeline(ports[0], ports[1]);
    GstElement *reference = gst_parse_launch(description, &error);
    g_free(description); g_assert_no_error(error); g_assert_nonnull(reference);
    g_assert_cmpint(gst_element_set_state(reference, GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);
    GstAppSink *ref_video = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(reference), "remote_video"));
    GstAppSink *ref_audio = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(reference), "remote_audio"));
    int received_video = 0, sent_video = 0, sent_audio = 0, preview = 0;
    gboolean announced = FALSE, pong = FALSE;
    send_message(control, ports[2], "GTKPIPE/1 PING");
    for (int i=0; i<800 && (received_video<5 || sent_video<5 || sent_audio<5 || g_atomic_int_get(&audio_buffers)<5); ++i) {
        app_session_poll(&session);
        g_assert_nonnull(session.pipeline);
        if (app_video_update(&view, session.remote)) ++received_video;
        GstSample *sample = gst_app_sink_try_pull_sample(session.local, 0);
        if (sample) { ++preview; gst_sample_unref(sample); }
        sample = gst_app_sink_try_pull_sample(ref_video, 0);
        if (sample) { ++sent_video; gst_sample_unref(sample); }
        sample = gst_app_sink_try_pull_sample(ref_audio, 0);
        if (sample) { ++sent_audio; gst_sample_unref(sample); }
        char message[128];
        gssize size = g_socket_receive(control, message, sizeof(message)-1, NULL, NULL);
        if (size > 0) { message[size]=0; announced |= !strcmp(message, "GTKPIPE/1 STREAM_STARTED"); pong |= !strcmp(message, "GTKPIPE/1 PONG"); }
        lv_tick_inc(10); lv_timer_handler(); g_usleep(10000);
    }
    g_assert_cmpint(received_video, >=, 5); g_assert_cmpint(sent_video, >=, 5);
    g_assert_cmpint(sent_audio, >=, 5); g_assert_cmpint(g_atomic_int_get(&audio_buffers), >=, 5);
    g_assert_cmpint(preview, >, 0); g_assert_true(announced); g_assert_true(pong);
    g_assert_cmpint(view.frame.header.w, ==, 640); g_assert_cmpint(view.frame.header.h, ==, 480);
    app_session_mute(&session, TRUE);
    gboolean muted = FALSE; g_object_get(session.microphone, "mute", &muted, NULL); g_assert_true(muted);
    send_message(control, ports[2], "GTKPIPE/1 STREAM_END");
    g_usleep(10000); app_session_poll(&session);
    g_assert_null(session.pipeline); g_assert_nonnull(strstr(session.status, "Remote disconnected"));
    g_assert_true(app_session_start(&session));
    app_session_stop(&session, TRUE);
    app_session_close(&session);
    gst_element_set_state(reference, GST_STATE_NULL);
    gst_object_unref(ref_video); gst_object_unref(ref_audio); gst_object_unref(reference);
    g_object_unref(control);
    lv_deinit(); gst_deinit();
    g_print("GTK Pipe interoperability: decoded video/audio both ways, LVGL rendering, preview, control, mute, restart passed\n");
    return 0;
}
