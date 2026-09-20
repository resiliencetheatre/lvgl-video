#include "ui.h"
#include "camera.h"

static void create_button(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    /* Intentionally no callbacks until call controls are implemented. */
}

void app_ui_create(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10151e), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xe8edf4), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_set_style_pad_row(screen, 12, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *video = lv_obj_create(screen);
    lv_obj_set_width(video, LV_PCT(100));
    lv_obj_set_flex_grow(video, 1);
    lv_obj_remove_flag(video, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(video, lv_color_hex(0x1b2533), 0);
    lv_obj_set_style_border_color(video, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(video, 1, 0);
    lv_obj_set_style_radius(video, 16, 0);

    lv_obj_t *image = lv_image_create(video);
    lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(image);

    lv_obj_t *label = lv_label_create(video);
    lv_label_set_text(label, "Starting camera...");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    app_camera_start(image, label);

    lv_obj_t *controls = lv_obj_create(screen);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_PCT(100), 56);
    lv_obj_set_style_pad_column(controls, 12, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    create_button(controls, "Mic", 0x334155);
    create_button(controls, "Camera", 0x334155);
    create_button(controls, "End call", 0xa83246);
}
