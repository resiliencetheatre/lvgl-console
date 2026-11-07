#include "update_panel.h"
#include "update_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct panel {
    AppUpdates *client;
    lv_obj_t *root, *check, *update, *update_caption, *version, *message, *detail, *bar, *dialog;
    lv_timer_t *timer;
    lv_group_t *group;
    char accepted_offer[65];
    AppUpdateAction accepted_action;
};
static void enabled(lv_obj_t *object, bool enable)
{
    if (enable) lv_obj_remove_state(object,LV_STATE_DISABLED);
    else lv_obj_add_state(object,LV_STATE_DISABLED);
}
static void label(lv_obj_t *object, const char *value)
{
    if (strcmp(lv_label_get_text(object),value)) lv_label_set_text(object,value);
}
static void refresh(lv_timer_t *timer)
{
    struct panel *p=lv_timer_get_user_data(timer); AppUpdateState state;
    app_updates_snapshot(p->client,&state);
    char versions[240];
    if (*state.offer_version) snprintf(versions,sizeof(versions),"Installed: %s   Available: %s (%.1f MiB)",
                                     *state.version ? state.version : "unknown",state.offer_version,(double)state.offer_size/1048576.0);
    else snprintf(versions,sizeof(versions),"Installed: %s",*state.version ? state.version : "unavailable");
    label(p->version,versions); label(p->message,state.message); label(p->detail,state.detail);
    label(p->update_caption,state.can_finish ? "Finish update" : state.download_ready ? "Install update" : "Update");
    enabled(p->check,state.can_check && !p->dialog);
    enabled(p->update,(state.can_update || state.can_finish) && !p->dialog);
    if (state.percent>=0) {
        lv_obj_set_hidden(p->bar,false);
        lv_bar_set_value(p->bar,state.percent,LV_ANIM_OFF);
    } else lv_obj_set_hidden(p->bar,true);
}
static void check(lv_event_t *event)
{
    struct panel *p=lv_event_get_user_data(event);
    if (!p->dialog) app_updates_request(p->client,APP_UPDATE_CHECK,NULL);
    lv_timer_ready(p->timer);
}
static void close_dialog(struct panel *p)
{
    if (p->dialog) { lv_obj_t *dialog=p->dialog; p->dialog=NULL; lv_obj_delete(dialog); }
    if (p->group) lv_group_focus_obj(p->update);
    lv_timer_ready(p->timer);
}
static void cancel(lv_event_t *event) { close_dialog(lv_event_get_user_data(event)); }
static void accept(lv_event_t *event)
{
    struct panel *p=lv_event_get_user_data(event);
    if (!app_updates_request(p->client,p->accepted_action,p->accepted_offer))
        lv_label_set_text(p->message,"Release or device state changed. Check again before updating.");
    close_dialog(p);
}
static lv_obj_t *button(lv_obj_t *parent, const char *name, lv_event_cb_t callback, struct panel *p)
{
    lv_obj_t *object=lv_button_create(parent); lv_obj_set_size(object,LV_PCT(48),44);
    lv_obj_set_scroll_on_focus(object,false);
    lv_obj_t *caption=lv_label_create(object); lv_label_set_text(caption,name); lv_obj_center(caption);
    lv_obj_add_event_cb(object,callback,LV_EVENT_CLICKED,p);
    return object;
}
static void confirm(lv_event_t *event)
{
    struct panel *p=lv_event_get_user_data(event); AppUpdateState state;
    app_updates_snapshot(p->client,&state);
    if ((!state.can_update && !state.can_finish) || p->dialog) return;
    snprintf(p->accepted_offer,sizeof(p->accepted_offer),"%s",state.offer_id);
    p->accepted_action=state.can_finish ? APP_UPDATE_FINISH : state.download_ready ? APP_UPDATE_INSTALL : APP_UPDATE_DOWNLOAD;
    p->dialog=lv_obj_create(lv_layer_top());
    lv_obj_set_size(p->dialog,LV_PCT(100),LV_PCT(100)); lv_obj_center(p->dialog);
    lv_obj_set_style_bg_color(p->dialog,lv_color_black(),0);
    lv_obj_set_style_bg_opa(p->dialog,LV_OPA_80,0);
    lv_obj_set_style_border_width(p->dialog,0,0);
    lv_obj_set_scrollable(p->dialog,false);
    lv_obj_t *box=lv_obj_create(p->dialog);
    lv_obj_set_size(box,LV_PCT(94),LV_PCT(90)); lv_obj_center(box);
    lv_obj_set_flex_flow(box,LV_FLEX_FLOW_COLUMN); lv_obj_set_scrollable(box,false);
    lv_obj_set_style_pad_all(box,12,0); lv_obj_set_style_border_width(box,0,0);
    lv_obj_set_style_bg_color(box,lv_color_hex(0x081A05),0);
    lv_obj_set_style_text_color(box,lv_color_hex(0x39FF14),0);
    lv_obj_t *title=lv_label_create(box); lv_obj_set_width(title,LV_PCT(100));
    lv_label_set_text_fmt(title,"%s %s?",state.can_finish ? "Finish update" : state.download_ready ? "Install" : "Download",state.offer_version);
    lv_obj_t *body=lv_obj_create(box);
    lv_obj_set_width(body,LV_PCT(100)); lv_obj_set_height(body,0);
    lv_obj_set_flex_grow(body,1); lv_obj_set_scroll_dir(body,LV_DIR_VER);
    lv_obj_set_style_pad_all(body,0,0); lv_obj_set_style_border_width(body,0,0);
    lv_obj_set_style_bg_opa(body,LV_OPA_TRANSP,0);
    lv_obj_t *description=lv_label_create(body); lv_obj_set_width(description,LV_PCT(100));
    lv_label_set_text_fmt(description,"%.1f MiB\n%s\n\n%s",(double)state.offer_size/1048576.0,state.notes,
        state.can_finish ? "Update confirmed. Restart normally to finish. Keep power connected." :
        state.download_ready ? "The device will restart after installation. Keep power connected." :
        "Download this signed release now. Installation will require a separate confirmation.");
    lv_label_set_recolor(description,false);
    lv_obj_t *row=lv_obj_create(box); lv_obj_set_size(row,LV_PCT(100),60);
    lv_obj_set_style_pad_all(row,4,0); lv_obj_set_style_border_width(row,0,LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW); lv_obj_set_scrollable(row,false);
    lv_obj_t *no=button(row,state.can_finish ? "Later" : "Cancel",cancel,p);
    lv_obj_t *yes=button(row,state.can_finish ? "Restart" : state.download_ready ? "Install and restart" : "Download",accept,p);
    if (p->group) { lv_group_add_obj(p->group,no); lv_group_add_obj(p->group,yes); lv_group_focus_obj(no); }
    lv_timer_ready(p->timer);
}
static void destroy(lv_event_t *event)
{
    struct panel *p=lv_event_get_user_data(event);
    if (p->timer) lv_timer_delete(p->timer);
    if (p->dialog) lv_obj_delete(p->dialog);
    app_updates_stop(p->client); free(p);
}
lv_obj_t *app_update_panel_create(lv_obj_t *parent)
{
    struct panel *p=calloc(1,sizeof(*p)); if (!p) return NULL;
    p->root=lv_obj_create(parent); lv_obj_set_user_data(p->root,p);
    lv_obj_set_size(p->root,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(p->root,0,LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(p->root,LV_FLEX_FLOW_COLUMN); lv_obj_set_scrollable(p->root,false);
    lv_obj_t *title=lv_label_create(p->root); lv_label_set_text(title,"System update");
    lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
    p->version=lv_label_create(p->root); lv_obj_set_width(p->version,LV_PCT(100));
    p->message=lv_label_create(p->root); lv_obj_set_width(p->message,LV_PCT(100));
    p->detail=lv_label_create(p->root); lv_obj_set_width(p->detail,LV_PCT(100));
    p->bar=lv_bar_create(p->root); lv_obj_set_size(p->bar,LV_PCT(100),12);
    lv_bar_set_range(p->bar,0,100); lv_obj_set_hidden(p->bar,true);
    lv_obj_t *row=lv_obj_create(p->root); lv_obj_set_size(row,LV_PCT(100),60);
    lv_obj_set_style_pad_all(row,4,0); lv_obj_set_style_border_width(row,0,LV_STATE_DEFAULT); lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(row,false);
    p->check=button(row,"Check for updates",check,p);
    p->update=button(row,"Update",confirm,p); p->update_caption=lv_obj_get_child(p->update,0);
    const char *path=getenv("RETERMINAL_UPDATE_SOCKET");
    p->client=app_updates_start(path ? path : "/run/reterminal-update-api/control.sock");
    p->timer=lv_timer_create(refresh,200,p);
    if (p->timer) lv_timer_ready(p->timer);
    enabled(p->check,false); enabled(p->update,false);
    lv_label_set_text(p->message,p->client ? "Connecting to update service..." : "Update service unavailable.");
    lv_obj_add_event_cb(p->root,destroy,LV_EVENT_DELETE,p);
    return p->root;
}
void app_update_panel_add_to_group(lv_obj_t *panel, lv_group_t *group)
{
    if (!panel) return;
    struct panel *p=lv_obj_get_user_data(panel); p->group=group;
    lv_group_add_obj(group,p->check); lv_group_add_obj(group,p->update);
}
