#ifndef APP_OPTIONS_H
#define APP_OPTIONS_H
#include <glib.h>

typedef struct {
    char *peer, *bind_address, *audio_input, *audio_output;
    int video_port, audio_port, text_port, rtp_mtu;
    gboolean start, test_media;
} AppOptions;

gboolean app_options_parse(AppOptions *options, int *argc, char ***argv, GError **error);
void app_options_clear(AppOptions *options);
#endif
