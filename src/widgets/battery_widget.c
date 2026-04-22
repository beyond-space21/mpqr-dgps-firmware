#include "widgets/battery_widget.h"

#define BATTERY_ARC_SIZE 180
#define BATTERY_ARC_WIDTH 14

struct battery_widget {
    lv_obj_t *root;
    lv_obj_t *arc;
    lv_obj_t *value_label;
};

static void battery_widget_delete_cb(lv_event_t *e)
{
    battery_widget_t *widget = (battery_widget_t *)lv_event_get_user_data(e);
    if (widget) {
        lv_mem_free(widget);
    }
}

battery_widget_t *battery_widget_create(lv_obj_t *parent)
{
    battery_widget_t *widget = lv_mem_alloc(sizeof(battery_widget_t));
    if (!widget) {
        return NULL;
    }

    widget->root = lv_obj_create(parent);
    lv_obj_set_size(widget->root, 230, 230);
    lv_obj_center(widget->root);
    lv_obj_clear_flag(widget->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(widget->root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(widget->root, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->root, 0, LV_PART_MAIN);

    widget->arc = lv_arc_create(widget->root);
    lv_obj_set_size(widget->arc, BATTERY_ARC_SIZE, BATTERY_ARC_SIZE);
    lv_obj_center(widget->arc);
    lv_arc_set_range(widget->arc, 0, 100);
    lv_arc_set_bg_angles(widget->arc, 135, 45);
    lv_arc_set_rotation(widget->arc, 0);
    lv_arc_set_value(widget->arc, 0);
    lv_obj_remove_style(widget->arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(widget->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(widget->arc, BATTERY_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(widget->arc, lv_color_hex(0x2F3A40), LV_PART_MAIN);
    lv_obj_set_style_arc_width(widget->arc, BATTERY_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(widget->arc, lv_color_hex(0x25F08A), LV_PART_INDICATOR);

    lv_obj_t *bolt = lv_label_create(widget->root);
    lv_label_set_text(bolt, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(bolt, lv_color_hex(0xB8FFD8), LV_PART_MAIN);
    lv_obj_align(bolt, LV_ALIGN_CENTER, 0, -14);

    widget->value_label = lv_label_create(widget->root);
    lv_obj_set_style_text_color(widget->value_label, lv_color_hex(0xE8FFF3), LV_PART_MAIN);
    lv_label_set_text(widget->value_label, "0%");
    lv_obj_align(widget->value_label, LV_ALIGN_CENTER, 0, 20);

    lv_obj_add_event_cb(widget->root, battery_widget_delete_cb, LV_EVENT_DELETE, widget);
    return widget;
}

void battery_widget_set_visible(battery_widget_t *widget, bool visible)
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

void battery_widget_render(battery_widget_t *widget, uint8_t battery_percentage)
{
    if (!widget) {
        return;
    }
    if (battery_percentage > 100) {
        battery_percentage = 100;
    }

    lv_arc_set_value(widget->arc, battery_percentage);
    if (battery_percentage <= 20) {
        lv_obj_set_style_arc_color(widget->arc, lv_color_hex(0xFF4D4D), LV_PART_INDICATOR);
    } else if (battery_percentage <= 50) {
        lv_obj_set_style_arc_color(widget->arc, lv_color_hex(0xFFD166), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_arc_color(widget->arc, lv_color_hex(0x25F08A), LV_PART_INDICATOR);
    }

    lv_label_set_text_fmt(widget->value_label, "%u%%", battery_percentage);
}
