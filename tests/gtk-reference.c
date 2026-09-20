/* Reuse the inspected GTK Pipe pipeline builder verbatim. Unused GTK UI and
 * secure-mode sections are discarded by the test link, never shipped. */
#define main gtk_pipe_unused_main
#include GTK_PIPE_SOURCE
#undef main

static char *replace(char *input, const char *from, const char *to)
{
    char **parts = g_strsplit(input, from, -1);
    char *result = g_strjoinv(to, parts);
    g_strfreev(parts);
    g_free(input);
    return result;
}

char *gtk_reference_pipeline(int video_port, int audio_port)
{
    App app = {.rtp_mtu=1400, .site_name="interop-test", .bind_address="127.0.0.2",
        .video_source="videotestsrc is-live=true pattern=smpte", .echo_cancellation=FALSE,
        .video_port=video_port, .audio_port=audio_port,
        .video_modes=g_array_new(FALSE, FALSE, sizeof(VideoMode))};
    VideoMode mode = {320, 240, 15, 1};
    g_array_append_val(app.video_modes, mode);
    char *description = make_pipeline(&app, "127.0.0.1");
    g_array_unref(app.video_modes);
    description = replace(description, "autoaudiosrc", "audiotestsrc is-live=true volume=0.1");
    description = replace(description, "gtksink name=remote_video sync=false",
        "appsink name=remote_video max-buffers=1 drop=true sync=false async=false");
    description = replace(description, "gtksink name=local_preview sync=false qos=false",
        "fakesink sync=false async=false");
    description = replace(description, "autoaudiosink sync=false",
        "appsink name=remote_audio max-buffers=1 drop=true sync=false async=false");
    return description;
}
