#ifndef APP_CHAT_BACKEND_H
#define APP_CHAT_BACKEND_H
#include <stdint.h>
struct app_chat;
/* Callback strings are valid only during the call. Runs on the polling thread. */
typedef void (*app_chat_event_fn)(void *, const char *type, unsigned request, const char *text);
struct app_chat *app_chat_create(const char *path, app_chat_event_fn event, void *context);
void app_chat_destroy(struct app_chat *chat);
void app_chat_poll(struct app_chat *chat);
/* Returns request ID, or zero with an explanatory error. One command in flight. */
unsigned app_chat_submit(struct app_chat *chat, const char *line, char *error, unsigned capacity);
#endif
