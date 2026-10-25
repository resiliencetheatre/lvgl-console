#include "ui.h"
#include "ping_status.h"
#include "ptt_status.h"
#include "settings.h"
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int sleep_fd = -1;
static lv_timer_t *wake_timer;
static lv_obj_t *sleep_button;
static lv_obj_t *sleep_status;

/* Single-line metrics plus a wider PTT box at 640px. */
#define STATUS_BOX_WIDTH 104
#define STATUS_BOX_HEIGHT 28
#define STATUS_BAR_HEIGHT 36
#define STATUS_ALARM_COLOR 0x401010
#define STATUS_RX_COLOR 0x101040

struct ping_panel {
    lv_obj_t *boxes[4];
    lv_obj_t *labels[4];
    lv_obj_t *ptt_label;
    lv_obj_t *ptt_box;
    unsigned ptt_color;
    int alarms[4];
    unsigned ticks;
    lv_timer_t *timer;
};

static void set_label_if_changed(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void ping_tick(lv_timer_t *timer)
{
    struct ping_panel *panel = lv_timer_get_user_data(timer);
    if (sleep_fd >= 0) return;
    char text[320];
    app_ptt_status_read("/run/udpptt/state", "/tmp/udpptt.sock", text, sizeof(text));
    set_label_if_changed(panel->ptt_label, text);
    unsigned ptt_color = !strncmp(text, "TX ", 3) ? STATUS_ALARM_COLOR :
                         !strncmp(text, "RX ", 3) ? STATUS_RX_COLOR : 0;
    if (ptt_color != panel->ptt_color) {
        panel->ptt_color = ptt_color;
        lv_obj_set_style_bg_color(panel->ptt_box, lv_color_hex(ptt_color), 0);
    }
    if (panel->ticks++ % 4) return;
    static const char *names[] = {"gateway", "internet", "vps", "wireguard"};
    time_t now = time(NULL);
    for (unsigned i = 0; i < 4; ++i) {
        char path[80];
        snprintf(path, sizeof(path), "/tmp/dpinger/%s.status", names[i]);
        int alarm = app_ping_status_read(path, now, text, sizeof(text));
        set_label_if_changed(panel->labels[i], text);
        if (alarm != panel->alarms[i]) {
            panel->alarms[i] = alarm;
            lv_obj_set_style_bg_color(panel->boxes[i],
                lv_color_hex(alarm == 1 ? STATUS_ALARM_COLOR : 0), 0);
        }
    }
}

static void delete_ping_panel(lv_event_t *event)
{
    struct ping_panel *panel = lv_event_get_user_data(event);
    if (panel->timer) lv_timer_delete(panel->timer);
    free(panel);
}

static void create_ping_panel(lv_obj_t *screen)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, LV_PCT(100), STATUS_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_scrollable(bar, false);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_set_style_pad_column(bar, 4, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    struct ping_panel *panel = calloc(1, sizeof(*panel));
    if (!panel) return;
    for (unsigned i = 0; i < 5; ++i) {
        lv_obj_t *box = lv_obj_create(bar);
        lv_obj_set_size(box, STATUS_BOX_WIDTH, STATUS_BOX_HEIGHT);
        if (i == 4) lv_obj_set_flex_grow(box, 1);
        lv_obj_set_scrollable(box, false);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_pad_all(box, 2, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(box, &lv_font_montserrat_14, 0);
        lv_obj_t *value = lv_label_create(box);
        lv_obj_set_width(value, LV_PCT(100));
        lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
        if (i < 4) {
            panel->boxes[i] = box;
            panel->labels[i] = value;
            panel->alarms[i] = -1;
            lv_label_set_text(value, "--/--");
        } else {
            panel->ptt_box = box;
            panel->ptt_label = value;
            lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
            lv_label_set_text(value, "PTT offline");
        }
    }
    panel->timer = lv_timer_create(ping_tick, 250, panel);
    if (panel->timer) lv_timer_ready(panel->timer);
    lv_obj_add_event_cb(bar, delete_ping_panel, LV_EVENT_DELETE, panel);
}

#define SCREENSAVER_IDLE_MS 60000
static app_settings saved_settings;
static lv_obj_t *settings_fields[3], *saver_checkbox, *save_button, *save_feedback;
static bool saver_sleeping;
static uint32_t saver_last_idle;

static void saver_tick(lv_timer_t *timer)
{
    lv_display_t *display = lv_timer_get_user_data(timer);
    uint32_t idle = lv_display_get_inactive_time(display);
    if (saver_sleeping) {
        if (idle < saver_last_idle || !saved_settings.screensaver) {
            app_ui_resume_display();
            saver_sleeping = false;
        }
        saver_last_idle = idle;
        return;
    }
    if (!saved_settings.screensaver || sleep_fd >= 0 || idle < SCREENSAVER_IDLE_MS) return;
    const char *path = getenv("LV_LINUX_FBDEV_DEVICE");
    if (!path || !*path) path = "/dev/fb0";
    sleep_fd = open(path, O_RDWR | O_CLOEXEC);
    if (sleep_fd < 0 || ioctl(sleep_fd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
        perror("Screen saver blank");
        if (sleep_fd >= 0) close(sleep_fd);
        sleep_fd = -1;
        lv_display_trigger_activity(display); /* Back off before retrying. */
        return;
    }
    saver_sleeping = true;
    saver_last_idle = idle;
}

static void delete_saver_timer(lv_event_t *event)
{
    lv_timer_delete(lv_event_get_user_data(event));
}

static void save_settings(lv_event_t *event)
{
    (void)event;
    app_settings next = {0};
    snprintf(next.txid, sizeof(next.txid), "%s", lv_textarea_get_text(settings_fields[0]));
    snprintf(next.dpi_id, sizeof(next.dpi_id), "%s", lv_textarea_get_text(settings_fields[1]));
    snprintf(next.preamble, sizeof(next.preamble), "%s", lv_textarea_get_text(settings_fields[2]));
    next.screensaver = lv_obj_has_state(saver_checkbox, LV_STATE_CHECKED);
    if (!app_settings_valid(&next)) {
        lv_label_set_text(save_feedback, "IDs: letters, digits, . _ -; preamble: exactly 5 A-Z letters.");
        return;
    }
    if (app_settings_save("/etc/default/udpptt", &next)) {
        lv_label_set_text_fmt(save_feedback, "Save failed: %s", strerror(errno));
        return;
    }
    saved_settings = next;
    lv_display_trigger_activity(lv_obj_get_display(save_feedback));
    lv_label_set_text(save_feedback, "Saved. Restart udpptt.service to apply PTT IDs.");
}

void app_ui_add_settings_to_group(lv_group_t *group)
{
    for (unsigned i = 0; i < 3; ++i) lv_group_add_obj(group, settings_fields[i]);
    lv_group_add_obj(group, saver_checkbox);
    lv_group_add_obj(group, save_button);
}

/* Also called before LVGL shuts down, so a normal exit cannot leave HDMI off. */
void app_ui_resume_display(void)
{
    if (wake_timer) {
        lv_timer_delete(wake_timer);
        wake_timer = NULL;
    }
    if (sleep_fd < 0) return;
    int result = ioctl(sleep_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    if (result < 0) perror("Display unblank");
    close(sleep_fd);
    sleep_fd = -1;
    lv_obj_remove_state(sleep_button, LV_STATE_DISABLED);
    lv_label_set_text(sleep_status, result < 0 ? "Display wake failed; see log." : "Display resumed.");
    lv_display_t *display = lv_obj_get_display(sleep_button);
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_obj_invalidate(lv_display_get_layer_top(display));
    lv_obj_invalidate(lv_display_get_layer_sys(display));
    lv_refr_now(display);
}

static void wake_display(lv_timer_t *timer)
{
    (void)timer;
    app_ui_resume_display();
}

static void sleep_display(lv_event_t *event)
{
    (void)event;
    if (sleep_fd >= 0) return;
    const char *path = getenv("LV_LINUX_FBDEV_DEVICE");
    if (!path || !*path) path = "/dev/fb0";
    sleep_fd = open(path, O_RDWR | O_CLOEXEC);
    if (sleep_fd < 0) {
        perror("Open framebuffer for blanking");
        lv_label_set_text(sleep_status, "Cannot open display; see log.");
        return;
    }
    wake_timer = lv_timer_create(wake_display, 5000, NULL);
    if (!wake_timer) {
        close(sleep_fd);
        sleep_fd = -1;
        lv_label_set_text(sleep_status, "Cannot schedule display wake.");
        return;
    }
    if (ioctl(sleep_fd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
        perror("Display blank");
        lv_timer_delete(wake_timer);
        wake_timer = NULL;
        close(sleep_fd);
        sleep_fd = -1;
        lv_label_set_text(sleep_status, "Display sleep failed; see log.");
        return;
    }
    lv_timer_reset(wake_timer);
    lv_obj_add_state(sleep_button, LV_STATE_DISABLED);
    lv_label_set_text(sleep_status, "Waking in 5 seconds...");
}

/* Keep the palette here so every tab and nested widget uses the same theme. */
#define NEON_GREEN 0x39FF14
#define DIM_GREEN 0x103D08

static void apply_neon_theme(lv_obj_t *obj)
{
    const lv_color_t green = lv_color_hex(NEON_GREEN);
    const lv_style_selector_t states[] = {
        LV_STATE_DEFAULT, LV_STATE_PRESSED, LV_STATE_CHECKED,
        LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY, LV_STATE_EDITED,
        LV_STATE_HOVERED, LV_STATE_DISABLED
    };
    for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        lv_style_selector_t state = states[i];
        lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, state);
        lv_obj_set_style_bg_color(obj, lv_color_black(), state);
        lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, state);
        lv_obj_set_style_text_color(obj, green, state);
        lv_obj_set_style_border_color(obj, green, state);
        lv_obj_set_style_outline_color(obj, green, state);
        lv_obj_set_style_shadow_color(obj, green, state);
        lv_obj_set_style_shadow_width(obj, 0, state);
        lv_obj_set_style_bg_color(obj, green, LV_PART_SCROLLBAR | state);
        lv_obj_set_style_border_color(obj, green, LV_PART_SCROLLBAR | state);
    }
    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(obj, 3, LV_STATE_CHECKED);
        /* Focus remains after touch release and outranks CHECKED in LVGL.
         * Keep the selected fill for every combined interaction state. */
        for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
            lv_style_selector_t selected = LV_STATE_CHECKED | states[i];
            lv_obj_set_style_bg_color(obj, lv_color_hex(DIM_GREEN), selected);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, selected);
        }
        lv_obj_set_style_bg_color(obj, lv_color_hex(DIM_GREEN), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(obj, 12, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_50, LV_STATE_PRESSED);
    }
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_set_style_text_color(obj, green, LV_PART_TEXTAREA_PLACEHOLDER);
        /* Cursor geometry/opacity must be explicit, not dependent on the
         * theme's focus-only cursor style. Match its focused selectors too. */
        for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
            lv_style_selector_t cursor = LV_PART_CURSOR | states[i];
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, cursor);
            lv_obj_set_style_border_color(obj, green, cursor);
            lv_obj_set_style_border_width(obj, 2, cursor);
            lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, cursor);
            lv_obj_set_style_border_opa(obj, LV_OPA_COVER, cursor);
            lv_obj_set_style_opa(obj, LV_OPA_COVER, cursor);
            lv_obj_set_style_pad_all(obj, 0, cursor);
            lv_obj_set_style_anim_duration(obj, 0, cursor);
        }
        lv_obj_set_style_bg_color(obj, green, LV_PART_SELECTED);
        lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_SELECTED);
    }
    if (lv_obj_check_type(obj, &lv_image_class)) {
        /* Recolor at draw time, preserving the logo's transparent pixels. */
        lv_obj_set_style_image_recolor(obj, green, 0);
        lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, 0);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        apply_neon_theme(lv_obj_get_child(obj, i));
}

/* Local-only chat demo. Replace this append callback when a backend is chosen. */
static void append_chat_message(lv_event_t *event)
{
    lv_obj_t *input = lv_event_get_target_obj(event);
    lv_obj_t *history = lv_event_get_user_data(event);
    const char *text = lv_textarea_get_text(input);
    bool nonempty = false;
    for (const char *p = text; *p; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') nonempty = true;
    }
    if (!nonempty) return;
    /* Bound memory usage while retaining enough messages to test scrolling. */
    if (lv_obj_get_child_count(history) >= 100)
        lv_obj_delete(lv_obj_get_child(history, 0));
    lv_obj_t *message = lv_label_create(history);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(message, text);
    apply_neon_theme(message);
    lv_textarea_set_text(input, "");
    lv_obj_update_layout(history);
    lv_obj_scroll_to_view(message, LV_ANIM_OFF);
}

lv_obj_t *app_ui_create(lv_obj_t *screen)
{
    lv_obj_t *tabs = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(tabs, 48);
    lv_obj_set_size(tabs, LV_PCT(100), lv_obj_get_height(screen) - STATUS_BAR_HEIGHT);
    lv_obj_t *comm = lv_tabview_add_tab(tabs, "PTT");
    lv_obj_t *status = lv_tabview_add_tab(tabs, "Messaging");
    lv_obj_t *settings = lv_tabview_add_tab(tabs, "Settings");
    lv_obj_set_flex_flow(comm, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(comm, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *logo = lv_image_create(comm);
    lv_image_set_src(logo, "A:/link.png");
    lv_obj_set_size(logo, LV_PCT(100), 101);
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_CONTAIN);

    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(status, 8, 0);
    lv_obj_set_scrollable(status, false);
    lv_obj_t *history = lv_obj_create(status);
    lv_obj_set_width(history, LV_PCT(100));
    lv_obj_set_height(history, 0);
    lv_obj_set_flex_grow(history, 1);
    lv_obj_set_flex_flow(history, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(history, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(history, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(history, 10, 0);
    lv_obj_t *input = lv_textarea_create(status);
    /* Keyboard focus must not scroll away from the initial PTT tab. */
    lv_obj_set_scroll_on_focus(input, false);
    lv_obj_set_width(input, LV_PCT(100));
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 512);
    lv_textarea_set_placeholder_text(input, "Type a message; Enter to add locally");
    lv_textarea_set_text(input, "");
    lv_obj_add_event_cb(input, append_chat_message, LV_EVENT_READY, history);
    lv_obj_set_flex_flow(settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings, 16, 0);
    int loaded = app_settings_load("/etc/default/udpptt", &saved_settings);
    const char *field_labels[] = {"PTT transmit ID", "PTT dpi ID", "PTT preamble ID\n(5 letters only)"};
    const char *values[] = {saved_settings.txid, saved_settings.dpi_id, saved_settings.preamble};
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *row = lv_obj_create(settings);
        lv_obj_set_size(row, LV_PCT(100), 52);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        settings_fields[i] = lv_textarea_create(row);
        lv_obj_set_size(settings_fields[i], 210, 44);
        lv_obj_set_scroll_on_focus(settings_fields[i], false);
        lv_textarea_set_one_line(settings_fields[i], true);
        lv_textarea_set_max_length(settings_fields[i], i == 2 ? 5 : i == 0 ? 31 : 63);
        lv_textarea_set_accepted_chars(settings_fields[i], i == 2 ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ" :
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-");
        lv_textarea_set_text(settings_fields[i], values[i]);
        lv_obj_t *caption = lv_label_create(row);
        lv_label_set_text(caption, field_labels[i]);
    }
    saver_checkbox = lv_checkbox_create(settings);
    lv_checkbox_set_text(saver_checkbox, "Screen saver (60 seconds)");
    if (saved_settings.screensaver) lv_obj_add_state(saver_checkbox, LV_STATE_CHECKED);
    lv_obj_set_scroll_on_focus(saver_checkbox, false);
    save_button = lv_button_create(settings);
    lv_obj_set_size(save_button, 110, 40);
    lv_obj_set_scroll_on_focus(save_button, false);
    lv_obj_t *save_label = lv_label_create(save_button);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_button, save_settings, LV_EVENT_CLICKED, NULL);
    save_feedback = lv_label_create(settings);
    lv_obj_set_width(save_feedback, LV_PCT(100));
    lv_label_set_text(save_feedback, loaded ? "Configuration unavailable; defaults shown." : "");
    lv_timer_t *saver_timer = lv_timer_create(saver_tick, 250, lv_obj_get_display(screen));
    if (saver_timer) lv_obj_add_event_cb(screen, delete_saver_timer, LV_EVENT_DELETE, saver_timer);
    sleep_button = lv_button_create(settings);
    lv_obj_set_size(sleep_button, LV_SIZE_CONTENT, 60);
    lv_obj_t *sleep_label = lv_label_create(sleep_button);
    lv_label_set_text(sleep_label, "Sleep screen for 5 seconds");
    lv_obj_center(sleep_label);
    lv_obj_add_event_cb(sleep_button, sleep_display, LV_EVENT_CLICKED, NULL);
    sleep_status = lv_label_create(settings);
    lv_label_set_text(sleep_status, "Wakes automatically and redraws the screen.");
    /* No virtual keyboard is created. */
    lv_tabview_set_active(tabs, 0, LV_ANIM_OFF);
    create_ping_panel(screen);
    apply_neon_theme(screen);
    return input;
}
