/* Mock only connection establishment; messages use real sequenced socket pairs. */
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
int chat_test_peer = -1, chat_test_disabled;
static int client = -1;
int __real_socket(int, int, int);
int __real_connect(int, const struct sockaddr *, socklen_t);
int __wrap_socket(int domain, int type, int protocol)
{
    if (domain != AF_UNIX || (type & 0xf) != SOCK_SEQPACKET)
        return __real_socket(domain, type, protocol);
    if (chat_test_disabled) { errno = ECONNREFUSED; return -1; }
    int pair[2];
    if (socketpair(domain, type, protocol, pair)) return -1;
    client = pair[0]; chat_test_peer = pair[1]; return client;
}
int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (fd == client) return 0;
    return __real_connect(fd, addr, len);
}
/* Real socket-pair traffic; avoid libc send's sendto syscall in sandboxes. */
ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags)
{
    struct iovec iov = {.iov_base = (void *)buf, .iov_len = len};
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};
    return sendmsg(fd, &msg, flags);
}
