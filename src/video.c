#include "video.h"
#include <gst/video/video.h>
#include <string.h>

gboolean app_video_update(AppVideo *view, GstAppSink *sink)
{
    if (!sink) return FALSE;
    GstSample *sample = gst_app_sink_try_pull_sample(sink, 0);
    if (!sample) return FALSE;
    GstVideoInfo info;
    GstVideoFrame mapped;
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    gst_video_info_init(&info);
    if (!caps || !buffer || !gst_video_info_from_caps(&info, caps) ||
        GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_RGB16 ||
        GST_VIDEO_INFO_WIDTH(&info) < 1 || GST_VIDEO_INFO_WIDTH(&info) > 640 ||
        GST_VIDEO_INFO_HEIGHT(&info) < 1 || GST_VIDEO_INFO_HEIGHT(&info) > 480 ||
        !gst_video_frame_map(&mapped, &info, buffer, GST_MAP_READ)) {
        g_warning("Invalid decoded video frame");
        gst_sample_unref(sample);
        return FALSE;
    }
    int width = GST_VIDEO_INFO_WIDTH(&info), height = GST_VIDEO_INFO_HEIGHT(&info);
    int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&mapped, 0);
    const uint8_t *data = GST_VIDEO_FRAME_PLANE_DATA(&mapped, 0);
    for (int y = 0; y < height; ++y)
        memcpy(view->pixels + y * width * 2, data + y * stride, width * 2);
    gst_video_frame_unmap(&mapped);
    gst_sample_unref(sample);
    view->frame = (lv_image_dsc_t){
        .header = {.magic=LV_IMAGE_HEADER_MAGIC, .cf=LV_COLOR_FORMAT_RGB565,
            .flags=LV_IMAGE_FLAGS_MODIFIABLE, .w=width, .h=height, .stride=width*2},
        .data_size=width*height*2, .data=view->pixels,
    };
    lv_image_set_src(view->image, &view->frame);
    lv_obj_invalidate(view->image);
    lv_obj_remove_flag(view->image, LV_OBJ_FLAG_HIDDEN);
    return TRUE;
}
