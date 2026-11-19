#ifndef APP_NETWORK_STATUS_H
#define APP_NETWORK_STATUS_H
/* Caller frees the returned text. NULL means the snapshot could not be read. */
#include <stdio.h>
char *app_network_status_read(void);
void app_network_default_routes(FILE *ipv4, FILE *ipv6, char *text, size_t size);
#endif
