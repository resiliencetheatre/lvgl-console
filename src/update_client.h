#ifndef APP_UPDATE_CLIENT_H
#define APP_UPDATE_CLIENT_H
#include <stdbool.h>
#include <stdint.h>

typedef struct AppUpdates AppUpdates;
typedef enum { APP_UPDATE_CHECK, APP_UPDATE_DOWNLOAD, APP_UPDATE_INSTALL, APP_UPDATE_FINISH } AppUpdateAction;
typedef struct {
    bool connected, busy, can_check, can_update, download_ready, trial, can_finish;
    uint64_t received, total, offer_size;
    int percent; /* -1: stage only; otherwise download or current SWUpdate step. */
    char phase[32], version[65], offer_version[65], offer_id[65];
    char notes[8193], message[512], detail[256];
} AppUpdateState;

AppUpdates *app_updates_start(const char *socket_path);
void app_updates_stop(AppUpdates *updates);
void app_updates_snapshot(AppUpdates *updates, AppUpdateState *state);
/* Called only after explicit UI acceptance; offer_id pins the shown release. */
bool app_updates_request(AppUpdates *updates, AppUpdateAction action, const char *offer_id);
/* Shared parsing/model logic, also exercised by headless tests. */
bool app_updates_parse_status(const char *json, AppUpdateState *state);
#endif
