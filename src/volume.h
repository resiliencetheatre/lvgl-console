#ifndef APP_VOLUME_H
#define APP_VOLUME_H
#include <stdbool.h>
/* Select the mixer card from the active hw:/plughw: PCM device. */
int app_volume_select_device(const char *device);
/* Returns zero on success; negative ALSA error otherwise. */
int app_volume_get(bool capture, int *percent);
int app_volume_set(bool capture, int percent);
#endif
