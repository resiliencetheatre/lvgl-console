#include "keyboard.h"
#include <fcntl.h>
#include <glob.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    int fd;
    bool left_shift, right_shift, caps;
    uint32_t key;
    lv_indev_state_t state;
    bool dropped;
} Keyboard;

/* Linux evdev key codes follow physical positions: this demo uses US layout. */
static uint32_t translate(unsigned code, bool shift, bool caps)
{
    static const char plain[KEY_MAX + 1] = {
        [KEY_1] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
        [KEY_Q] = 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
        [KEY_A] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        [KEY_BACKSLASH] = '\\',
        [KEY_Z] = 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
        [KEY_SPACE] = ' '
    };
    static const char shifted[KEY_MAX + 1] = {
        [KEY_1] = '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
        [KEY_LEFTBRACE] = '{', '}',
        [KEY_SEMICOLON] = ':', '"', '~',
        [KEY_BACKSLASH] = '|',
        [KEY_COMMA] = '<', '>', '?'
    };
    switch (code) {
    case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
    case KEY_DELETE: return LV_KEY_DEL;
    case KEY_LEFT: return LV_KEY_LEFT;
    case KEY_RIGHT: return LV_KEY_RIGHT;
    case KEY_HOME: return LV_KEY_HOME;
    case KEY_END: return LV_KEY_END;
    case KEY_ENTER: case KEY_KPENTER: return LV_KEY_ENTER;
    case KEY_TAB: return shift ? LV_KEY_PREV : LV_KEY_NEXT;
    case KEY_ESC: return LV_KEY_ESC;
    default: break;
    }
    if (code > KEY_MAX) return 0;
    char c = plain[code];
    if (c >= 'a' && c <= 'z') return (shift != caps) ? c - 'a' + 'A' : c;
    return shift && shifted[code] ? shifted[code] : c;
}

static void sync_modifiers(Keyboard *keyboard)
{
    unsigned char keys[(KEY_MAX + 8) / 8] = {0};
    unsigned char leds[(LED_MAX + 8) / 8] = {0};
    if (ioctl(keyboard->fd, EVIOCGKEY(sizeof(keys)), keys) >= 0) {
        keyboard->left_shift = !!(keys[KEY_LEFTSHIFT / 8] & (1u << (KEY_LEFTSHIFT % 8)));
        keyboard->right_shift = !!(keys[KEY_RIGHTSHIFT / 8] & (1u << (KEY_RIGHTSHIFT % 8)));
    }
    if (ioctl(keyboard->fd, EVIOCGLED(sizeof(leds)), leds) >= 0)
        keyboard->caps = !!(leds[LED_CAPSL / 8] & (1u << (LED_CAPSL % 8)));
}

static void read_keyboard(lv_indev_t *indev, lv_indev_data_t *data)
{
    Keyboard *keyboard = lv_indev_get_user_data(indev);
    struct input_event event;
    ssize_t n = -1;
    while (keyboard->fd >= 0 && (n = read(keyboard->fd, &event, sizeof(event))) == sizeof(event)) {
        if (event.type == EV_SYN && event.code == SYN_DROPPED) {
            keyboard->dropped = true;
            keyboard->left_shift = keyboard->right_shift = false;
            keyboard->state = LV_INDEV_STATE_RELEASED;
        }
        if (keyboard->dropped) {
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                sync_modifiers(keyboard);
                keyboard->dropped = false;
            }
            continue;
        }
        if (event.type != EV_KEY) continue;
        if (event.code == KEY_LEFTSHIFT) keyboard->left_shift = event.value != 0;
        else if (event.code == KEY_RIGHTSHIFT) keyboard->right_shift = event.value != 0;
        else if (event.code == KEY_CAPSLOCK) {
            if (event.value == 1) keyboard->caps = !keyboard->caps;
        }
        else {
            uint32_t key = translate(event.code,
                                     keyboard->left_shift || keyboard->right_shift,
                                     keyboard->caps);
            if (!key) continue;
            /* LVGL handles held-key repeat. Preserve the last key on release. */
            if (event.value == 2) continue;
            if (event.value) keyboard->key = key;
            keyboard->state = event.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            data->continue_reading = true;
            break;
        }
    }
    if (keyboard->fd >= 0 && n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        close(keyboard->fd);
        keyboard->fd = -1;
        keyboard->left_shift = keyboard->right_shift = false;
        keyboard->state = LV_INDEV_STATE_RELEASED;
    }
    data->key = keyboard->key;
    data->state = keyboard->state;
}

static void delete_keyboard(lv_event_t *event)
{
    Keyboard *keyboard = lv_event_get_user_data(event);
    if (keyboard->fd >= 0) close(keyboard->fd);
    free(keyboard);
}

lv_indev_t *app_keyboard_create(const char *path, lv_group_t *group)
{
    glob_t matches = {0};
    if (!path || !*path) {
        if (glob("/dev/input/by-id/*-event-kbd", 0, NULL, &matches) == 0)
            path = matches.gl_pathv[0];
        else path = "/dev/input/keyboard";
    }
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) fprintf(stderr, "Unable to open keyboard %s; set LV_LINUX_KEYBOARD_DEVICE\n", path);
    globfree(&matches);
    if (fd < 0) return NULL;
    Keyboard *keyboard = calloc(1, sizeof(*keyboard));
    lv_indev_t *indev = keyboard ? lv_indev_create() : NULL;
    if (!indev) {
        close(fd);
        free(keyboard);
        return NULL;
    }
    keyboard->fd = fd;
    sync_modifiers(keyboard);
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_user_data(indev, keyboard);
    lv_indev_set_read_cb(indev, read_keyboard);
    lv_indev_set_group(indev, group);
    lv_indev_add_event_cb(indev, delete_keyboard, LV_EVENT_DELETE, keyboard);
    return indev;
}

/* Each physical keyboard has its own LVGL input and modifier state. */
typedef struct KeyboardDevice {
    char *path;
    dev_t device;
    ino_t inode;
    lv_indev_t *indev;
    struct KeyboardDevice *next;
} KeyboardDevice;

struct AppKeyboards {
    char *path;
    lv_group_t *group;
    lv_display_t *display;
    lv_timer_t *timer;
    KeyboardDevice *devices;
};

static bool typing_device(const char *path)
{
    unsigned char keys[(KEY_MAX + 8) / 8] = {0};
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0;
    close(fd);
    /* Pico also advertises REL/ABS controls: do not reject mixed devices. */
    const unsigned required[] = {KEY_A, KEY_Z, KEY_1, KEY_SPACE, KEY_ENTER};
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        if (!(keys[required[i] / 8] & (1u << (required[i] % 8)))) ok = false;
    return ok;
}

static void scan_keyboards(lv_timer_t *timer)
{
    AppKeyboards *manager = lv_timer_get_user_data(timer);
    KeyboardDevice **link = &manager->devices;
    while (*link) {
        KeyboardDevice *device = *link;
        Keyboard *keyboard = lv_indev_get_user_data(device->indev);
        struct stat st;
        if (keyboard->fd < 0 || stat(device->path, &st) < 0 ||
            st.st_rdev != device->device || st.st_ino != device->inode) {
            *link = device->next;
            lv_indev_delete(device->indev);
            free(device->path);
            free(device);
        } else link = &device->next;
    }
    glob_t matches = {0};
    if (!manager->path && glob("/dev/input/event*", 0, NULL, &matches) != 0) {
        globfree(&matches);
        return;
    }
    size_t count = manager->path ? 1 : matches.gl_pathc;
    for (size_t i = 0; i < count; ++i) {
        const char *path = manager->path ? manager->path : matches.gl_pathv[i];
        struct stat st;
        if (stat(path, &st) < 0) continue;
        KeyboardDevice *existing = manager->devices;
        while (existing && strcmp(existing->path, path)) existing = existing->next;
        if (existing || !typing_device(path)) continue;
        KeyboardDevice *device = calloc(1, sizeof(*device));
        if (!device) continue;
        device->path = strdup(path);
        device->indev = device->path ? app_keyboard_create(path, manager->group) : NULL;
        if (!device->indev) { free(device->path); free(device); continue; }
        device->device = st.st_rdev;
        device->inode = st.st_ino;
        lv_indev_set_display(device->indev, manager->display);
        device->next = manager->devices;
        manager->devices = device;
        fprintf(stderr, "Keyboard connected: %s\n", path);
    }
    globfree(&matches);
}

AppKeyboards *app_keyboards_start(const char *path, lv_group_t *group, lv_display_t *display)
{
    AppKeyboards *manager = calloc(1, sizeof(*manager));
    if (!manager) return NULL;
    if (path && *path) {
        manager->path = strdup(path);
        if (!manager->path) { free(manager); return NULL; }
    }
    manager->group = group;
    manager->display = display;
    manager->timer = lv_timer_create(scan_keyboards, 1000, manager);
    if (!manager->timer) { free(manager->path); free(manager); return NULL; }
    scan_keyboards(manager->timer);
    return manager;
}

void app_keyboards_stop(AppKeyboards *manager)
{
    if (!manager) return;
    lv_timer_delete(manager->timer);
    while (manager->devices) {
        KeyboardDevice *device = manager->devices;
        manager->devices = device->next;
        lv_indev_delete(device->indev);
        free(device->path);
        free(device);
    }
    free(manager->path);
    free(manager);
}
