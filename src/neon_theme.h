#ifndef APP_NEON_THEME_H
#define APP_NEON_THEME_H
#include "lvgl.h"

/* Keep the palette here so every tab and nested widget uses the same theme. */
#define NEON_GREEN 0x39FF14
#define DIM_GREEN 0x103D08

static void apply_neon_theme(lv_obj_t *obj)
{
    const lv_color_t green = lv_color_hex(NEON_GREEN);
    const lv_style_selector_t states[] = {
        LV_STATE_DEFAULT, LV_STATE_PRESSED, LV_STATE_CHECKED,
        LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY, LV_STATE_EDITED,
        LV_STATE_HOVERED, LV_STATE_DISABLED
    };
    for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        lv_style_selector_t state = states[i];
        lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, state);
        lv_obj_set_style_bg_color(obj, lv_color_black(), state);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, state);
        lv_obj_set_style_text_color(obj, green, state);
        lv_obj_set_style_border_color(obj, green, state);
        lv_obj_set_style_outline_color(obj, green, state);
        lv_obj_set_style_shadow_color(obj, green, state);
        lv_obj_set_style_shadow_width(obj, 0, state);
        lv_obj_set_style_bg_color(obj, green, LV_PART_SCROLLBAR | state);
        lv_obj_set_style_border_color(obj, green, LV_PART_SCROLLBAR | state);
    }
    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(obj, 3, LV_STATE_CHECKED);
        /* Focus remains after touch release and outranks CHECKED in LVGL.
         * Keep the selected fill for every combined interaction state. */
        for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
            lv_style_selector_t selected = LV_STATE_CHECKED | states[i];
            lv_obj_set_style_bg_color(obj, lv_color_hex(DIM_GREEN), selected);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, selected);
        }
        lv_obj_set_style_bg_color(obj, lv_color_hex(DIM_GREEN), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(obj, 12, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_50, LV_STATE_PRESSED);
    }
    if (lv_obj_check_type(obj, &lv_roller_class)) {
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(DIM_GREEN), LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(obj, green, LV_PART_SELECTED);
        lv_obj_set_style_border_color(obj, green, LV_PART_SELECTED);
    }
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_set_style_text_color(obj, green, LV_PART_TEXTAREA_PLACEHOLDER);
        /* Cursor geometry/opacity must be explicit, not dependent on the
         * theme's focus-only cursor style. Match its focused selectors too. */
        for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
            lv_style_selector_t cursor = LV_PART_CURSOR | states[i];
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, cursor);
            lv_obj_set_style_border_color(obj, green, cursor);
            lv_obj_set_style_border_width(obj, 2, cursor);
            lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, cursor);
            lv_obj_set_style_border_opa(obj, LV_OPA_COVER, cursor);
            lv_obj_set_style_opa(obj, LV_OPA_COVER, cursor);
            lv_obj_set_style_pad_all(obj, 0, cursor);
            lv_obj_set_style_anim_duration(obj, 0, cursor);
        }
        lv_obj_set_style_bg_color(obj, green, LV_PART_SELECTED);
        lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_SELECTED);
    }
    if (lv_obj_check_type(obj, &lv_image_class)) {
        /* Recolor at draw time, preserving the logo's transparent pixels. */
        lv_obj_set_style_image_recolor(obj, green, 0);
        lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, 0);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        apply_neon_theme(lv_obj_get_child(obj, i));
}

#endif
