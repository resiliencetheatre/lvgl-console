#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "network_status.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_lines(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* getifaddrs enumeration order is not part of the displayed network state. */
static char *sort_snapshot(char *text, unsigned count)
{
    if (count < 2) return text;
    char **lines = malloc(count * sizeof(*lines));
    char *sorted = malloc(strlen(text) + 1);
    if (!lines || !sorted) {
        free(lines); free(sorted); free(text);
        return NULL;
    }
    unsigned n = 0;
    char *save;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
        lines[n++] = line;
    qsort(lines, n, sizeof(*lines), compare_lines);
    char *out = sorted;
    for (unsigned i = 0; i < n; ++i) {
        size_t len = strlen(lines[i]);
        memcpy(out, lines[i], len);
        out += len;
        if (i + 1 < n) *out++ = '\n';
    }
    *out = 0;
    free(lines); free(text);
    return sorted;
}

char *app_network_status_read(void)
{
    struct ifaddrs *interfaces;
    if (getifaddrs(&interfaces) < 0) return NULL;
    char *text = NULL;
    size_t size = 0;
    FILE *stream = open_memstream(&text, &size);
    if (!stream) {
        freeifaddrs(interfaces);
        return NULL;
    }
    unsigned lines = 0;
    for (struct ifaddrs *it = interfaces; it; it = it->ifa_next) {
        if (!it->ifa_name) continue;
        struct ifaddrs *previous;
        for (previous = interfaces; previous != it; previous = previous->ifa_next)
            if (previous->ifa_name && !strcmp(previous->ifa_name, it->ifa_name)) break;
        if (previous != it) continue;
        const char *status = (it->ifa_flags & IFF_UP) && (it->ifa_flags & IFF_RUNNING)
            ? "UP" : "DOWN";
        unsigned addresses = 0;
        for (struct ifaddrs *addr = interfaces; addr; addr = addr->ifa_next) {
            if (!addr->ifa_name || strcmp(addr->ifa_name, it->ifa_name) || !addr->ifa_addr)
                continue;
            int family = addr->ifa_addr->sa_family;
            const void *bytes;
            if (family == AF_INET)
                bytes = &((struct sockaddr_in *)addr->ifa_addr)->sin_addr;
            else if (family == AF_INET6)
                bytes = &((struct sockaddr_in6 *)addr->ifa_addr)->sin6_addr;
            else continue;
            char ip[INET6_ADDRSTRLEN];
            if (!inet_ntop(family, bytes, ip, sizeof(ip))) continue;
            fprintf(stream, "%s%s:%s%s%s [%s]", lines++ ? "\n" : "", it->ifa_name,
                    family == AF_INET6 ? "[" : "", ip,
                    family == AF_INET6 ? "]" : "", status);
            ++addresses;
        }
        if (!addresses)
            fprintf(stream, "%s%s:No IP [%s]", lines++ ? "\n" : "", it->ifa_name, status);
    }
    if (!lines) fputs("No network interfaces", stream);
    int failed = ferror(stream);
    if (fclose(stream)) failed = 1;
    freeifaddrs(interfaces);
    if (failed) { free(text); return NULL; }
    return sort_snapshot(text, lines);
}
