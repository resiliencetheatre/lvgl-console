#ifndef APP_UPDATE_PANEL_H
#define APP_UPDATE_PANEL_H
#include <lvgl.h>
/* The panel owns a background API client, independent of the LVGL thread. */
lv_obj_t *app_update_panel_create(lv_obj_t *parent);
void app_update_panel_add_to_group(lv_obj_t *panel, lv_group_t *group);
#endif
