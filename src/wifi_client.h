#ifndef APP_WIFI_CLIENT_H
#define APP_WIFI_CLIENT_H
#include <stdbool.h>
#define WIFI_MAX_NETWORKS 64
#define WIFI_PATH_SIZE 256
typedef struct {
    char path[WIFI_PATH_SIZE], name[128], security[24], known[WIFI_PATH_SIZE];
    bool connected;
    int signal;
} AppWifiNetwork;
typedef struct {
    bool available, busy, scanning;
    char station_state[32];
    unsigned count;
    char message[256];
    AppWifiNetwork networks[WIFI_MAX_NETWORKS];
} AppWifiState;
typedef enum { APP_WIFI_SCAN, APP_WIFI_CONNECT, APP_WIFI_DISCONNECT, APP_WIFI_FORGET } AppWifiAction;
typedef struct AppWifi AppWifi;
AppWifi *app_wifi_start(void);
void app_wifi_stop(AppWifi *wifi);
void app_wifi_snapshot(AppWifi *wifi, AppWifiState *state);
bool app_wifi_request(AppWifi *wifi, AppWifiAction action, const char *path, const char *password);
#endif
