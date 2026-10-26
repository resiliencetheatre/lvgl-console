#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    char txid[64], dpi_id[64], preamble[6], server[256];
    bool screensaver;
} app_settings;
int app_settings_load(const char *path, app_settings *settings);
int app_settings_save(const char *path, const app_settings *settings);
bool app_settings_valid(const app_settings *settings);
#endif
