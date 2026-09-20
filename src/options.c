#include "options.h"
#include <gio/gio.h>

gboolean app_options_parse(AppOptions *o, int *argc, char ***argv, GError **error)
{
    *o = (AppOptions){.video_port=5000, .audio_port=5002, .text_port=5004, .rtp_mtu=1400};
    GOptionEntry entries[] = {
        {"peer", 0, 0, G_OPTION_ARG_STRING, &o->peer, "Peer numeric IPv4/IPv6 address; omit for local preview", "IP"},
        {"bind", 0, 0, G_OPTION_ARG_STRING, &o->bind_address, "Local receive address", "IP"},
        {"video-port", 0, 0, G_OPTION_ARG_INT, &o->video_port, "VP8 RTP UDP port (5000)", "PORT"},
        {"audio-port", 0, 0, G_OPTION_ARG_INT, &o->audio_port, "Opus RTP UDP port (5002)", "PORT"},
        {"text-port", 0, 0, G_OPTION_ARG_INT, &o->text_port, "GTK Pipe control/text UDP port (5004)", "PORT"},
        {"rtp-mtu", 0, 0, G_OPTION_ARG_INT, &o->rtp_mtu, "Maximum outgoing video RTP packet bytes (1400)", "BYTES"},
        {"audio-input", 0, 0, G_OPTION_ARG_STRING, &o->audio_input, "ALSA capture device (default)", "DEVICE"},
        {"audio-output", 0, 0, G_OPTION_ARG_STRING, &o->audio_output, "ALSA playback device (default)", "DEVICE"},
        {"start", 0, 0, G_OPTION_ARG_NONE, &o->start, "Start media immediately in peer mode", NULL},
        {"disable-echo-cancellation", 0, 0, G_OPTION_ARG_NONE, &o->disable_echo_cancellation, "Disable echo cancellation and capture DSP for headset use", NULL},
        {"test-media", 0, 0, G_OPTION_ARG_NONE, &o->test_media, "Use test video/tone and discard received audio", NULL},
        {NULL}
    };
    GOptionContext *context = g_option_context_new("- fixed 640x480/10 fps LVGL camera and RTP peer");
    g_option_context_add_main_entries(context, entries, NULL);
    gboolean ok = g_option_context_parse(context, argc, argv, error);
    g_option_context_free(context);
    if (!ok) return FALSE;
    if (*argc != 1) goto invalid;
    if (o->video_port < 1 || o->video_port > 65535 || o->audio_port < 1 || o->audio_port > 65535 ||
        o->text_port < 1 || o->text_port > 65535 || o->video_port == o->audio_port ||
        o->video_port == o->text_port || o->audio_port == o->text_port ||
        o->rtp_mtu < 28 || o->rtp_mtu > 65507) goto invalid;
    GInetAddress *peer = o->peer ? g_inet_address_new_from_string(o->peer) : NULL;
    GInetAddress *local = o->bind_address ? g_inet_address_new_from_string(o->bind_address) : NULL;
    ok = (!o->peer || peer) && (!o->bind_address || local) &&
         (!local || (peer && g_inet_address_get_family(local) == g_inet_address_get_family(peer)));
    if (ok && !o->bind_address)
        o->bind_address = g_strdup(peer && g_inet_address_get_family(peer) == G_SOCKET_FAMILY_IPV6 ? "::" : "0.0.0.0");
    g_clear_object(&peer);
    g_clear_object(&local);
    if (!ok) goto invalid;
    if (!o->audio_input) o->audio_input = g_strdup("default");
    if (!o->audio_output) o->audio_output = g_strdup("default");
    return TRUE;
invalid:
    g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
        "Use numeric peer/bind addresses of the same family, three distinct ports (1..65535), and RTP MTU 28..65507");
    return FALSE;
}

void app_options_clear(AppOptions *o)
{
    g_free(o->peer); g_free(o->bind_address);
    g_free(o->audio_input); g_free(o->audio_output);
}
