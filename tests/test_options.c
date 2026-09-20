#include "options.h"
#include <string.h>

static void parse(const char *arguments, gboolean expected)
{
    char **argv;
    int argc;
    GError *error = NULL;
    g_assert_true(g_shell_parse_argv(arguments, &argc, &argv, &error));
    /* GOption removes consumed entries; retain the allocation pointers. */
    char **allocated = g_strdupv(argv);
    g_strfreev(argv);
    argv = g_new0(char *, argc + 1);
    for (int i=0; i<argc; ++i) argv[i] = allocated[i];
    AppOptions options;
    gboolean result = app_options_parse(&options, &argc, &argv, &error);
    g_assert_cmpint(result, ==, expected);
    if (result) { g_assert_no_error(error); g_assert_cmpint(argc, ==, 1);
        g_assert_cmpint(options.disable_echo_cancellation, ==, strstr(arguments, "--disable-echo-cancellation") != NULL); }
    else g_assert_nonnull(error);
    g_clear_error(&error);
    app_options_clear(&options);
    g_strfreev(allocated); g_free(argv);
}

int main(void)
{
    parse("lvgl-video", TRUE);
    parse("lvgl-video --peer 192.0.2.1 --disable-echo-cancellation", TRUE);
    parse("lvgl-video --peer 192.0.2.1 --bind 192.0.2.2 --rtp-mtu 1100 --start --audio-input plughw:1,0", TRUE);
    parse("lvgl-video --peer ::1 --bind ::1", TRUE);
    parse("lvgl-video --peer localhost", FALSE);
    parse("lvgl-video --peer 192.0.2.1 --bind ::1", FALSE);
    parse("lvgl-video --peer 192.0.2.1 --video-port 5002", FALSE);
    parse("lvgl-video --audio-port 0", FALSE);
    parse("lvgl-video --text-port 65536", FALSE);
    parse("lvgl-video --rtp-mtu 27", FALSE);
    parse("lvgl-video --rtp-mtu 65508", FALSE);
    parse("lvgl-video --secure", FALSE);
    parse("lvgl-video --quality 1", FALSE);
    parse("lvgl-video extra", FALSE);
    return 0;
}
