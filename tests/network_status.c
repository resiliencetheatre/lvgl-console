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
    if (!text || strncmp(text, expected, strlen(expected)) || !strstr(text,"\nDefault route: IPv4 ")) {
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
    if (!text || strncmp(text, expected, strlen(expected)) || !strstr(text,"\nDefault route: IPv4 ")) { free(text); return EXIT_FAILURE; }
    free(text);
    FILE *v4=tmpfile(), *v6=tmpfile();
    if (!v4 || !v6) return EXIT_FAILURE;
    fputs("Iface Destination Gateway Flags RefCnt Use Metric Mask\n"
          "wlan0 00000000 010200C0 0003 0 0 600 00000000\n"
          "eth0 00000000 010200C0 0003 0 0 100 00000000\n"
          "bad0 00000000 00000000 0201 0 0 0 00000000\n",v4);
    fputs("00000000000000000000000000000000 00 00000000000000000000000000000000 00 fe800000000000000000000000000001 00000400 00000000 00000000 00000003 wlan0\n",v6);
    rewind(v4); rewind(v6);
    char routes[160]; app_network_default_routes(v4,v6,routes,sizeof(routes));
    fclose(v4); fclose(v6);
    if (strcmp(routes,"Default route: IPv4 eth0; IPv6 wlan0")) return EXIT_FAILURE;
    app_network_default_routes(NULL,NULL,routes,sizeof(routes));
    if (strcmp(routes,"Default route: IPv4 none; IPv6 none")) return EXIT_FAILURE;
    unavailable = 1;
    text = app_network_status_read();
    if (text) { free(text); return EXIT_FAILURE; }
    puts("Network addresses, interface states and read failure passed");
    return EXIT_SUCCESS;
}
