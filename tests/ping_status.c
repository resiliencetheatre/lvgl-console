#include "ping_status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char path[] = "/tmp/lvgl-ping-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd);
    const struct { const char *report; const char *expected; } cases[] = {
        {"9688 1104 0\n", "0%/9ms"},
        {"1000 200 25\n", "25%/1ms"},
        {"0 0 100\n", "100%/0ms"},
        {"", "--/--"},
        {"1 2\n", "--/--"},
        {"-1 2 0\n", "--/--"},
        {"1 2 101\n", "--/--"},
        {"1 2 0 extra\n", "--/--"},
        {"1 2 0\n1 2 0\n", "--/--"},
        {"9999999999999999999999999999 0 0\n", "--/--"},
    };
    char text[128];
    int failed = 0;
    const struct { const char *report; int alarm; } thresholds[] = {
        {"100000 0 0\n", 0}, {"100001 0 0\n", 1}, {"1000 0 1\n", 1}
    };
    for (unsigned i = 0; i < sizeof(thresholds) / sizeof(thresholds[0]); ++i) {
        FILE *file = fopen(path, "w");
        if (!file) return 1;
        fputs(thresholds[i].report, file);
        fclose(file);
        if (app_ping_status_read(path, time(NULL), text, sizeof(text)) != thresholds[i].alarm)
            failed = 1;
    }
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        FILE *file = fopen(path, "w");
        if (!file) { failed = 1; break; }
        fputs(cases[i].report, file);
        fclose(file);
        struct stat info;
        if (stat(path, &info)) { failed = 1; break; }
        app_ping_status_read(path, info.st_mtime, text, sizeof(text));
        if (strcmp(text, cases[i].expected)) {
            fprintf(stderr, "Report case %u: %s\n", i, text);
            failed = 1;
        }
        app_ping_status_read(path, info.st_mtime + 6, text, sizeof(text));
        if (strcmp(text, "--/--")) failed = 1;
        app_ping_status_read(path, info.st_mtime - 1, text, sizeof(text));
        if (strcmp(text, "--/--")) failed = 1;
    }
    unlink(path);
    app_ping_status_read(path, time(NULL), text, sizeof(text));
    if (strcmp(text, "--/--")) failed = 1;
    return failed;
}
