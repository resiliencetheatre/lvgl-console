#include "ui.h"
#include <stdio.h>
#include <stdlib.h>

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
    lv_display_t *display = lv_display_create(800, 480);
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
    app_ui_create(screen);
    lv_obj_t *tabs = lv_obj_get_child(screen, 0);
    for (unsigned i = 0; i < 3; ++i) {
        unsigned before = flushes;
        lv_tabview_set_active(tabs, i, LV_ANIM_OFF);
        lv_obj_invalidate(screen);
        lv_refr_now(display);
        if (flushes == before || lv_tabview_get_tab_active(tabs) != i) {
            fprintf(stderr, "Tab %u failed to render\n", i);
            return EXIT_FAILURE;
        }
    }
    lv_deinit();
    puts("Logo decoding and all three tabs rendered successfully");
    return EXIT_SUCCESS;
}
