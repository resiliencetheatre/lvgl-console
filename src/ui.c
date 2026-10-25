#include "ui.h"
#include "ping_status.h"
#include "ptt_status.h"
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
        lv_obj_set_style_bg_color(obj, green, LV_PART_CURSOR);
        lv_obj_set_style_border_color(obj, green, LV_PART_CURSOR);
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

lv_obj_t *app_ui_create(lv_obj_t *screen)
{
    lv_obj_t *tabs = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(tabs, 48);
    lv_obj_set_size(tabs, LV_PCT(100), lv_obj_get_height(screen) - STATUS_BAR_HEIGHT);
    lv_obj_t *comm = lv_tabview_add_tab(tabs, "PTT");
    lv_obj_t *status = lv_tabview_add_tab(tabs, "Status");
    lv_obj_t *settings = lv_tabview_add_tab(tabs, "Settings");
    lv_obj_set_flex_flow(comm, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(comm, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *logo = lv_image_create(comm);
    lv_image_set_src(logo, "A:/link.png");
    lv_obj_set_size(logo, LV_PCT(100), 101);
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_CONTAIN);

    lv_obj_t *status_note = lv_label_create(status);
    lv_label_set_text(status_note, "Connectivity and speech activity are shown below.");
    lv_obj_set_width(status_note, LV_PCT(100));
    lv_obj_t *label = lv_label_create(settings);
    lv_obj_set_flex_flow(settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings, 16, 0);
    lv_label_set_text(label, "Keyboard text entry");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_t *input = lv_textarea_create(settings);
    /* Adding this field to the keyboard group must not scroll to Settings. */
    lv_obj_set_scroll_on_focus(input, false);
    lv_obj_set_width(input, LV_PCT(100));
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 128);
    lv_textarea_set_placeholder_text(input, "Type using your external keyboard");
    lv_textarea_set_text(input, "");
    sleep_button = lv_button_create(settings);
    lv_obj_set_size(sleep_button, LV_SIZE_CONTENT, 60);
    lv_obj_t *sleep_label = lv_label_create(sleep_button);
    lv_label_set_text(sleep_label, "Sleep screen for 5 seconds");
    lv_obj_center(sleep_label);
    lv_obj_add_event_cb(sleep_button, sleep_display, LV_EVENT_CLICKED, NULL);
    sleep_status = lv_label_create(settings);
    lv_label_set_text(sleep_status, "Wakes automatically and redraws the screen.");
    /* Only this field belongs to the physical keyboard's focus group.
     * LVGL does not open a virtual keyboard unless the application creates one. */
    lv_tabview_set_active(tabs, 0, LV_ANIM_OFF);
    create_ping_panel(screen);
    apply_neon_theme(screen);
    return input;
}
