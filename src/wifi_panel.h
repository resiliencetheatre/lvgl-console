#ifndef APP_WIFI_PANEL_H
#define APP_WIFI_PANEL_H
#include "lvgl.h"
lv_obj_t *app_wifi_panel_create(lv_obj_t *parent);
void app_wifi_panel_add_to_group(lv_obj_t *panel, lv_group_t *group);
#endif
