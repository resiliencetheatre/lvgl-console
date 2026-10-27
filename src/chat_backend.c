/* Independent implementation of utxtrelay/CONSOLE-IPC.md; no relay/crypto dependency. */
#define _POSIX_C_SOURCE 200809L
#include "chat_backend.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define RECORD 2048
struct app_chat {
    int fd;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    uint64_t epoch, last;
    unsigned next, pending;
    double retry, deadline;
    app_chat_event_fn event;
    void *context;
};
static double monotime(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void wipe(void *ptr, size_t n) { volatile unsigned char *p = ptr; while (n--) *p++ = 0; }
static void lost(struct app_chat *c) {
    if (c->fd >= 0) close(c->fd);
    c->fd = -1; c->retry = monotime() + 1;
    if (c->pending) c->event(c->context, "ERROR", c->pending, "Connector disconnected; command outcome unknown. No automatic retry.");
    c->pending = 0;
    c->event(c->context, "STATE", 0, "Chat connector offline");
}
struct app_chat *app_chat_create(const char *path, app_chat_event_fn event, void *context) {
    if (!path || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path) || !event) return NULL;
    struct app_chat *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = -1; c->event = event; c->context = context;
    strcpy(c->path, path); return c;
}
void app_chat_destroy(struct app_chat *c) {
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    wipe(c, sizeof(*c)); free(c);
}
/* Parse bounded decimal fields without scanf overflow or sign acceptance. */
static int parse_header(const char *header, unsigned *request,
                        unsigned long long *epoch_out, unsigned long long *event_out,
                        char type[16]) {
    if (strncmp(header, "UX1 ", 4)) return -1;
    const char *p = header + 4;
    unsigned long long fields[3];
    for (unsigned i = 0; i < 3; ++i) {
        if (*p < '0' || *p > '9') return -1;
        char *end;
        errno = 0;
        fields[i] = strtoull(p, &end, 10);
        if (errno || *end != ' ') return -1;
        p = end + 1;
    }
    size_t len = strlen(p);
    if (fields[0] > UINT32_MAX || !len || len > 15) return -1;
    for (size_t i = 0; i < len; ++i) if (p[i] < 'A' || p[i] > 'Z') return -1;
    *request = (unsigned)fields[0]; *epoch_out = fields[1]; *event_out = fields[2];
    memcpy(type, p, len + 1);
    return 0;
}
void app_chat_poll(struct app_chat *c) {
    if (!c) return;
    if (c->pending && monotime() > c->deadline) { lost(c); return; }
    if (c->fd < 0) {
        if (monotime() < c->retry) return;
        c->retry = monotime() + 1;
        int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return;
        struct sockaddr_un addr = {.sun_family = AF_UNIX};
        strcpy(addr.sun_path, c->path);
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) { close(fd); return; }
        char resume[160];
        int n = snprintf(resume, sizeof(resume), "UX1 0 %llu %llu RESUME\n",
                         (unsigned long long)c->epoch, (unsigned long long)c->last);
        if (send(fd, resume, (size_t)n, MSG_DONTWAIT | MSG_NOSIGNAL) != n) { close(fd); return; }
        c->fd = fd;
        c->event(c->context, "STATE", 0, "Chat connector attached; checking relay");
    }
    for (unsigned i = 0; i < 16; ++i) {
        char buf[RECORD + 1];
        ssize_t n = recv(c->fd, buf, RECORD, MSG_DONTWAIT | MSG_TRUNC);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
        if (n <= 0 || n > RECORD || memchr(buf, 0, (size_t)n)) { lost(c); break; }
        buf[n] = 0;
        char *body = strchr(buf, '\n');
        unsigned request; unsigned long long epoch, event; char type[16];
        if (!body) { lost(c); break; }
        *body++ = 0;
        if (parse_header(buf, &request, &epoch, &event, type) || !epoch) { lost(c); break; }
        if (c->epoch != epoch) {
            if (c->epoch) c->event(c->context, "NOTICE", 0, "Chat connector restarted; messages received while disconnected are not saved");
            c->epoch = epoch; c->last = 0;
        }
        if (event) {
            if (event <= c->last) continue;
            c->last = event;
        }
        if (request && request == c->pending && (!strcmp(type, "OK") || !strcmp(type, "ERROR") || !strcmp(type, "STATE"))) c->pending = 0;
        c->event(c->context, type, request, body);
    }
}
unsigned app_chat_submit(struct app_chat *c, const char *line, char *error, unsigned capacity) {
    const char *why = NULL;
    if (!c || c->fd < 0) why = "Chat connector offline; message was not sent";
    else if (c->pending) why = "Wait for the previous command result";
    if (why) { snprintf(error, capacity, "%s", why); return 0; }
    const char *type = "SEND", *body = line;
    char direct[RECORD];
    if (!strncmp(line, "/msg ", 5)) {
        const char *target = line + 5;
        while (*target == ' ') ++target;
        const char *sep = strchr(target, ' ');
        if (!sep || sep == target || sep - target > 32) { snprintf(error, capacity, "Use /msg NAME text"); return 0; }
        const char *text = sep + 1; while (*text == ' ') ++text;
        if (!*text || strlen(text) > 900) { snprintf(error, capacity, "Direct message must contain 1–900 UTF-8 bytes (less with encryption)"); return 0; }
        snprintf(direct, sizeof(direct), "%.*s\n%s", (int)(sep-target), target, text);
        type = "DIRECT"; body = direct;
    } else if (!strcmp(line, "/who")) { type = "WHO"; body = ""; }
    else if (!strcmp(line, "/secret") || !strcmp(line, "/status")) { type = "STATE"; body = ""; }
    else if (!strncmp(line, "/secret ", 8)) {
        type = "SECRET"; body = line + 8;
        while (*body == ' ') ++body;
        if (!strcmp(body, "off")) body = "";
        else if (!*body) { snprintf(error, capacity, "Use /secret STRING or /secret off"); return 0; }
    } else if (*line == '/') { snprintf(error, capacity, "Commands: /msg NAME text, /who, /secret STRING, /secret off, /status"); return 0; }
    if (strlen(body) > 1024) { snprintf(error, capacity, "Command exceeds UTF-8 byte limit"); return 0; }
    unsigned request = ++c->next; if (!request) request = ++c->next;
    char wire[RECORD];
    int n = snprintf(wire, sizeof(wire), "UX1 %u 0 0 %s\n%s", request, type, body);
    ssize_t sent = send(c->fd, wire, (size_t)n, MSG_DONTWAIT | MSG_NOSIGNAL);
    wipe(wire, sizeof(wire)); wipe(direct, sizeof(direct));
    if (sent != n) {
        snprintf(error, capacity, "Connector unavailable or busy; command was not submitted");
        if (errno != EAGAIN && errno != EWOULDBLOCK) lost(c);
        return 0;
    }
    c->pending = request; c->deadline = monotime() + 10;
    return request;
}
