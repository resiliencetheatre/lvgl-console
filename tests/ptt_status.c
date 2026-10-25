#include "ptt_status.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int online = 1;
int __wrap_socket(int domain, int type, int protocol)
{
    (void)domain; (void)type; (void)protocol;
    return open("/dev/null", O_RDONLY);
}
int __wrap_connect(int fd, const struct sockaddr *address, socklen_t length)
{
    (void)fd; (void)address; (void)length;
    return online ? 0 : -1;
}
int main(void)
{
    char path[] = "/tmp/lvgl-ptt-test-XXXXXX", text[320];
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd);
    const struct { const char *data; const char *expected; } cases[] = {
        {"tx=0\nrx=0\n", "PTT idle"},
        {"tx=1\nrx=0\ntxid=pocketterm\n", "TX pocketterm"},
        {"tx=0\nrx=1\ntalker=desktop\n", "RX desktop"},
        {"tx=1\nrx=1\ntxid=pocketterm\ntalker=desktop\n", "TX pocketterm | RX desktop"},
        {"tx=1\n", "PTT unavailable"},
        {"tx=garbage\nrx=0\n", "PTT unavailable"},
    };
    int failed = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        FILE *file = fopen(path, "w");
        if (!file) return 1;
        fputs(cases[i].data, file);
        fclose(file);
        app_ptt_status_read(path, "/tmp/udpptt.sock", text, sizeof(text));
        if (strcmp(text, cases[i].expected)) failed = 1;
    }
    unlink(path);
    app_ptt_status_read(path, "/tmp/udpptt.sock", text, sizeof(text));
    if (strcmp(text, "PTT unavailable")) failed = 1;
    online = 0;
    app_ptt_status_read(path, "/tmp/udpptt.sock", text, sizeof(text));
    if (strcmp(text, "PTT offline")) failed = 1;
    return failed;
}
