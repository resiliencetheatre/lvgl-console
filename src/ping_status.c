#include "ping_status.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int app_ping_status_read(const char *path, time_t now, char *text, size_t size)
{
    snprintf(text, size, "--/--");
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    struct stat info;
    char line[128];
    int valid = fstat(fileno(file), &info) == 0 && S_ISREG(info.st_mode) &&
                now != (time_t)-1 && difftime(now, info.st_mtime) >= 0 &&
                difftime(now, info.st_mtime) <= 5 &&
                fgets(line, sizeof(line), file) != NULL;
    /* Reject extra data, including an overlong or multi-line report. */
    if (valid && (fgetc(file) != EOF || ferror(file))) valid = 0;
    fclose(file);
    if (!valid) return -1;
    unsigned long values[3];
    char *cursor = line;
    for (unsigned i = 0; i < 3; ++i) {
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (!isdigit((unsigned char)*cursor)) return -1;
        errno = 0;
        char *end;
        values[i] = strtoul(cursor, &end, 10);
        if (errno || (*end && !isspace((unsigned char)*end))) return -1;
        cursor = end;
    }
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor || values[2] > 100) return -1;
    unsigned long ms = values[0] / 1000;
    if (ms > 9999)
        snprintf(text, size, "%lu%%/>9999ms", values[2]);
    else
        snprintf(text, size, "%lu%%/%lums", values[2], ms);
    return values[2] > APP_PING_LOSS_LIMIT_PERCENT ||
           values[0] > APP_PING_LATENCY_LIMIT_MS * 1000UL;
}
