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

/* Main-table defaults from procfs, choosing the lowest metric per family. */
void app_network_default_routes(FILE *ipv4, FILE *ipv6, char *text, size_t size)
{
    char v4[IF_NAMESIZE]="", v6[IF_NAMESIZE]="", line[512], name[IF_NAMESIZE];
    unsigned long best4=~0UL, best6=~0UL;
    if (ipv4) while (fgets(line,sizeof(line),ipv4)) {
        unsigned long destination, gateway, flags, metric, mask;
        if (sscanf(line,"%15s %lx %lx %lx %*u %*u %lu %lx",name,&destination,&gateway,&flags,&metric,&mask)==6 &&
            !destination && !mask && (flags & 1) && !(flags & 0x200) && metric<best4) {
            best4=metric; snprintf(v4,sizeof(v4),"%s",name);
        }
    }
    if (ipv6) while (fgets(line,sizeof(line),ipv6)) {
        char destination[33], source[33], gateway[33];
        unsigned prefix, source_prefix; unsigned long metric, refs, use, flags;
        if (sscanf(line,"%32s %x %32s %x %32s %lx %lx %lx %lx %15s",
                   destination,&prefix,source,&source_prefix,gateway,&metric,&refs,&use,&flags,name)==10 &&
            !strcmp(destination,"00000000000000000000000000000000") && !prefix && !source_prefix &&
            (flags & 1) && !(flags & 0x200) && metric<best6) {
            best6=metric; snprintf(v6,sizeof(v6),"%s",name);
        }
    }
    snprintf(text,size,"Default route: IPv4 %s; IPv6 %s",*v4 ? v4 : "none",*v6 ? v6 : "none");
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
    text = sort_snapshot(text, lines);
    if (!text) return NULL;
    FILE *ipv4 = fopen("/proc/net/route", "r");
    FILE *ipv6 = fopen("/proc/net/ipv6_route", "r");
    char routes[160]; app_network_default_routes(ipv4, ipv6, routes, sizeof(routes));
    if (ipv4) fclose(ipv4);
    if (ipv6) fclose(ipv6);
    size_t length = strlen(text) + strlen(routes) + 2;
    char *result = malloc(length);
    if (result) snprintf(result, length, "%s\n%s", text, routes);
    free(text);
    return result;
}
