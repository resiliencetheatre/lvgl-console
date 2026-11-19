#include "wifi_panel.h"
#include "wifi_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct panel {
    AppWifi *client;
    lv_obj_t *root, *list, *password, *status, *buttons[4];
    lv_timer_t *timer;
    AppWifiState shown;
    char selected[WIFI_PATH_SIZE];
};
static void enable(lv_obj_t *o, bool value) {
    if (value) lv_obj_remove_state(o,LV_STATE_DISABLED); else lv_obj_add_state(o,LV_STATE_DISABLED);
}
static void selection(lv_event_t *event)
{
    struct panel *p=lv_event_get_user_data(event);
    unsigned i=lv_dropdown_get_selected(p->list);
    snprintf(p->selected,sizeof(p->selected),"%s",i<p->shown.count ? p->shown.networks[i].path : "");
    lv_textarea_set_text(p->password,"");
}
static void refresh(lv_timer_t *timer)
{
    struct panel *p=lv_timer_get_user_data(timer); AppWifiState s;
    app_wifi_snapshot(p->client,&s);
    char status[400];
    snprintf(status,sizeof(status),"%s%s%s%s",s.message,*s.station_state ? "\nWifi state: " : "",
             s.station_state,s.scanning ? " (scanning)" : "");
    if (strcmp(lv_label_get_text(p->status),status)) lv_label_set_text(p->status,status);
    if (!lv_dropdown_is_open(p->list) && (s.count!=p->shown.count || memcmp(s.networks,p->shown.networks,sizeof(s.networks)))) {
        char options[WIFI_MAX_NETWORKS*200]; size_t used=0; unsigned selected=0;
        for (unsigned i=0;i<s.count;i++) {
            char name[128]; snprintf(name,sizeof(name),"%s",s.networks[i].name);
            for (char *c=name;*c;c++) if (*c=='\n' || *c=='\r') *c=' ';
            int n=snprintf(options+used,sizeof(options)-used,"%s%s (%s, %d dBm)%s%s",i ? "\n" : "",name,
                s.networks[i].security,s.networks[i].signal,s.networks[i].connected ? " connected" : "",
                *s.networks[i].known ? " saved" : "");
            if (n<0 || (size_t)n>=sizeof(options)-used) break;
            used+=(size_t)n;
            if (!strcmp(p->selected,s.networks[i].path)) selected=i;
        }
        if (!s.count) snprintf(options,sizeof(options),"No networks; select Scan");
        lv_dropdown_set_options(p->list,options); lv_dropdown_set_selected(p->list,selected);
        const char *path=s.count ? s.networks[selected].path : "";
        if (strcmp(path,p->selected)) lv_textarea_set_text(p->password,"");
        snprintf(p->selected,sizeof(p->selected),"%s",path);
        p->shown=s;
    }
    bool connected=false, known=false, supported=false;
    for (unsigned i=0;i<p->shown.count;i++) {
        connected|=p->shown.networks[i].connected;
        if (!strcmp(p->selected,p->shown.networks[i].path)) {
            known=*p->shown.networks[i].known;
            supported=!strcmp(p->shown.networks[i].security,"open") || !strcmp(p->shown.networks[i].security,"psk");
        }
    }
    bool ready=s.available && !s.busy;
    enable(p->buttons[0],ready); enable(p->buttons[1],ready && supported && *p->selected);
    enable(p->buttons[2],ready && connected); enable(p->buttons[3],ready && known);
    enable(p->list,ready && s.count); enable(p->password,ready);
}
static void action(lv_event_t *event)
{
    struct panel *p=lv_event_get_user_data(event); lv_obj_t *target=lv_event_get_target_obj(event);
    for (unsigned i=0;i<4;i++) if (p->buttons[i]==target) {
        if (app_wifi_request(p->client,(AppWifiAction)i,p->selected,lv_textarea_get_text(p->password))) {
            lv_textarea_set_text(p->password,""); lv_timer_ready(p->timer);
        }
    }
}
static void destroy(lv_event_t *event)
{
    struct panel *p=lv_event_get_user_data(event);
    lv_timer_delete(p->timer); app_wifi_stop(p->client); free(p);
}
lv_obj_t *app_wifi_panel_create(lv_obj_t *parent)
{
    struct panel *p=calloc(1,sizeof(*p)); if (!p) return NULL;
    p->root=lv_obj_create(parent); lv_obj_set_user_data(p->root,p);
    lv_obj_set_size(p->root,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(p->root,0,0); lv_obj_set_style_pad_all(p->root,0,0);
    lv_obj_set_scrollable(p->root,false); lv_obj_set_flex_flow(p->root,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p->root,8,0);
    lv_obj_t *title=lv_label_create(p->root); lv_label_set_text(title,"Wifi");
    lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
    p->status=lv_label_create(p->root); lv_obj_set_width(p->status,LV_PCT(100));
    lv_label_set_text(p->status,"Connecting to Wifi service...");
    p->list=lv_dropdown_create(p->root); lv_obj_set_width(p->list,LV_PCT(100));
    lv_dropdown_set_options(p->list,"No networks; select Scan");
    lv_obj_add_event_cb(p->list,selection,LV_EVENT_VALUE_CHANGED,p);
    p->password=lv_textarea_create(p->root); lv_obj_set_size(p->password,LV_PCT(100),44);
    lv_textarea_set_one_line(p->password,true); lv_textarea_set_password_mode(p->password,false);
    lv_textarea_set_max_length(p->password,64); lv_textarea_set_placeholder_text(p->password,"PSK / password (visible)");
    const char *names[]={"Scan","Connect","Disconnect","Forget network"};
    for (unsigned row=0;row<2;row++) {
        lv_obj_t *box=lv_obj_create(p->root); lv_obj_set_size(box,LV_PCT(100),44);
        lv_obj_set_style_border_width(box,0,0); lv_obj_set_style_pad_all(box,0,0);
        lv_obj_set_scrollable(box,false); lv_obj_set_flex_flow(box,LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(box,12,0);
        for (unsigned j=0;j<2;j++) {
            unsigned i=row*2+j; p->buttons[i]=lv_button_create(box); lv_obj_set_size(p->buttons[i],0,44);
            lv_obj_set_flex_grow(p->buttons[i],1);
            lv_obj_t *label=lv_label_create(p->buttons[i]); lv_label_set_text(label,names[i]); lv_obj_center(label);
            lv_obj_add_event_cb(p->buttons[i],action,LV_EVENT_CLICKED,p); enable(p->buttons[i],false);
        }
    }
    p->client=app_wifi_start(); p->timer=lv_timer_create(refresh,250,p);
    lv_obj_add_event_cb(p->root,destroy,LV_EVENT_DELETE,p);
    return p->root;
}
void app_wifi_panel_add_to_group(lv_obj_t *root, lv_group_t *group)
{
    if (!root) return;
    struct panel *p=lv_obj_get_user_data(root);
    lv_group_add_obj(group,p->list); lv_group_add_obj(group,p->password);
    for (unsigned i=0;i<4;i++) lv_group_add_obj(group,p->buttons[i]);
}
