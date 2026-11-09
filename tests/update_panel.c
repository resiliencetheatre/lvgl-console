#include "update_panel.h"
#include "update_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct AppUpdates { int unused; };
static struct AppUpdates client;
static AppUpdateState state;
static int calls[4],stops;
AppUpdates *app_updates_start(const char *path){ assert(path);return &client; }
void app_updates_stop(AppUpdates *u){ assert(u==&client);++stops; }
void app_updates_snapshot(AppUpdates *u,AppUpdateState *s){ assert(u==&client);*s=state; }
bool app_updates_request(AppUpdates *u,AppUpdateAction action,const char *offer)
{
    assert(u==&client);
    if(action!=APP_UPDATE_CHECK && action!=APP_UPDATE_FINISH && strcmp(offer,state.offer_id))return false;
    ++calls[action];state.can_check=state.can_update=false;state.busy=true;return true;
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels)
{ (void)area;(void)pixels;lv_display_flush_ready(display); }
static lv_obj_t *find(lv_obj_t *root,const char *caption)
{
    if(lv_obj_check_type(root,&lv_button_class) && lv_obj_get_child_count(root) &&
       lv_obj_check_type(lv_obj_get_child(root,0),&lv_label_class) &&
       !strcmp(lv_label_get_text(lv_obj_get_child(root,0)),caption))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);++i){lv_obj_t *r=find(lv_obj_get_child(root,i),caption);if(r)return r;}
    return NULL;
}
static void visible_button(const char *caption)
{
    lv_obj_update_layout(lv_layer_top());
    lv_obj_t *button=find(lv_layer_top(),caption); assert(button);
    const lv_style_selector_t states[]={LV_STATE_DEFAULT,LV_STATE_FOCUSED,LV_STATE_PRESSED};
    for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i) {
        lv_obj_remove_state(button,LV_STATE_FOCUSED|LV_STATE_PRESSED);
        lv_obj_add_state(button,states[i]);
        assert(lv_color_eq(lv_obj_get_style_text_color(button,0),lv_color_hex(0x39FF14)));
        assert(lv_color_eq(lv_obj_get_style_bg_color(button,0),
                          lv_color_hex(states[i]==LV_STATE_PRESSED ? 0x103D08 : 0x000000)));
    }
    lv_obj_remove_state(button,LV_STATE_FOCUSED|LV_STATE_PRESSED);
    lv_area_t area;lv_obj_get_coords(button,&area);
    assert(area.y1>=0 && area.y2<lv_display_get_vertical_resolution(NULL));
    for(lv_obj_t *parent=lv_obj_get_parent(button);parent;parent=lv_obj_get_parent(parent)) {
        lv_area_t content;lv_obj_get_content_coords(parent,&content);
        assert(area.x1>=content.x1 && area.x2<=content.x2);
        assert(area.y1>=content.y1 && area.y2<=content.y2);
    }
}
static void tick(void){ lv_tick_inc(250);lv_timer_handler(); }
static void click(lv_obj_t *object){assert(object);lv_obj_send_event(object,LV_EVENT_CLICKED,NULL);}
int main(void)
{
    lv_init();lv_display_t *display=lv_display_create(640,480);
    static uint8_t buffer[640*48*2];lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    lv_obj_t *panel=app_update_panel_create(lv_screen_active());assert(panel);
    lv_group_t *group=lv_group_create();app_update_panel_add_to_group(panel,group);
    lv_obj_t *check=find(panel,"Check for updates"),*update=find(panel,"Update");
    tick();assert(lv_obj_has_state(check,LV_STATE_DISABLED) && lv_obj_has_state(update,LV_STATE_DISABLED));
    state.connected=state.can_check=true;state.percent=-1;strcpy(state.version,"dev-11");tick();
    click(check);assert(calls[APP_UPDATE_CHECK]==1 && !calls[APP_UPDATE_DOWNLOAD] && !calls[APP_UPDATE_INSTALL]);
    state.can_update=state.can_check=true;state.busy=false;strcpy(state.offer_version,"dev-12");
    strcpy(state.offer_id,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    strcpy(state.notes,"Review this release before proceeding.");state.offer_size=805309300;tick();
    memset(state.notes,'x',sizeof(state.notes)-1);state.notes[sizeof(state.notes)-1]=0;
    click(update);assert(!calls[APP_UPDATE_DOWNLOAD]);
    visible_button("Cancel");visible_button("Download");
    click(find(lv_layer_top(),"Cancel"));
    assert(!calls[APP_UPDATE_DOWNLOAD]);
    click(update);state.offer_id[0]='a';click(find(lv_layer_top(),"Download"));assert(!calls[APP_UPDATE_DOWNLOAD]);
    tick();click(update);click(find(lv_layer_top(),"Download"));assert(calls[APP_UPDATE_DOWNLOAD]==1 && !calls[APP_UPDATE_INSTALL]);
    state.can_update=state.can_check=true;state.busy=false;state.download_ready=true;tick();
    assert(find(panel,"Install update"));click(update);assert(!calls[APP_UPDATE_INSTALL]);
    visible_button("Cancel");visible_button("Install and restart");
    click(find(lv_layer_top(),"Install and restart"));assert(calls[APP_UPDATE_INSTALL]==1);tick();
    assert(lv_obj_has_state(check,LV_STATE_DISABLED) && lv_obj_has_state(update,LV_STATE_DISABLED));
    state.busy=false;state.can_finish=true;state.can_update=state.can_check=false;tick();
    assert(find(panel,"Finish update"));click(update);
    visible_button("Later");visible_button("Restart");
    click(find(lv_layer_top(),"Later"));assert(!calls[APP_UPDATE_FINISH]);
    click(update);click(find(lv_layer_top(),"Restart"));assert(calls[APP_UPDATE_FINISH]==1);
    lv_obj_delete(panel);assert(stops==1);lv_group_delete(group);lv_deinit();
    puts("Update panel availability, pinned consent, separate install and cleanup: OK");return 0;
}
