#include "ui.h"

static void add_button(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, LV_PCT(44), 88);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_24, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    /* Deliberately no event handler: connection support comes later. */
}

void app_ui_create(lv_obj_t *screen)
{
    lv_obj_t *tabs = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(tabs, 48);
    lv_obj_t *comm = lv_tabview_add_tab(tabs, "Comm");
    lv_obj_t *status = lv_tabview_add_tab(tabs, "Status");
    lv_obj_t *settings = lv_tabview_add_tab(tabs, "Settings");
    lv_obj_set_flex_flow(comm, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(comm, LV_FLEX_ALIGN_SPACE_EVENLY,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *logo = lv_image_create(comm);
    lv_image_set_src(logo, "A:/link.png");
    lv_obj_set_size(logo, LV_PCT(100), 101);
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_CONTAIN);

    lv_obj_t *row = lv_obj_create(comm);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    add_button(row, "Connect", 0x217846);
    add_button(row, "Disconnect", 0xB83B3B);

    lv_obj_t *label = lv_label_create(status);
    lv_label_set_text(label, "Connection controls are placeholders.");
    lv_obj_center(label);
    label = lv_label_create(settings);
    lv_label_set_text(label, "Settings will be available here.");
    lv_obj_center(label);
    lv_tabview_set_active(tabs, 0, LV_ANIM_OFF);
}
