#include "camera.h"

#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 640
#define HEIGHT 480
#define ROW_BYTES (WIDTH * 2)

static GstElement *pipeline;
static GstAppSink *sink;
static GstBus *bus;
static lv_obj_t *video_image;
static lv_obj_t *status_label;
static uint8_t pixels[ROW_BYTES * HEIGHT];
static lv_image_dsc_t frame;

static void report_error(const char *message)
{
    fprintf(stderr, "Camera: %s\n", message);
    lv_label_set_text(status_label, "Camera unavailable\nSee application log");
    lv_obj_remove_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

void app_camera_stop(void)
{
    if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
    if (bus) gst_object_unref(bus);
    if (sink) gst_object_unref(sink);
    if (pipeline) gst_object_unref(pipeline);
    pipeline = NULL;
    sink = NULL;
    bus = NULL;
}

void app_camera_start(lv_obj_t *image, lv_obj_t *status)
{
    video_image = image;
    status_label = status;
    GError *error = NULL;
    if (!gst_init_check(NULL, NULL, &error)) {
        report_error(error ? error->message : "GStreamer initialization failed");
        g_clear_error(&error);
        return;
    }

    /* The test pattern exercises the same conversion and presentation path. */
    const char *test = getenv("LVGL_VIDEO_TEST_PATTERN");
    const char *source = test && strcmp(test, "1") == 0
        ? "videotestsrc is-live=true"
        : "libcamerasrc";
    gchar *description = g_strdup_printf(
        "%s ! video/x-raw,format=NV12,width=640,height=480,"
        "framerate=10/1,colorimetry=bt709 ! "
        "queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! "
        "videoconvert ! video/x-raw,format=RGB16 ! "
        "appsink name=camera_sink max-buffers=1 drop=true sync=false", source);
    pipeline = gst_parse_launch(description, &error);
    g_free(description);
    if (error || !pipeline) {
        report_error(error ? error->message : "Could not create camera pipeline");
        g_clear_error(&error);
        app_camera_stop();
        return;
    }
    sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline), "camera_sink"));
    bus = gst_element_get_bus(pipeline);
    if (!sink || !bus || gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        report_error("Could not start camera pipeline");
        app_camera_stop();
    }
}

void app_camera_poll(void)
{
    if (!pipeline) return;

    GstMessage *message;
    while ((message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(message, &error, &debug);
            report_error(error ? error->message : "Pipeline error");
            if (debug) fprintf(stderr, "Camera details: %s\n", debug);
            g_clear_error(&error);
            g_free(debug);
        } else {
            report_error("Camera stream ended");
        }
        gst_message_unref(message);
        app_camera_stop();
        return;
    }

    /* Nonblocking: an absent frame must not stall touch handling or rendering. */
    GstSample *sample = gst_app_sink_try_pull_sample(sink, 0);
    if (!sample) return;
    GstVideoInfo info;
    GstVideoFrame mapped;
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    gst_video_info_init(&info);
    if (!caps || !buffer || !gst_video_info_from_caps(&info, caps) ||
        GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_RGB16 ||
        GST_VIDEO_INFO_WIDTH(&info) != WIDTH || GST_VIDEO_INFO_HEIGHT(&info) != HEIGHT ||
        !gst_video_frame_map(&mapped, &info, buffer, GST_MAP_READ)) {
        report_error("Invalid camera frame");
        gst_sample_unref(sample);
        app_camera_stop();
        return;
    }

    const uint8_t *data = GST_VIDEO_FRAME_PLANE_DATA(&mapped, 0);
    int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&mapped, 0);
    /* Copy rows so LVGL owns stable pixels independent of camera buffer reuse. */
    for (int y = 0; y < HEIGHT; ++y)
        memcpy(pixels + y * ROW_BYTES, data + y * stride, ROW_BYTES);
    gst_video_frame_unmap(&mapped);
    gst_sample_unref(sample);

    frame = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = LV_IMAGE_FLAGS_MODIFIABLE,
            .w = WIDTH,
            .h = HEIGHT,
            .stride = ROW_BYTES,
        },
        .data_size = sizeof(pixels),
        .data = pixels,
    };
    lv_image_set_src(video_image, &frame);
    lv_obj_invalidate(video_image);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}
