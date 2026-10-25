#ifndef APP_KEYBOARD_H
#define APP_KEYBOARD_H
#include <lvgl.h>
/* Opens an explicit evdev path, or discovers a USB keyboard by-id symlink. */
/* Optional tabview must outlive the keyboard. Arrows navigate once per press. */
lv_indev_t *app_keyboard_create(const char *path, lv_group_t *group, lv_obj_t *tabs);
#endif
