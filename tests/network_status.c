#include "network_status.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ifaddrs entries[5];
static int unavailable;
int __wrap_getifaddrs(struct ifaddrs **result)
{
    *result = entries;
    return unavailable ? -1 : 0;
}
void __wrap_freeifaddrs(struct ifaddrs *entries_to_free) { (void)entries_to_free; }

int main(void)
{
    struct sockaddr_in ipv4 = {.sin_family = AF_INET};
    struct sockaddr_in6 ipv6 = {.sin6_family = AF_INET6};
    inet_pton(AF_INET, "192.0.2.10", &ipv4.sin_addr);
    inet_pton(AF_INET6, "2001:db8::10", &ipv6.sin6_addr);
    const char *names[] = {"eth0", "eth0", "wlan0", "eth0", "dummy0"};
    for (unsigned i = 0; i < 5; ++i) {
        entries[i].ifa_name = (char *)names[i];
        entries[i].ifa_flags = IFF_UP | IFF_RUNNING;
        entries[i].ifa_next = i < 4 ? &entries[i + 1] : NULL;
    }
    entries[1].ifa_addr = (struct sockaddr *)&ipv4;
    entries[2].ifa_flags = IFF_UP; /* Enabled, but no operational link. */
    entries[3].ifa_addr = (struct sockaddr *)&ipv6;
    entries[4].ifa_flags = 0;
    char *text = app_network_status_read();
    const char *expected = "dummy0:No IP [DOWN]\neth0:192.0.2.10 [UP]\neth0:[2001:db8::10] [UP]\nwlan0:No IP [DOWN]";
    if (!text || strcmp(text, expected)) {
        fprintf(stderr, "Unexpected network snapshot: %s\n", text ? text : "NULL");
        free(text);
        return EXIT_FAILURE;
    }
    free(text);
    /* Reorder enumeration without changing any interfaces or addresses. */
    entries[0].ifa_next = &entries[4];
    entries[4].ifa_next = &entries[3];
    entries[3].ifa_next = &entries[2];
    entries[2].ifa_next = &entries[1];
    entries[1].ifa_next = NULL;
    text = app_network_status_read();
    if (!text || strcmp(text, expected)) { free(text); return EXIT_FAILURE; }
    free(text);
    unavailable = 1;
    text = app_network_status_read();
    if (text) { free(text); return EXIT_FAILURE; }
    puts("Network addresses, interface states and read failure passed");
    return EXIT_SUCCESS;
}
