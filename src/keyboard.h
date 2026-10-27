#ifndef APP_KEYBOARD_H
#define APP_KEYBOARD_H
#include <lvgl.h>
/* Opens an explicit evdev path, or discovers a USB keyboard by-id symlink. */
lv_indev_t *app_keyboard_create(const char *path, lv_group_t *group);
typedef struct AppKeyboards AppKeyboards;
/* NULL/empty path discovers all typing keyboards; explicit path restricts input. */
AppKeyboards *app_keyboards_start(const char *path, lv_group_t *group, lv_display_t *display);
void app_keyboards_stop(AppKeyboards *manager);
#endif
