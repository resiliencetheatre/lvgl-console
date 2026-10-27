/* Never execute a shutdown command from the headless UI test. */
#include <spawn.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
int poweroff_test_calls, poweroff_test_spawn_error, poweroff_test_exit_status;
int __wrap_posix_spawn(pid_t *pid, const char *path,
                       const posix_spawn_file_actions_t *actions,
                       const posix_spawnattr_t *attributes,
                       char *const argv[], char *const env[])
{
    (void)env;
    if (strcmp(path, "/bin/systemctl") || actions || attributes ||
        strcmp(argv[0], path) || strcmp(argv[1], "--no-block") ||
        strcmp(argv[2], "poweroff") || argv[3]) abort();
    ++poweroff_test_calls;
    if (poweroff_test_spawn_error) return poweroff_test_spawn_error;
    *pid = 12345;
    return 0;
}
pid_t __wrap_waitpid(pid_t pid, int *status, int options)
{
    if (pid != 12345 || options != WNOHANG) abort();
    if (status) *status = poweroff_test_exit_status << 8;
    return pid;
}
