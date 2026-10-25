#include "keyboard.h"
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    int fd;
    lv_obj_t *tabs;
    bool left_shift, right_shift, caps;
    uint32_t key;
    lv_indev_state_t state;
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

static void read_keyboard(lv_indev_t *indev, lv_indev_data_t *data)
{
    Keyboard *keyboard = lv_indev_get_user_data(indev);
    struct input_event event;
    while (read(keyboard->fd, &event, sizeof(event)) == sizeof(event)) {
        if (event.type != EV_KEY) continue;
        if (keyboard->tabs && (event.code == KEY_LEFT || event.code == KEY_RIGHT)) {
            /* Consume release and kernel repeat too, so navigation never
             * reaches the text field or repeats at the keyboard's 33 ms rate. */
            if (event.value == 1) {
                uint32_t count = lv_tabview_get_tab_count(keyboard->tabs);
                uint32_t active = lv_tabview_get_tab_active(keyboard->tabs);
                if (count) {
                    uint32_t next = event.code == KEY_RIGHT
                        ? (active + 1) % count : (active + count - 1) % count;
                    lv_tabview_set_active(keyboard->tabs, next, LV_ANIM_OFF);
                }
            }
            continue;
        }
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
    data->key = keyboard->key;
    data->state = keyboard->state;
}

static void delete_keyboard(lv_event_t *event)
{
    Keyboard *keyboard = lv_event_get_user_data(event);
    close(keyboard->fd);
    free(keyboard);
}

lv_indev_t *app_keyboard_create(const char *path, lv_group_t *group, lv_obj_t *tabs)
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
    keyboard->tabs = tabs;
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_user_data(indev, keyboard);
    lv_indev_set_read_cb(indev, read_keyboard);
    lv_indev_set_group(indev, group);
    lv_indev_add_event_cb(indev, delete_keyboard, LV_EVENT_DELETE, keyboard);
    return indev;
}
