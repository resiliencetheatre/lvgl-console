#include "ui.h"
#include "keyboard.h"
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned flushes;
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    ++flushes;
    lv_display_flush_ready(display);
}

int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(640, 480);
    static uint8_t buffer[800 * 48 * 2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_image_header_t header;
    if (lv_image_decoder_get_info("A:/link.png", &header) != LV_RESULT_OK ||
        header.w != 420 || header.h != 101) {
        fprintf(stderr, "Logo decoder failed\n");
        return EXIT_FAILURE;
    }
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_t *input = app_ui_create(screen);
    lv_group_t *group = lv_group_create();
    lv_group_add_obj(group, input);
    lv_obj_t *tabs = lv_obj_get_child(screen, 0);
    lv_timer_handler();
    lv_tick_inc(500);
    lv_timer_handler();
    lv_obj_update_layout(screen);
    lv_obj_t *content = lv_tabview_get_content(tabs);
    lv_obj_t *ptt = lv_obj_get_child(content, 0);
    lv_obj_t *logo = lv_obj_get_child(ptt, 0);
    if (lv_tabview_get_tab_active(tabs) != 0 || lv_obj_get_scroll_x(content) != 0 ||
        lv_obj_get_child_count(ptt) != 1 || !lv_obj_check_type(logo, &lv_image_class)) {
        fprintf(stderr, "Startup must show PTT with only its logo\n");
        return EXIT_FAILURE;
    }
    lv_area_t ptt_area, logo_area;
    lv_obj_get_coords(ptt, &ptt_area);
    lv_obj_get_coords(logo, &logo_area);
    if (abs((ptt_area.y1 + ptt_area.y2) - (logo_area.y1 + logo_area.y2)) > 2 ||
        abs((ptt_area.x1 + ptt_area.x2) - (logo_area.x1 + logo_area.x2)) > 2)
        return EXIT_FAILURE;
    lv_obj_t *bar = lv_obj_get_child(screen, 1);
    if (lv_obj_get_child_count(bar) != 5 ||
        lv_obj_get_y(bar) < lv_obj_get_height(tabs)) return EXIT_FAILURE;
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *box = lv_obj_get_child(bar, i);
        if (lv_obj_get_width(box) != 104 || lv_obj_get_height(box) != 28 ||
            lv_obj_get_child_count(box) != 1)
            return EXIT_FAILURE;
        lv_point_t text_size;
        lv_text_get_size(&text_size, "100%/9999ms", &lv_font_montserrat_14,
                         0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (text_size.x > lv_obj_get_content_width(box)) return EXIT_FAILURE;
    }
    if (lv_obj_get_width(lv_obj_get_child(bar, 4)) <= 104) return EXIT_FAILURE;
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabs);
    for (unsigned i = 0; i < 3; ++i) {
        unsigned before = flushes;
        lv_tabview_set_active(tabs, i, LV_ANIM_OFF);
        lv_obj_invalidate(screen);
        lv_refr_now(display);
        if (flushes == before || lv_tabview_get_tab_active(tabs) != i) {
            fprintf(stderr, "Tab %u failed to render\n", i);
            return EXIT_FAILURE;
        }
        lv_obj_t *button = lv_obj_get_child(tab_bar, i);
        /* Touch leaves focus behind after release; keyboard focus and hover
         * must also preserve the selected tab's persistent fill. */
        const lv_state_t focus_states[] = {
            LV_STATE_DEFAULT, LV_STATE_FOCUSED,
            LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY, LV_STATE_HOVERED
        };
        for (unsigned j = 0; j < sizeof(focus_states) / sizeof(focus_states[0]); ++j) {
            lv_obj_add_state(button, focus_states[j] | LV_STATE_PRESSED);
            lv_obj_remove_state(button, LV_STATE_PRESSED);
            if (!lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN),
                             lv_color_hex(0x103D08)) ||
                lv_obj_get_style_bg_opa(button, LV_PART_MAIN) != LV_OPA_COVER) {
                fprintf(stderr, "Selected tab %u lost its fill after release\n", i);
                return EXIT_FAILURE;
            }
            lv_obj_remove_state(button, focus_states[j]);
        }
        for (unsigned j = 0; j < 3; ++j) {
            if (j != i && !lv_color_eq(lv_obj_get_style_bg_color(
                    lv_obj_get_child(tab_bar, j), LV_PART_MAIN), lv_color_black())) {
                fprintf(stderr, "Inactive tab %u retained its fill\n", j);
                return EXIT_FAILURE;
            }
        }
    }
    lv_group_focus_obj(input);
    /* Feed real evdev records through the keyboard reader without hardware. */
    const struct input_event events[] = {
        {.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 1},
        {.type = EV_KEY, .code = KEY_H, .value = 1},
        {.type = EV_KEY, .code = KEY_H, .value = 0},
        {.type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 0},
        {.type = EV_KEY, .code = KEY_I, .value = 1},
        {.type = EV_KEY, .code = KEY_I, .value = 0},
        {.type = EV_KEY, .code = KEY_1, .value = 1},
        {.type = EV_KEY, .code = KEY_1, .value = 0},
        {.type = EV_KEY, .code = KEY_BACKSPACE, .value = 1},
        {.type = EV_KEY, .code = KEY_BACKSPACE, .value = 0},
    };
    FILE *stream = tmpfile();
    if (!stream) return EXIT_FAILURE;
    if (fwrite(events, sizeof(events), 1, stream) != 1 || fflush(stream))
        return EXIT_FAILURE;
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fileno(stream));
    lv_indev_t *keyboard = app_keyboard_create(path, group, tabs);
    if (!keyboard) return EXIT_FAILURE;
    lv_indev_set_display(keyboard, display);
    lv_indev_read(keyboard);
    if (strcmp(lv_textarea_get_text(input), "Hi") != 0) {
        fprintf(stderr, "Physical keyboard text entry failed\n");
        return EXIT_FAILURE;
    }
    lv_tabview_set_active(tabs, 0, LV_ANIM_OFF);
    const struct {
        unsigned short code;
        int value;
        uint32_t tab;
    } navigation[] = {
        {KEY_RIGHT, 1, 1}, {KEY_RIGHT, 2, 1}, {KEY_RIGHT, 0, 1},
        {KEY_RIGHT, 1, 2}, {KEY_RIGHT, 0, 2},
        {KEY_RIGHT, 1, 0}, {KEY_RIGHT, 0, 0},
        {KEY_LEFT, 1, 2}, {KEY_LEFT, 2, 2}, {KEY_LEFT, 0, 2},
        {KEY_LEFT, 1, 1}, {KEY_LEFT, 0, 1},
        {KEY_LEFT, 1, 0}, {KEY_LEFT, 0, 0},
    };
    uint32_t cursor = lv_textarea_get_cursor_pos(input);
    for (unsigned i = 0; i < sizeof(navigation) / sizeof(navigation[0]); ++i) {
        const struct input_event packet[] = {
            {.type = EV_MSC, .code = MSC_SCAN, .value = 0x7004f},
            {.type = EV_KEY, .code = navigation[i].code, .value = navigation[i].value},
            {.type = EV_SYN, .code = SYN_REPORT},
        };
        if (fwrite(packet, sizeof(packet), 1, stream) != 1 || fflush(stream))
            return EXIT_FAILURE;
        lv_indev_read(keyboard);
        lv_refr_now(display);
        if (lv_tabview_get_tab_active(tabs) != navigation[i].tab ||
            lv_textarea_get_cursor_pos(input) != cursor ||
            strcmp(lv_textarea_get_text(input), "Hi") != 0) {
            fprintf(stderr, "Arrow navigation failed at event %u\n", i);
            return EXIT_FAILURE;
        }
        lv_obj_t *selected = lv_obj_get_child(tab_bar, navigation[i].tab);
        if (!lv_color_eq(lv_obj_get_style_bg_color(selected, LV_PART_MAIN),
                         lv_color_hex(0x103D08))) {
            fprintf(stderr, "Arrow navigation lost the selected tab fill\n");
            return EXIT_FAILURE;
        }
    }
    fclose(stream);
    lv_deinit();
    puts("Logo, tabs, keyboard text entry and arrow navigation passed");
    return EXIT_SUCCESS;
}
