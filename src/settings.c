#include "settings.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *keys[] = {"UDPPTT_TXID", "UDPPTT_DPI_ID", "UDPPTT_PREAMBLE_ID", "UDPPTT_SCREENSAVER", "UDPPTT_SERVER", "UDPPTT_ALSA_DEVICE", "UDPPTT_PREAMBLE_ENABLED"};
static int key_index(const char *line, const char **value)
{
    while (*line == ' ' || *line == '\t') ++line;
    for (int i = 0; i < 7; ++i) {
        size_t n = strlen(keys[i]);
        if (strncmp(line, keys[i], n)) continue;
        const char *separator = line + n;
        while (*separator == ' ' || *separator == '\t') ++separator;
        if (*separator == '=') {
            ++separator;
            while (*separator == ' ' || *separator == '\t') ++separator;
            *value = separator;
            return i;
        }
    }
    return -1;
}
int app_settings_load(const char *path, app_settings *s)
{
    *s = (app_settings){.txid="pocketterm", .dpi_id="pocketterm", .preamble="POCKT", .preamble_enabled=true};
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, f) >= 0) {
        const char *value;
        int i = key_index(line, &value);
        if (i < 0) continue;
        size_t n = strcspn(value, "\r\n");
        while (n && isspace((unsigned char)value[n-1])) --n;
        if (n >= 2 && (value[0] == '\'' || value[0] == '"') && value[n-1] == value[0]) { ++value; n -= 2; }
        char *dest = i == 0 ? s->txid : i == 1 ? s->dpi_id : i == 4 ? s->server : i == 5 ? s->alsa_device : s->preamble;
        size_t limit = i == 2 ? sizeof(s->preamble) : i == 4 ? sizeof(s->server) : i == 5 ? sizeof(s->alsa_device) : sizeof(s->txid);
        if (i == 6) s->preamble_enabled = !(n == 1 && *value == '0');
        else if (i == 3) s->screensaver = n == 1 && *value == '1';
        else {
            if (n >= limit) { free(line); fclose(f); errno = EINVAL; return -1; }
            memcpy(dest, value, n); dest[n] = 0;
        }
    }
    int failed = ferror(f);
    free(line); fclose(f);
    return failed ? -1 : 0;
}
static bool identifier(const char *s)
{
    if (!*s) return false;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '.' || *s == '_' || *s == '-')) return false;
    return true;
}
bool app_settings_valid(const app_settings *s)
{
    if (!identifier(s->txid) || strlen(s->txid) > 31 ||
        !identifier(s->dpi_id) || strlen(s->preamble) != 5) return false;
    for (unsigned i = 0; i < 5; ++i) if (s->preamble[i] < 'A' || s->preamble[i] > 'Z') return false;
    /* Preserve support for hostnames as well as IPv4/IPv6 server addresses.
     * Only shell/environment-safe address characters are stored unquoted. */
    for (const char *p = s->server; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-' && *p != ':') return false;
    return true;
}
int app_settings_save(const char *path, const app_settings *s)
{
    if (!app_settings_valid(s)) { errno = EINVAL; return -1; }
    FILE *in = fopen(path, "r");
    if (!in) return -1; /* Never silently recreate a missing configuration. */
    struct stat info;
    if (fstat(fileno(in), &info)) { fclose(in); return -1; }
    char temp[4096];
    if (snprintf(temp, sizeof(temp), "%s.XXXXXX", path) >= (int)sizeof(temp)) { fclose(in); errno=ENAMETOOLONG; return -1; }
    int fd = mkstemp(temp);
    if (fd < 0) { fclose(in); return -1; }
    FILE *out = fdopen(fd, "w");
    if (!out) { close(fd); unlink(temp); fclose(in); return -1; }
    int failed = fchmod(fd, info.st_mode & 0777);
    if (geteuid() == 0 && fchown(fd, info.st_uid, info.st_gid)) failed = 1;
    char *line = NULL; size_t capacity = 0;
    while (getline(&line, &capacity, in) >= 0) {
        const char *value;
        int key = key_index(line, &value);
        if ((key < 0 || key == 5) && fputs(line, out) == EOF) failed = 1;
    }
    if (ferror(in)) failed = 1;
    free(line); fclose(in);
    if (fprintf(out, "\nUDPPTT_TXID=%s\nUDPPTT_DPI_ID=%s\nUDPPTT_PREAMBLE_ID=%s\nUDPPTT_SCREENSAVER=%d\nUDPPTT_SERVER=%s\nUDPPTT_PREAMBLE_ENABLED=%d\n", s->txid, s->dpi_id, s->preamble, s->screensaver, s->server, s->preamble_enabled) < 0) failed = 1;
    if (fflush(out) || fsync(fd)) failed = 1;
    if (fclose(out)) failed = 1;
    if (!failed && rename(temp, path) == 0) return 0;
    unlink(temp);
    return -1;
}
