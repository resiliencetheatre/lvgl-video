#ifndef APP_SESSION_H
#define APP_SESSION_H
#include "options.h"
#include <gio/gio.h>
#include <gst/app/gstappsink.h>

/* Owned by the UI thread. GStreamer alone runs capture/codec worker threads. */
typedef struct {
    const AppOptions *options;
    GstElement *pipeline, *microphone;
    GstAppSink *remote, *local;
    GstBus *bus;
    GSocket *control;
    gint64 last_seen, last_ping;
    gboolean muted;
    char status[256], text[1100];
} AppSession;

gboolean app_session_init(AppSession *s, const AppOptions *options, GError **error);
gboolean app_session_start(AppSession *s);
void app_session_stop(AppSession *s, gboolean notify);
void app_session_poll(AppSession *s);
void app_session_mute(AppSession *s, gboolean muted);
void app_session_close(AppSession *s);
gboolean app_session_reachable(const AppSession *s);
/* Also used by the interoperability test to exercise the production pipeline. */
char *app_session_pipeline(const AppOptions *options);
#endif
