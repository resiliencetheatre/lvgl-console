#include "ui.h"
#include "keyboard.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 6 || LVGL_VERSION_PATCH != 0
#error "lvgl-console requires LVGL 9.6.0"
#endif

static volatile sig_atomic_t running = 1;
static void stop(int signo)
{
    (void)signo;
    running = 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (argc == 2 && strcmp(argv[1], "--help") == 0) {
            puts("Usage: lvgl-console\n"
                 "  LV_LINUX_FBDEV_DEVICE: framebuffer (default /dev/fb0)\n"
                 "  LV_LINUX_EVDEV_DEVICE: pointer/touch device (default /dev/input/touchscreen)\n"
                 "  LV_LINUX_KEYBOARD_DEVICE: keyboard event device (default USB auto-detection)");
            return EXIT_SUCCESS;
        }
        fprintf(stderr, "Usage: %s [--help]\n", argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    lv_init();
    const char *fb = getenv("LV_LINUX_FBDEV_DEVICE");
    const char *input = getenv("LV_LINUX_EVDEV_DEVICE");
    if (!fb || !*fb) fb = "/dev/fb0";
    if (!input || !*input) input = "/dev/input/touchscreen";
    lv_display_t *display = lv_linux_fbdev_create();
    if (!display || lv_linux_fbdev_set_file(display, fb) != LV_RESULT_OK) {
        fprintf(stderr, "Unable to initialize framebuffer %s\n", fb);
        lv_deinit();
        return EXIT_FAILURE;
    }
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, input);
    if (touch) lv_indev_set_display(touch, display);
    else fprintf(stderr, "Unable to open input %s; continuing without touch input\n", input);

    lv_image_header_t header;
    if (lv_image_decoder_get_info("A:/link.png", &header) != LV_RESULT_OK) {
        fprintf(stderr, "Unable to load logo from %s/link.png\n", APP_ASSET_DIR);
        lv_deinit();
        return EXIT_FAILURE;
    }
    lv_group_t *keyboard_group = lv_group_create();
    lv_obj_t *text_input = app_ui_create(lv_display_get_screen_active(display));
    lv_group_add_obj(keyboard_group, text_input);
    app_ui_add_settings_to_group(keyboard_group);
    lv_indev_t *keyboard = app_keyboard_create(getenv("LV_LINUX_KEYBOARD_DEVICE"), keyboard_group);
    if (keyboard) lv_indev_set_display(keyboard, display);
    while (running) {
        /* fbdev supplies LVGL's monotonic tick callback. */
        uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY || delay > 20) delay = 20;
        if (delay < 1) delay = 1;
        usleep(delay * 1000);
    }
    app_ui_resume_display();
    lv_deinit();
    return EXIT_SUCCESS;
}
