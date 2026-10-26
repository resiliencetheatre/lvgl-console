#ifndef APP_VOLUME_H
#define APP_VOLUME_H
#include <stdbool.h>
/* Returns zero on success; negative ALSA error otherwise. */
int app_volume_get(bool capture, int *percent);
int app_volume_set(bool capture, int percent);
#endif
