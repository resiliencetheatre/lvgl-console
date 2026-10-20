#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/* Link-time wrappers apply only to the test launcher, never the installed one. */
int __real_open(const char *path, int flags, ...);
int __real_fstat(int fd, struct stat *info);
pid_t __real_fork(void);
static unsigned terminal;

int __wrap_open(const char *path, int flags, ...)
{
    if (strncmp(path, "/dev/tty", 8) == 0) {
        terminal = (unsigned)strtoul(path + 8, NULL, 10);
        return __real_open(getenv("TEST_VT_FILE"), flags);
    }
    return __real_open(path, flags);
}

int __wrap_fstat(int fd, struct stat *info)
{
    int result = __real_fstat(fd, info);
    info->st_mode = S_IFCHR | 0600;
    info->st_rdev = makedev(4, terminal);
    return result;
}

pid_t __wrap_fork(void)
{
    if (getenv("TEST_FORK_FAIL")) {
        errno = EAGAIN;
        return -1;
    }
    return __real_fork();
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    va_list args;
    va_start(args, request);
    int result = 0;
    switch (request) {
    case VT_GETSTATE:
        va_arg(args, struct vt_stat *)->v_active = 1;
        break;
    case KDGETMODE:
        *va_arg(args, int *) = getenv("TEST_ALREADY_GRAPHICS") ? KD_GRAPHICS : KD_TEXT;
        break;
    case KDGKBMODE:
        *va_arg(args, int *) = K_XLATE;
        break;
    case KDSETMODE:
    case KDSKBMODE: {
        int value = va_arg(args, int);
        FILE *log = fopen(getenv("TEST_VT_LOG"), "a");
        if (!log) abort();
        fprintf(log, "%s %d\n", request == KDSETMODE ? "display" : "keyboard", value);
        fclose(log);
        if (request == KDSETMODE && value == KD_GRAPHICS && getenv("TEST_GRAPHICS_FAIL")) {
            errno = EIO;
            result = -1;
        }
        break;
    }
    default:
        errno = EINVAL;
        result = -1;
    }
    va_end(args);
    return result;
}
