#ifndef APP_NETWORK_STATUS_H
#define APP_NETWORK_STATUS_H
/* Caller frees the returned text. NULL means the snapshot could not be read. */
char *app_network_status_read(void);
#endif
