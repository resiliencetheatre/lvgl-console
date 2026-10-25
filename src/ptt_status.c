#include "ptt_status.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void app_ptt_status_read(const char *state_path, const char *socket_path,
                         char *text, size_t size)
{
    snprintf(text, size, "PTT offline");
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(socket_path) >= sizeof(address.sun_path)) return;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    int connected = connect(fd, (struct sockaddr *)&address, sizeof(address));
    close(fd); /* No commands or probe datagrams are sent. */
    if (connected < 0) return;
    snprintf(text, size, "PTT unavailable");
    FILE *file = fopen(state_path, "r");
    if (!file) return;
    char line[256], txid[128] = "", talker[128] = "";
    int tx = -1, rx = -1;
    unsigned lines = 0;
    while (lines++ < 32 && fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, "tx=0")) tx = 0;
        else if (!strcmp(line, "tx=1")) tx = 1;
        else if (!strcmp(line, "rx=0")) rx = 0;
        else if (!strcmp(line, "rx=1")) rx = 1;
        else if (!strncmp(line, "txid=", 5)) snprintf(txid, sizeof(txid), "%.127s", line + 5);
        else if (!strncmp(line, "talker=", 7)) snprintf(talker, sizeof(talker), "%.127s", line + 7);
    }
    int error = ferror(file);
    fclose(file);
    if (error || tx < 0 || rx < 0) return;
    if (tx && rx) snprintf(text, size, "TX %s | RX %s", txid, talker);
    else if (tx) snprintf(text, size, "TX %s", txid);
    else if (rx) snprintf(text, size, "RX %s", talker);
    else snprintf(text, size, "PTT idle");
}
