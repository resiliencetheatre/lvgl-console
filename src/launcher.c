#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef APP_EXECUTABLE
#define APP_EXECUTABLE "/usr/bin/lvgl-console"
#endif

/* Keep restoration in a separate process, including when the UI crashes. */
int main(int argc, char **argv)
{
    const char *tty_path = "/dev/tty0";
    char *default_command[] = {APP_EXECUTABLE, NULL};
    char **command = default_command;
    int arg = 1;
    if (arg < argc && strcmp(argv[arg], "--tty") == 0 && arg + 1 < argc) {
        tty_path = argv[arg + 1];
        arg += 2;
    }
    if (arg < argc && strcmp(argv[arg], "--") == 0 && arg + 1 < argc)
        command = &argv[arg + 1];
    else if (arg < argc) {
        bool help = strcmp(argv[arg], "--help") == 0;
        fprintf(help ? stdout : stderr,
                "Usage: %s [--tty /dev/ttyN] [-- command [args...]]\n"
                "Defaults: active Linux VT and " APP_EXECUTABLE "\n"
                "The selected VT must be active and in text mode.\n", argv[0]);
        return help ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Block shutdown signals before touching the console. sigwaitinfo avoids
     * handler/fork/wait races and keeps all cleanup outside signal handlers. */
    const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGCHLD, SIGALRM};
    sigset_t watched, previous_mask;
    sigemptyset(&watched);
    struct sigaction action = {.sa_handler = SIG_DFL};
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        sigaddset(&watched, signals[i]);
        if (sigaction(signals[i], &action, NULL) < 0) {
            perror("sigaction");
            return EXIT_FAILURE;
        }
    }
    if (sigprocmask(SIG_BLOCK, &watched, &previous_mask) < 0) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }
    /* Do not suspend the console guardian through shell job control. */
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    int result = EXIT_FAILURE, saved_display = KD_TEXT, saved_keyboard = K_UNICODE;
    bool display_changed = false, keyboard_changed = false;
    int tty = open(tty_path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (tty < 0) {
        perror(tty_path);
        return EXIT_FAILURE;
    }
    struct vt_stat vt;
    struct stat info;
    if (ioctl(tty, VT_GETSTATE, &vt) < 0 || fstat(tty, &info) < 0) {
        perror("Inspect virtual terminal");
        goto cleanup;
    }
    /* /dev/tty0 follows the active VT. Reopen the numbered device so cleanup
     * still restores the same terminal if another process switches VTs. */
    if (major(info.st_rdev) == 4 && minor(info.st_rdev) == 0) {
        char numbered_tty[32];
        snprintf(numbered_tty, sizeof(numbered_tty), "/dev/tty%u", vt.v_active);
        close(tty);
        tty = open(numbered_tty, O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (tty < 0 || fstat(tty, &info) < 0) {
            perror("Open active virtual terminal");
            goto cleanup;
        }
    }
    if (!S_ISCHR(info.st_mode) || major(info.st_rdev) != 4 ||
        minor(info.st_rdev) < 1 || minor(info.st_rdev) > 63 ||
        ioctl(tty, VT_GETSTATE, &vt) < 0 || minor(info.st_rdev) != vt.v_active) {
        fprintf(stderr, "Selected device must be the active Linux virtual terminal\n");
        goto cleanup;
    }
    if (flock(tty, LOCK_EX | LOCK_NB) < 0) {
        perror("Lock virtual terminal (another launcher may own it)");
        goto cleanup;
    }
    if (ioctl(tty, KDGETMODE, &saved_display) < 0 ||
        ioctl(tty, KDGKBMODE, &saved_keyboard) < 0) {
        perror("Save console modes");
        goto cleanup;
    }
    if (saved_display != KD_TEXT) {
        fprintf(stderr, "Virtual terminal already belongs to a graphics application\n");
        goto cleanup;
    }
    if (ioctl(tty, KDSKBMODE, K_OFF) < 0) {
        perror("Disable console keyboard processing");
        goto cleanup;
    }
    keyboard_changed = true;
    if (ioctl(tty, KDSETMODE, KD_GRAPHICS) < 0) {
        perror("Enable graphics mode");
        goto cleanup;
    }
    display_changed = true;

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        goto cleanup;
    }
    if (child == 0) {
        if (setpgid(0, 0) < 0) _exit(126);
        close(tty);
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0) _exit(126);
        if (null_fd != STDIN_FILENO) close(null_fd);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        sigprocmask(SIG_SETMASK, &previous_mask, NULL);
        execvp(command[0], command);
        perror(command[0]);
        _exit(127);
    }
    /* Both sides set the group, so forwarding works before or after exec. */
    if (setpgid(child, child) < 0 && errno != EACCES && errno != ESRCH)
        perror("setpgid");
    bool stopping = false;
    for (;;) {
        int status;
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            break;
        }
        if (waited < 0 && errno != EINTR) {
            perror("waitpid");
            break;
        }
        int signo = sigwaitinfo(&watched, NULL);
        if (signo < 0) {
            if (errno == EINTR) continue;
            perror("sigwaitinfo");
            kill(-child, SIGKILL);
            while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
            break;
        }
        if (signo == SIGCHLD) continue;
        if (signo == SIGALRM) {
            kill(-child, SIGKILL);
        }
        else {
            kill(-child, signo);
            if (!stopping) {
                stopping = true;
                alarm(5);
            }
        }
    }
    alarm(0);

cleanup:
    /* Attempt both restorations even if one fails. Keep shutdown signals
     * blocked until exit so a second SIGTERM cannot interrupt restoration. */
    if (display_changed && ioctl(tty, KDSETMODE, saved_display) < 0) {
        perror("Restore console display mode");
        result = EXIT_FAILURE;
    }
    if (keyboard_changed && ioctl(tty, KDSKBMODE, saved_keyboard) < 0) {
        perror("Restore console keyboard mode");
        result = EXIT_FAILURE;
    }
    if (tty >= 0) close(tty);
    return result;
}
