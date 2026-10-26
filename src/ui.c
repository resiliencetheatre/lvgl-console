#include "ui.h"
#include "ping_status.h"
#include "ptt_status.h"
#include "settings.h"
#include "volume.h"
#include <alsa/asoundlib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "world_map_data.h"

/* Flat equirectangular outline, deliberately quieter than the foreground logo. */
#define WORLD_MAP_COLOR 0x206B12
#define WORLD_FADE_TICKS 40 /* Four-second crossfade at 100 ms updates. */
#define WORLD_WAIT_MIN_TICKS 20
#define WORLD_WAIT_MAX_TICKS 60
static int sleep_fd = -1;
/* Decorative locations, not live network activity; longitude/latitude × 10. */
static const int16_t world_locations[][2] = {
    {-980, 390}, {100, 510}, {1380, 370}, {1340, -250}, {200, 0}, {-600, -150},
    {-1060, 570}, {900, 600}, {250, -290} /* Canada, Russia, South Africa */
};
#define WORLD_LOCATION_COUNT (sizeof(world_locations) / sizeof(world_locations[0]))
struct world_animation {
    lv_obj_t *map, *tabs;
    lv_timer_t *timer;
    unsigned previous, current, next, tick, wait_ticks, pulse_tick;
};

static void world_bounds(lv_obj_t *obj, lv_area_t *bounds)
{
    lv_area_t area;
    lv_obj_get_content_coords(obj, &area);
    int32_t width = lv_area_get_width(&area) - 8;
    int32_t height = lv_area_get_height(&area) - 8;
    if (width > height * 2) width = height * 2;
    if (width < 2) width = 2;
    height = width / 2;
    bounds->x1 = area.x1 + (lv_area_get_width(&area) - width) / 2;
    bounds->y1 = area.y1 + (lv_area_get_height(&area) - height) / 2;
    bounds->x2 = bounds->x1 + width;
    bounds->y2 = bounds->y1 + height;
}

static lv_point_t world_point(struct world_animation *animation, unsigned location)
{
    lv_area_t map;
    world_bounds(animation->map, &map);
    const int16_t *point = world_locations[location];
    return (lv_point_t){map.x1 + (point[0] + 1800) * (map.x2-map.x1) / 3600,
                        map.y1 + (900-point[1]) * (map.y2-map.y1) / 1800};
}

static unsigned pick_world_location(unsigned a, unsigned b, unsigned c)
{
    unsigned choices[WORLD_LOCATION_COUNT], count = 0;
    for (unsigned i = 0; i < WORLD_LOCATION_COUNT; ++i)
        if (i != a && i != b && i != c) choices[count++] = i;
    return choices[lv_rand(0, count-1)];
}

static void invalidate_world_links(struct world_animation *animation)
{
    lv_point_t points[] = {world_point(animation, animation->previous),
                           world_point(animation, animation->current),
                           world_point(animation, animation->next)};
    for (unsigned i = 0; i < 2; ++i) {
        lv_area_t area = {
            LV_MIN(points[i].x, points[i+1].x)-6,
            LV_MIN(points[i].y, points[i+1].y)-6,
            LV_MAX(points[i].x, points[i+1].x)+6,
            LV_MAX(points[i].y, points[i+1].y)+6
        };
        lv_obj_invalidate_area(animation->map, &area);
    }
}

static void pulse_world(lv_timer_t *timer)
{
    struct world_animation *animation = lv_timer_get_user_data(timer);
    if (sleep_fd >= 0 || lv_tabview_get_tab_active(animation->tabs) != 0) return;
    invalidate_world_links(animation);
    animation->pulse_tick = (animation->pulse_tick + 1) % 40;
    if (++animation->tick >= animation->wait_ticks + WORLD_FADE_TICKS) {
        unsigned old = animation->previous;
        animation->previous = animation->current;
        animation->current = animation->next;
        animation->next = pick_world_location(old, animation->previous, animation->current);
        animation->tick = 0;
        animation->wait_ticks = lv_rand(WORLD_WAIT_MIN_TICKS, WORLD_WAIT_MAX_TICKS);
    }
    invalidate_world_links(animation);
}

static void draw_world_activity(struct world_animation *animation, lv_layer_t *layer)
{
    /* The shared endpoint stays visible as the old link fades and the next
     * link rises. A faint preview keeps all three locations legible. */
    float progress = animation->tick <= animation->wait_ticks ? 0.0f :
        (float)(animation->tick-animation->wait_ticks) / WORLD_FADE_TICKS;
    float fade = progress * progress * (3.0f - 2.0f * progress);
    float pulse = 0.5f - 0.5f * cosf(animation->pulse_tick * 6.2831853f / 40.0f);
    unsigned brightness = 180 + (unsigned)(75 * pulse);
    unsigned opacity[] = {24 + (unsigned)((brightness-24)*(1.0f-fade)),
                          brightness, 24 + (unsigned)((brightness-24)*fade)};
    lv_point_t points[] = {world_point(animation, animation->previous),
                           world_point(animation, animation->current),
                           world_point(animation, animation->next)};
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(0xFF1010);
    line.width = 1;
    for (unsigned i = 0; i < 2; ++i) {
        line.p1.x = points[i].x; line.p1.y = points[i].y;
        line.p2.x = points[i+1].x; line.p2.y = points[i+1].y;
        line.opa = opacity[i == 0 ? 0 : 2] * 3 / 4;
        lv_draw_line(layer, &line);
    }
    for (unsigned i = 0; i < 3; ++i) {
        lv_draw_rect_dsc_t dot;
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = lv_color_hex(0xFF1010);
        dot.bg_opa = opacity[i];
        dot.radius = LV_RADIUS_CIRCLE;
        lv_area_t bounds = {points[i].x-5, points[i].y-5, points[i].x+5, points[i].y+5};
        lv_draw_rect(layer, &dot, &bounds);
    }
}

static void delete_world_animation(lv_event_t *event)
{
    struct world_animation *animation = lv_event_get_user_data(event);
    if (animation->timer) lv_timer_delete(animation->timer);
    free(animation);
}

static void draw_world_map(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(event);
    struct world_animation *animation = lv_event_get_user_data(event);
    lv_area_t area;
    world_bounds(obj, &area);
    int32_t x = area.x1, y = area.y1;
    int32_t width = area.x2-area.x1, height = area.y2-area.y1;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.base.layer = layer;
    line.color = lv_color_hex(WORLD_MAP_COLOR);
    line.width = 1;
    line.opa = LV_OPA_COVER;
    for (unsigned i = 0; i < sizeof(world_segments) / sizeof(world_segments[0]); ++i) {
        const int16_t *segment = world_segments[i];
        line.p1.x = x + (segment[0] + 1800) * width / 3600;
        line.p1.y = y + (900 - segment[1]) * height / 1800;
        line.p2.x = x + (segment[2] + 1800) * width / 3600;
        line.p2.y = y + (900 - segment[3]) * height / 1800;
        if (line.p1.x != line.p2.x || line.p1.y != line.p2.y)
            lv_draw_line(layer, &line);
    }
    if (animation) draw_world_activity(animation, layer);
}

/* Single-line metrics plus a wider PTT box at 640px. */
#define STATUS_BOX_WIDTH 104
#define STATUS_BOX_HEIGHT 28
#define STATUS_BAR_HEIGHT 36
#define STATUS_ALARM_COLOR 0x401010
#define STATUS_TX_COLOR 0xF01010
#define STATUS_RX_COLOR 0x1010F0

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
    unsigned ptt_color = !strncmp(text, "TX ", 3) ? STATUS_TX_COLOR :
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
static lv_obj_t *server_input, *operation_modes[4];
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
    snprintf(next.server, sizeof(next.server), "%s", lv_textarea_get_text(server_input));
    next.screensaver = lv_obj_has_state(saver_checkbox, LV_STATE_CHECKED);
    if (!app_settings_valid(&next)) {
        lv_label_set_text(save_feedback, "Invalid settings: check IDs, five-letter preamble and server address.");
        return;
    }
    if (app_settings_save("/etc/default/udpptt", &next)) {
        lv_label_set_text_fmt(save_feedback, "Save failed: %s", strerror(errno));
        return;
    }
    saved_settings = next;
    lv_display_trigger_activity(lv_obj_get_display(save_feedback));
    lv_label_set_text(save_feedback, "Saved. Restart udpptt.service to apply IDs/server.");
}

static lv_obj_t *volume_rollers[2];
static bool volume_updating;
static void volume_event(lv_event_t *event)
{
    if (volume_updating) return;
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *roller = volume_rollers[index];
    int value = lv_roller_get_selected(roller) * 5;
    int rc = app_volume_set(index == 0, value);
    if (rc < 0) lv_label_set_text_fmt(save_feedback, "Volume failed: %s", snd_strerror(rc));
    int actual;
    if (!app_volume_get(index == 0, &actual) && actual != value) {
        volume_updating = true;
        lv_roller_set_selected(roller, (actual + 2) / 5, LV_ANIM_OFF);
        volume_updating = false;
    }
}

static void create_volume_controls(lv_obj_t *settings)
{
    lv_obj_t *row = lv_obj_create(settings);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *audio_title = lv_label_create(row);
    lv_label_set_text(audio_title, "Audio volume");
    lv_obj_set_style_text_font(audio_title, &lv_font_montserrat_20, 0);
    for (unsigned i = 0; i < 2; ++i) {
        lv_obj_t *column = lv_obj_create(row);
        lv_obj_set_size(column, 116, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(column, 0, 0);
        lv_obj_set_style_border_width(column, 0, 0);
        lv_obj_set_scrollable(column, false);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(column, 8, 0);
        lv_obj_t *caption = lv_label_create(column);
        lv_label_set_text(caption, i == 0 ? "Input" : "Output");
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_20, 0);
        int value = 0;
        bool available = app_volume_get(i == 0, &value) == 0;
        volume_rollers[i] = lv_roller_create(column);
        lv_obj_set_width(volume_rollers[i], LV_PCT(100));
        lv_obj_set_scroll_on_focus(volume_rollers[i], false);
        lv_obj_set_scroll_chain(volume_rollers[i], false);
        lv_obj_set_gesture_bubble(volume_rollers[i], false);
        lv_roller_set_options(volume_rollers[i],
            "0%\n5%\n10%\n15%\n20%\n25%\n30%\n35%\n40%\n45%\n50%\n55%\n60%\n65%\n70%\n75%\n80%\n85%\n90%\n95%\n100%",
            LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(volume_rollers[i], 3);
        lv_roller_set_selected(volume_rollers[i], (value + 2) / 5, LV_ANIM_OFF);
        if (!available) {
            lv_label_set_text(caption, i == 0 ? "Input: N/A" : "Output: N/A");
            lv_obj_add_state(volume_rollers[i], LV_STATE_DISABLED);
        }
        lv_obj_add_event_cb(volume_rollers[i], volume_event, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    }
}

static void select_operation_mode(lv_event_t *event)
{
    lv_obj_t *selected = lv_event_get_target_obj(event);
    for (unsigned i = 0; i < 4; ++i) {
        if (operation_modes[i] == selected) lv_obj_add_state(operation_modes[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(operation_modes[i], LV_STATE_CHECKED);
    }
    /* UI-only choice: intentionally no client commands or configuration writes. */
}

static void create_operation_modes(lv_obj_t *settings)
{
    lv_obj_t *title = lv_label_create(settings);
    lv_label_set_text(title, "PTT Operation mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    const char *names[] = {"WAN", "DARK FIBER", "DATA DIODE (TX)", "DATA DIODE (RX)"};
    for (unsigned row_index = 0; row_index < 2; ++row_index) {
        lv_obj_t *row = lv_obj_create(settings);
        lv_obj_set_size(row, LV_PCT(100), 56);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        for (unsigned col = 0; col < 2; ++col) {
            unsigned i = row_index * 2 + col;
            lv_obj_t *cell = lv_obj_create(row);
            lv_obj_set_size(cell, LV_PCT(49), LV_PCT(100));
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_scrollable(cell, false);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            operation_modes[i] = lv_checkbox_create(cell);
            lv_checkbox_set_text(operation_modes[i], names[i]);
            lv_obj_set_scroll_on_focus(operation_modes[i], false);
            lv_obj_set_style_radius(operation_modes[i], LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
            lv_obj_add_event_cb(operation_modes[i], select_operation_mode, LV_EVENT_VALUE_CHANGED, NULL);
            if (i == 0) {
                lv_obj_add_state(operation_modes[i], LV_STATE_CHECKED);
                server_input = lv_textarea_create(cell);
                lv_obj_set_size(server_input, 185, 44);
                lv_obj_set_scroll_on_focus(server_input, false);
                lv_textarea_set_one_line(server_input, true);
                lv_textarea_set_max_length(server_input, 255);
                lv_textarea_set_placeholder_text(server_input, "Server IP");
                lv_textarea_set_text(server_input, saved_settings.server);
            }
        }
    }
}

void app_ui_add_settings_to_group(lv_group_t *group)
{
    for (unsigned i = 0; i < 3; ++i) lv_group_add_obj(group, settings_fields[i]);
    for (unsigned i = 0; i < 2; ++i) lv_group_add_obj(group, volume_rollers[i]);
    for (unsigned i = 0; i < 4; ++i) lv_group_add_obj(group, operation_modes[i]);
    lv_group_add_obj(group, server_input);
    lv_group_add_obj(group, saver_checkbox);
    lv_group_add_obj(group, save_button);
}

/* Also called before LVGL shuts down, so a normal exit cannot leave HDMI off. */
void app_ui_resume_display(void)
{
    if (sleep_fd < 0) return;
    int result = ioctl(sleep_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    if (result < 0) perror("Display unblank");
    close(sleep_fd);
    sleep_fd = -1;
    if (result < 0) lv_label_set_text(save_feedback, "Display wake failed; see log.");
    lv_display_t *display = lv_obj_get_display(save_feedback);
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_obj_invalidate(lv_display_get_layer_top(display));
    lv_obj_invalidate(lv_display_get_layer_sys(display));
    lv_refr_now(display);
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
    if (lv_obj_check_type(obj, &lv_roller_class)) {
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(DIM_GREEN), LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(obj, green, LV_PART_SELECTED);
        lv_obj_set_style_border_color(obj, green, LV_PART_SELECTED);
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
    /* DRAW_MAIN runs behind child widgets, including the centered Link PNG. */
    struct world_animation *animation = calloc(1, sizeof(*animation));
    if (animation) {
        animation->map = comm;
        animation->tabs = tabs;
        animation->previous = lv_rand(0, WORLD_LOCATION_COUNT-1);
        animation->current = pick_world_location(animation->previous, WORLD_LOCATION_COUNT, WORLD_LOCATION_COUNT);
        animation->next = pick_world_location(animation->previous, animation->current, WORLD_LOCATION_COUNT);
        animation->wait_ticks = lv_rand(WORLD_WAIT_MIN_TICKS, WORLD_WAIT_MAX_TICKS);
        animation->timer = lv_timer_create(pulse_world, 100, animation);
        lv_obj_add_event_cb(comm, delete_world_animation, LV_EVENT_DELETE, animation);
    }
    lv_obj_add_event_cb(comm, draw_world_map, LV_EVENT_DRAW_MAIN, animation);
    lv_obj_t *status = lv_tabview_add_tab(tabs, "Messaging");
    lv_obj_t *settings = lv_tabview_add_tab(tabs, "Settings");
    lv_obj_t *logo = lv_image_create(comm);
    lv_image_set_src(logo, "A:/link.png");
    lv_obj_set_size(logo, LV_PCT(100), 101);
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_align(logo, LV_ALIGN_BOTTOM_MID, 0, -8);

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
    lv_obj_set_scrollable(settings, true);
    lv_obj_set_scroll_dir(settings, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_chain(settings, false);
    lv_obj_set_style_pad_row(settings, 10, 0);
    int loaded = app_settings_load("/etc/default/udpptt", &saved_settings);
    const char *field_labels[] = {"Transmit ID", "dpi ID", "preamble ID"};
    const char *values[] = {saved_settings.txid, saved_settings.dpi_id, saved_settings.preamble};
    lv_obj_t *fields_row = lv_obj_create(settings);
    lv_obj_set_size(fields_row, LV_PCT(100), 112);
    lv_obj_set_style_pad_all(fields_row, 0, 0);
    lv_obj_set_style_border_width(fields_row, 0, 0);
    lv_obj_set_scrollable(fields_row, false);
    lv_obj_set_flex_flow(fields_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fields_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                         LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *column = lv_obj_create(fields_row);
        lv_obj_set_size(column, 168, LV_PCT(100));
        lv_obj_set_style_pad_all(column, 0, 0);
        lv_obj_set_style_pad_row(column, 8, 0);
        lv_obj_set_style_border_width(column, 0, 0);
        lv_obj_set_scrollable(column, false);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        lv_obj_t *caption = lv_label_create(column);
        lv_label_set_text(caption, field_labels[i]);
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_20, 0);
        settings_fields[i] = lv_textarea_create(column);
        lv_obj_set_size(settings_fields[i], LV_PCT(100), 44);
        lv_obj_set_scroll_on_focus(settings_fields[i], false);
        lv_textarea_set_one_line(settings_fields[i], true);
        lv_textarea_set_max_length(settings_fields[i], i == 2 ? 5 : i == 0 ? 31 : 63);
        lv_textarea_set_accepted_chars(settings_fields[i], i == 2 ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ" :
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-");
        lv_textarea_set_text(settings_fields[i], values[i]);
        if (i == 2) {
            lv_obj_t *hint = lv_label_create(column);
            lv_label_set_text(hint, "(5 letters only)");
        }
    }
    create_volume_controls(settings);
    create_operation_modes(settings);
    lv_obj_t *system_title = lv_label_create(settings);
    lv_label_set_text(system_title, "System settings");
    lv_obj_set_style_text_font(system_title, &lv_font_montserrat_20, 0);
    saver_checkbox = lv_checkbox_create(settings);
    lv_checkbox_set_text(saver_checkbox, "Screen saver");
    if (saved_settings.screensaver) lv_obj_add_state(saver_checkbox, LV_STATE_CHECKED);
    lv_obj_set_scroll_on_focus(saver_checkbox, false);
    lv_obj_t *actions = lv_obj_create(settings);
    lv_obj_set_size(actions, LV_PCT(100), 48);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_scrollable(actions, false);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    save_button = lv_button_create(actions);
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
    /* No virtual keyboard is created. */
    lv_tabview_set_active(tabs, 0, LV_ANIM_OFF);
    create_ping_panel(screen);
    apply_neon_theme(screen);
    return input;
}
