/* Simulated evdev devices exercise discovery, hotplug and independent state. */
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
static bool present[3] = {true, true, true};
static struct input_event pending[3];
static bool queued[3];
static unsigned opens, closes;
static int device_index(const char *path) { return path[strlen(path)-1] - '0'; }
static int fake_open(const char *path, int flags, ...)
{
    (void)flags;
    int i = device_index(path);
    if (i < 0 || i > 2 || !present[i]) { errno = ENOENT; return -1; }
    ++opens;
    return 100 + i;
}
static int fake_close(int fd) { assert(fd >= 100 && fd <= 102); ++closes; return 0; }
static ssize_t fake_read(int fd, void *buffer, size_t size)
{
    int i = fd - 100;
    if (!present[i]) { errno = ENODEV; return -1; }
    if (!queued[i]) { errno = EAGAIN; return -1; }
    assert(size == sizeof(struct input_event));
    memcpy(buffer, &pending[i], size);
    queued[i] = false;
    return size;
}
static int fake_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    unsigned char *bits = va_arg(ap, unsigned char *);
    va_end(ap);
    memset(bits, 0, _IOC_SIZE(request));
    if (_IOC_NR(request) == _IOC_NR(EVIOCGBIT(EV_KEY, 0)) && fd != 102) {
        const unsigned keys[] = {KEY_A, KEY_Z, KEY_1, KEY_SPACE, KEY_ENTER};
        for (unsigned i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i)
            bits[keys[i]/8] |= 1u << (keys[i]%8);
    }
    return 0;
}
static int fake_stat(const char *path, struct stat *st)
{
    int i = device_index(path);
    if (i < 0 || i > 2 || !present[i]) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_rdev = i + 1;
    st->st_ino = i + 10;
    return 0;
}
static int fake_glob(const char *pattern, int flags, int (*err)(const char *, int), glob_t *g)
{
    (void)pattern; (void)flags; (void)err;
    static char *paths[] = {"/dev/input/event0", "/dev/input/event1", "/dev/input/event2"};
    g->gl_pathc = 3;
    g->gl_pathv = paths;
    return 0;
}
static void fake_globfree(glob_t *g) { (void)g; }
#define open fake_open
#define close fake_close
#define read fake_read
#define ioctl fake_ioctl
#define glob fake_glob
#define globfree fake_globfree
#define stat(path, st) fake_stat(path, st)
#include "../src/keyboard.c"

static lv_indev_data_t key(KeyboardDevice *device, unsigned code, int value)
{
    Keyboard *keyboard = lv_indev_get_user_data(device->indev);
    int i = keyboard->fd - 100;
    pending[i] = (struct input_event){.type = EV_KEY, .code = code, .value = value};
    queued[i] = true;
    lv_indev_data_t data = {0};
    read_keyboard(device->indev, &data);
    return data;
}

int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(640, 480);
    lv_group_t *group = lv_group_create();
    AppKeyboards *m = app_keyboards_start(NULL, group, display);
    assert(m && m->devices && m->devices->next && !m->devices->next->next);
    KeyboardDevice *external = m->devices, *internal = external->next;
    key(external, KEY_LEFTSHIFT, 1);
    assert(key(external, KEY_A, 1).key == 'A');
    assert(key(internal, KEY_A, 1).key == 'a');
    /* Lost event synchronization releases held input and discards stale keys. */
    pending[1] = (struct input_event){.type = EV_SYN, .code = SYN_DROPPED};
    queued[1] = true;
    lv_indev_data_t dropped = {0};
    read_keyboard(external->indev, &dropped);
    assert(dropped.state == LV_INDEV_STATE_RELEASED);
    assert(key(external, KEY_B, 1).state == LV_INDEV_STATE_RELEASED);
    pending[1] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
    queued[1] = true;
    read_keyboard(external->indev, &dropped);
    assert(key(external, KEY_A, 1).key == 'a');
    key(internal, KEY_A, 0);
    present[1] = false;
    lv_indev_data_t data = {0};
    read_keyboard(external->indev, &data);
    assert(data.state == LV_INDEV_STATE_RELEASED);
    scan_keyboards(m->timer);
    assert(m->devices == internal && !internal->next);
    assert(key(internal, KEY_Z, 1).key == 'z');
    present[1] = true;
    scan_keyboards(m->timer);
    assert(m->devices->next == internal);
    assert(key(m->devices, KEY_A, 1).key == 'a');
    app_keyboards_stop(m);
    m = app_keyboards_start("/dev/input/event0", group, display);
    assert(m && m->devices && !m->devices->next);
    app_keyboards_stop(m);
    present[0] = present[1] = false;
    m = app_keyboards_start(NULL, group, display);
    assert(m && !m->devices);
    present[0] = true;
    scan_keyboards(m->timer);
    assert(m->devices && !m->devices->next);
    app_keyboards_stop(m);
    assert(opens == closes);
    lv_group_delete(group);
    lv_deinit();
    puts("Keyboard discovery, simultaneous input, unplug, reconnect and override passed");
    return 0;
}
