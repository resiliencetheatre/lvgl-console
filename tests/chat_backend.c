#define _POSIX_C_SOURCE 200809L
#include "chat_backend.h"
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
extern int chat_test_peer, chat_test_disabled;
static time_t seconds = 100;
static unsigned chats, errors, notices, results;
int __wrap_clock_gettime(clockid_t clock, struct timespec *ts)
{
    (void)clock; ts->tv_sec = seconds; ts->tv_nsec = 0; return 0;
}
static void event(void *ctx, const char *type, unsigned request, const char *text)
{
    (void)ctx; (void)request; (void)text;
    if (!strcmp(type, "CHAT")) ++chats;
    if (!strcmp(type, "ERROR")) ++errors;
    if (!strcmp(type, "NOTICE")) ++notices;
    if (!strcmp(type, "OK")) ++results;
}
static void expect_command(const char *text)
{
    char wire[2049]; ssize_t n = recv(chat_test_peer, wire, sizeof(wire)-1, MSG_DONTWAIT);
    assert(n > 0); wire[n] = 0; assert(strstr(wire, text));
}
static void reply(struct app_chat *c, const char *text)
{
    assert(send(chat_test_peer, text, strlen(text), MSG_NOSIGNAL) == (ssize_t)strlen(text));
    app_chat_poll(c);
}
int main(void)
{
    struct app_chat *c = app_chat_create("/mock/chat.sock", event, NULL);
    assert(c); app_chat_poll(c); expect_command("UX1 0 0 0 RESUME\n");
    reply(c, "UX1 0 42 0 STATE\nrelay responding");
    char error[192];
    assert(app_chat_submit(c, "hello", error, sizeof(error)) == 1);
    expect_command("UX1 1 0 0 SEND\nhello");
    assert(!app_chat_submit(c, "second", error, sizeof(error)));
    reply(c, "UX1 1 42 1 CHAT\nhello");
    reply(c, "UX1 1 42 0 OK\nsubmitted");
    assert(chats == 1 && results == 1);
    reply(c, "UX1 0 42 1 CHAT\nhello"); assert(chats == 1);
    assert(app_chat_submit(c, "/msg bob hello", error, sizeof(error)) == 2);
    expect_command("UX1 2 0 0 DIRECT\nbob\nhello");
    close(chat_test_peer); app_chat_poll(c); assert(errors == 1);
    seconds += 2; app_chat_poll(c); expect_command("UX1 0 42 1 RESUME\n");
    reply(c, "UX1 0 42 2 CHAT\nwhile detached"); assert(chats == 2);
    assert(app_chat_submit(c, "/secret test secret", error, sizeof(error)) == 3);
    expect_command("UX1 3 0 0 SECRET\ntest secret");
    reply(c, "UX1 3 42 0 OK\nenabled");
    assert(app_chat_submit(c, "/secret off", error, sizeof(error)) == 4);
    expect_command("UX1 4 0 0 SECRET\n");
    seconds += 11; app_chat_poll(c); assert(errors == 2);
    close(chat_test_peer); seconds += 2;
    app_chat_poll(c); expect_command("UX1 0 42 2 RESUME\n");
    reply(c, "UX1 0 99 0 STATE\nconnector restarted"); assert(notices == 1);
    reply(c, "UX1 0 99 1 CHAT\nnew history"); assert(chats == 3);
    reply(c, "bad protocol");
    assert(!app_chat_submit(c, "offline", error, sizeof(error)));
    close(chat_test_peer); app_chat_destroy(c);
    puts("Chat IPC submission, deduplication, reconnect cursors, restart and uncertain outcomes passed");
    return 0;
}
