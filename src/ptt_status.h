#ifndef APP_PTT_STATUS_H
#define APP_PTT_STATUS_H
#include <stddef.h>
void app_ptt_status_read(const char *state_path, const char *socket_path,
                         char *text, size_t size);
#endif
