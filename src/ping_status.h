#ifndef APP_PING_STATUS_H
#define APP_PING_STATUS_H
#include <stddef.h>
#include <time.h>
/* Alarm thresholds: strictly greater than these limits turns a box dim red. */
#define APP_PING_LOSS_LIMIT_PERCENT 0
#define APP_PING_LATENCY_LIMIT_MS 100
/* Returns 1 for alarm, 0 for healthy, -1 for unavailable. */
int app_ping_status_read(const char *path, time_t now, char *text, size_t size);
#endif
