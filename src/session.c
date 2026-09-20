#include "session.h"
#include <stdio.h>
#include <string.h>

#define PING "GTKPIPE/1 PING"
#define PONG "GTKPIPE/1 PONG"
#define START "GTKPIPE/1 STREAM_STARTED"
#define END "GTKPIPE/1 STREAM_END"
#define TEXT "GTKPIPE/1 TEXT "
#define FRAME_SINK "max-buffers=1 drop=true sync=false async=false"
#define LATEST_QUEUE "queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream"

char *app_session_pipeline(const AppOptions *o)
{
    const char *pattern = g_getenv("LVGL_VIDEO_TEST_PATTERN");
    const char *source = o->test_media || (pattern && !strcmp(pattern, "1"))
        ? "videotestsrc is-live=true pattern=ball" : "libcamerasrc";
    char *capture = g_strdup_printf("%s ! video/x-raw,format=NV12,width=640,height=480,"
        "framerate=10/1,colorimetry=bt709 ! " LATEST_QUEUE " ! videoconvert", source);
    if (!o->peer) {
        char *result = g_strdup_printf("%s ! video/x-raw,format=RGB16 ! appsink name=remote_video " FRAME_SINK, capture);
        g_free(capture);
        return result;
    }
    const char *capture_dsp = o->disable_echo_cancellation ? "" :
        "webrtcdsp name=echo_cancel probe=echo_probe echo-cancel=true gain-control=false "
        "noise-suppression=true high-pass-filter=true ! ";
    const char *playback_probe = o->disable_echo_cancellation ? "" :
        "webrtcechoprobe name=echo_probe ! ";
    char *input = g_strescape(o->audio_input, NULL);
    char *output = g_strescape(o->audio_output, NULL);
    char *audio_source = o->test_media ? g_strdup("audiotestsrc is-live=true volume=0.1")
        : g_strdup_printf("alsasrc device=\"%s\"", input);
    char *audio_sink = o->test_media ? g_strdup("fakesink name=audio_playback sync=false async=false")
        : g_strdup_printf("alsasink name=audio_playback device=\"%s\" sync=false async=false", output);
    char *result = g_strdup_printf(
        "%s ! video/x-raw,format=I420 ! tee name=camera "
        "camera. ! " LATEST_QUEUE " ! vp8enc deadline=1 cpu-used=8 threads=2 "
        "target-bitrate=600000 keyframe-max-dist=30 ! rtpvp8pay pt=96 mtu=%d ! "
        "udpsink host=\"%s\" port=%d sync=false async=false "
        "camera. ! " LATEST_QUEUE " ! videoscale ! video/x-raw,width=160,height=120 ! "
        "videoconvert ! video/x-raw,format=RGB16 ! appsink name=local_video " FRAME_SINK " "
        "%s ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=48000,channels=1 ! "
        "%svolume name=microphone ! queue ! opusenc bitrate=32000 inband-fec=true ! "
        "rtpopuspay pt=97 ! udpsink host=\"%s\" port=%d sync=false async=false "
        "udpsrc address=\"%s\" port=%d reuse=false "
        "caps=\"application/x-rtp,media=video,clock-rate=90000,encoding-name=VP8,payload=96\" ! "
        "rtpjitterbuffer latency=120 drop-on-latency=true ! rtpvp8depay ! vp8dec ! "
        "videoconvert ! videoscale add-borders=true ! video/x-raw,format=RGB16,width=640,height=480 ! "
        "appsink name=remote_video " FRAME_SINK " "
        "udpsrc address=\"%s\" port=%d reuse=false "
        "caps=\"application/x-rtp,media=audio,clock-rate=48000,encoding-name=OPUS,payload=97\" ! "
        "rtpjitterbuffer latency=120 drop-on-latency=true ! rtpopusdepay ! opusdec plc=true ! "
        "audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=48000 ! %s%s",
        capture, o->rtp_mtu, o->peer, o->video_port, audio_source, capture_dsp,
        o->peer, o->audio_port, o->bind_address, o->video_port,
        o->bind_address, o->audio_port, playback_probe, audio_sink);
    g_free(capture); g_free(input); g_free(output); g_free(audio_source); g_free(audio_sink);
    return result;
}

static void send_control(AppSession *s, const char *message)
{
    if (s->control) g_socket_send(s->control, message, strlen(message), NULL, NULL);
}

gboolean app_session_init(AppSession *s, const AppOptions *options, GError **error)
{
    *s = (AppSession){.options=options};
    g_strlcpy(s->status, options->peer ? "Ready; press Start" : "Local camera preview", sizeof(s->status));
    if (!options->peer) return TRUE;
    GInetAddress *peer = g_inet_address_new_from_string(options->peer);
    GInetAddress *bind = g_inet_address_new_from_string(options->bind_address);
    GSocketAddress *remote = g_inet_socket_address_new(peer, options->text_port);
    GSocketAddress *local = g_inet_socket_address_new(bind, options->text_port);
    s->control = g_socket_new(g_inet_address_get_family(peer), G_SOCKET_TYPE_DATAGRAM,
                              G_SOCKET_PROTOCOL_UDP, error);
    gboolean ok = s->control && g_socket_bind(s->control, local, FALSE, error) &&
        g_socket_connect(s->control, remote, NULL, error);
    g_object_unref(peer); g_object_unref(bind); g_object_unref(remote); g_object_unref(local);
    if (!ok) { g_clear_object(&s->control); return FALSE; }
    g_socket_set_blocking(s->control, FALSE);
    return TRUE;
}

void app_session_stop(AppSession *s, gboolean notify)
{
    if (s->pipeline && notify) send_control(s, END);
    if (s->pipeline) gst_element_set_state(s->pipeline, GST_STATE_NULL);
    g_clear_pointer(&s->bus, gst_object_unref);
    g_clear_pointer(&s->remote, gst_object_unref);
    g_clear_pointer(&s->local, gst_object_unref);
    g_clear_pointer(&s->microphone, gst_object_unref);
    g_clear_pointer(&s->pipeline, gst_object_unref);
    g_strlcpy(s->status, "Media stopped", sizeof(s->status));
}

static void fail(AppSession *s, const char *message)
{
    fprintf(stderr, "Media: %s\n", message);
    app_session_stop(s, TRUE);
    g_strlcpy(s->status, message, sizeof(s->status));
}

gboolean app_session_start(AppSession *s)
{
    if (s->pipeline) return TRUE;
    GError *error = NULL;
    char *description = app_session_pipeline(s->options);
    s->pipeline = gst_parse_launch(description, &error);
    g_free(description);
    if (error || !s->pipeline) {
        fail(s, error ? error->message : "Could not create pipeline");
        g_clear_error(&error);
        return FALSE;
    }
    s->remote = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(s->pipeline), "remote_video"));
    s->local = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(s->pipeline), "local_video"));
    s->microphone = gst_bin_get_by_name(GST_BIN(s->pipeline), "microphone");
    s->bus = gst_element_get_bus(s->pipeline);
    app_session_mute(s, s->muted);
    if (!s->remote || gst_element_set_state(s->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        fail(s, "Could not start media; check camera and ALSA devices");
        return FALSE;
    }
    send_control(s, START);
    g_strlcpy(s->status, s->options->peer ? "Streaming; waiting for remote video" : "Local preview", sizeof(s->status));
    return TRUE;
}

void app_session_mute(AppSession *s, gboolean muted)
{
    s->muted = muted;
    if (s->microphone) g_object_set(s->microphone, "mute", muted, NULL);
}

gboolean app_session_reachable(const AppSession *s)
{
    return s->last_seen && g_get_monotonic_time() - s->last_seen < 6 * G_USEC_PER_SEC;
}

static void poll_control(AppSession *s)
{
    if (!s->control) return;
    /* Bound work per UI iteration, including malformed or excessive traffic. */
    for (int i = 0; i < 16; ++i) {
        char data[1100];
        GError *error = NULL;
        gssize length = g_socket_receive(s->control, data, sizeof(data), NULL, &error);
        if (length < 0) { g_clear_error(&error); break; }
        if (!length || length > 1024 || memchr(data, 0, length) || !g_utf8_validate(data, length, NULL)) continue;
        data[length] = 0;
        if (!strcmp(data, PING)) send_control(s, PONG);
        else if (!strcmp(data, PONG)) { /* heartbeat response */ }
        else if (!strcmp(data, START)) {
            if (!s->pipeline) g_strlcpy(s->status, "Incoming stream; press Start", sizeof(s->status));
        } else if (!strcmp(data, END)) {
            app_session_stop(s, FALSE);
            g_strlcpy(s->status, "Remote disconnected; press Start to restart", sizeof(s->status));
        } else {
            const char *text = g_str_has_prefix(data, TEXT) ? data + strlen(TEXT) : data;
            g_strlcpy(s->text, text, sizeof(s->text));
            fprintf(stderr, "Peer: %s\n", text);
        }
        s->last_seen = g_get_monotonic_time();
    }
    gint64 now = g_get_monotonic_time();
    if (now - s->last_ping >= 2 * G_USEC_PER_SEC) {
        send_control(s, PING);
        s->last_ping = now;
    }
}

void app_session_poll(AppSession *s)
{
    poll_control(s);
    if (!s->pipeline) return;
    GstMessage *message = gst_bus_pop_filtered(s->bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    if (!message) return;
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = NULL; char *debug = NULL;
        gst_message_parse_error(message, &error, &debug);
        fail(s, error ? error->message : "Pipeline error");
        if (debug) fprintf(stderr, "Media details: %s\n", debug);
        g_clear_error(&error); g_free(debug);
    } else fail(s, "Media stream ended");
    gst_message_unref(message);
}

void app_session_close(AppSession *s)
{
    app_session_stop(s, TRUE);
    g_clear_object(&s->control);
}
