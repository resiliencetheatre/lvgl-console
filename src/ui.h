#ifndef LVGL_CONSOLE_UI_H
#define LVGL_CONSOLE_UI_H
#include <lvgl.h>
lv_obj_t *app_ui_create(lv_obj_t *screen);
void app_ui_resume_display(void);
void app_ui_add_settings_to_group(lv_group_t *group);
#endif
