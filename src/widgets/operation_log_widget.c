#include "widgets/operation_log_widget.h"

struct operation_log_widget {
    lv_obj_t *root;
    lv_obj_t *title_label;
    lv_obj_t *text_label;
};

static void operation_log_delete_cb(lv_event_t *e)
{
    operation_log_widget_t *widget = (operation_log_widget_t *)lv_event_get_user_data(e);
    if (widget) {
        lv_mem_free(widget);
    }
}

operation_log_widget_t *operation_log_widget_create(lv_obj_t *parent)
{
    operation_log_widget_t *widget = lv_mem_alloc(sizeof(operation_log_widget_t));
    if (!widget) {
        return NULL;
    }

    widget->root = lv_obj_create(parent);
    lv_obj_set_size(widget->root, 380, 470);
    lv_obj_center(widget->root);
    lv_obj_clear_flag(widget->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(widget->root, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_color(widget->root, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(widget->root, 10, LV_PART_MAIN);

    widget->title_label = lv_label_create(widget->root);
    lv_label_set_text(widget->title_label, "Operation Mode");
    lv_obj_set_style_text_color(widget->title_label, lv_color_hex(0x9EC7FF), LV_PART_MAIN);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_LEFT, 12, 12);

    widget->text_label = lv_label_create(widget->root);
    lv_obj_set_width(widget->text_label, 356);
    lv_obj_set_style_text_font(widget->text_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->text_label, lv_color_hex(0xD8DEE9), LV_PART_MAIN);
    lv_label_set_long_mode(widget->text_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(widget->text_label, "waiting...");
    lv_obj_align(widget->text_label, LV_ALIGN_TOP_LEFT, 12, 48);

    lv_obj_add_event_cb(widget->root, operation_log_delete_cb, LV_EVENT_DELETE, widget);
    return widget;
}

void operation_log_widget_set_visible(operation_log_widget_t *widget, bool visible)
{
    if (!widget) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(widget->root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(widget->root, LV_OBJ_FLAG_HIDDEN);
    }
}

void operation_log_widget_render(operation_log_widget_t *widget, const char *text)
{
    if (!widget || !text) {
        return;
    }
    lv_label_set_text(widget->text_label, text);
}
