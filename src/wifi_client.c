#define _POSIX_C_SOURCE 200809L
#include "wifi_client.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef APP_HAVE_SYSTEMD
#include <systemd/sd-bus.h>
#endif
struct AppWifi {
    pthread_t thread;
    pthread_mutex_t lock;
    bool stop, queued;
    AppWifiState state;
    AppWifiAction action;
    char path[WIFI_PATH_SIZE], password[65];
#ifdef APP_HAVE_SYSTEMD
    sd_bus *bus;
    sd_bus_slot *agent, *pending;
    char station[WIFI_PATH_SIZE], connecting[WIFI_PATH_SIZE], secret[65], owner[256];
    bool registered;
    time_t deadline;
#endif
};
static void copy(char *out, size_t size, const char *in) { snprintf(out,size,"%s",in ? in : ""); }
static void wipe(char *s, size_t n) { volatile char *p=s; while (n--) *p++=0; }
static void message(AppWifi *w, const char *s) {
    pthread_mutex_lock(&w->lock); copy(w->state.message,sizeof(w->state.message),s); pthread_mutex_unlock(&w->lock);
}
#ifdef APP_HAVE_SYSTEMD
#define IWD "net.connman.iwd"
#define STATION IWD ".Station"
#define NETWORK IWD ".Network"
#define AGENT_PATH "/org/lvgl/console/wifi_agent"
static time_t monotonic_seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec; }
static int credential(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    AppWifi *w=userdata; const char *path;
    (void)error;
    if (!sd_bus_message_get_sender(m) || strcmp(sd_bus_message_get_sender(m),w->owner))
        return sd_bus_reply_method_errorf(m,SD_BUS_ERROR_ACCESS_DENIED,"Unexpected credential requester");
    if (sd_bus_message_read(m,"o",&path)<0 || !w->pending || strcmp(path,w->connecting) || !*w->secret)
        return sd_bus_reply_method_errorf(m,IWD ".Canceled","No credentials supplied for this network");
    return sd_bus_reply_method_return(m,"s",w->secret);
}
static int canceled(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    AppWifi *w=userdata; (void)error;
    if (!sd_bus_message_get_sender(m) || strcmp(sd_bus_message_get_sender(m),w->owner))
        return sd_bus_reply_method_errorf(m,SD_BUS_ERROR_ACCESS_DENIED,"Unexpected agent caller");
    wipe(w->secret,sizeof(w->secret));
    return sd_bus_reply_method_return(m,"");
}
static const sd_bus_vtable agent_vtable[]={
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RequestPassphrase","o","s",credential,SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cancel","s","",canceled,SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Release","","",canceled,SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};
static int completed(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    AppWifi *w=userdata; (void)error;
    const sd_bus_error *e=sd_bus_message_get_error(m);
    message(w,e ? e->message : "Connected to Wifi; see Network status for IP address.");
    w->pending=sd_bus_slot_unref(w->pending); wipe(w->secret,sizeof(w->secret));
    return 1;
}
/* Read the ObjectManager snapshot. Object paths, rather than SSID strings,
 * identify selections, including equal names with different security types. */
static int discover(AppWifi *w, AppWifiState *s)
{
    sd_bus_message *m=NULL; sd_bus_error error=SD_BUS_ERROR_NULL;
    int r=sd_bus_call_method(w->bus,IWD,"/","org.freedesktop.DBus.ObjectManager","GetManagedObjects",&error,&m,"");
    if (r<0) goto done;
    sd_bus_creds *creds=NULL; const char *owner=NULL;
    r=sd_bus_get_name_creds(w->bus,IWD,SD_BUS_CREDS_UNIQUE_NAME,&creds);
    if (r>=0) r=sd_bus_creds_get_unique_name(creds,&owner);
    if (r<0) { sd_bus_creds_unref(creds); goto done; }
    if (strcmp(w->owner,owner)) {
        w->registered=false; w->pending=sd_bus_slot_unref(w->pending);
        wipe(w->secret,sizeof(w->secret)); copy(w->owner,sizeof(w->owner),owner);
    }
    sd_bus_creds_unref(creds);
    *w->station=0;
    if ((r=sd_bus_message_enter_container(m,'a',"{oa{sa{sv}}}"))<0) goto done;
    while ((r=sd_bus_message_enter_container(m,'e',"oa{sa{sv}}"))>0) {
        const char *path; if ((r=sd_bus_message_read(m,"o",&path))<0) goto done;
        if ((r=sd_bus_message_enter_container(m,'a',"{sa{sv}}"))<0) goto done;
        while ((r=sd_bus_message_enter_container(m,'e',"sa{sv}"))>0) {
            const char *iface; if ((r=sd_bus_message_read(m,"s",&iface))<0) goto done;
            bool station=!strcmp(iface,STATION), network=!strcmp(iface,NETWORK);
            AppWifiNetwork n={0};
            copy(n.path,sizeof(n.path),path);
            if ((r=sd_bus_message_enter_container(m,'a',"{sv}"))<0) goto done;
            while ((r=sd_bus_message_enter_container(m,'e',"sv"))>0) {
                const char *key,*type; if ((r=sd_bus_message_read(m,"s",&key))<0) goto done;
                if ((r=sd_bus_message_peek_type(m,NULL,&type))<0) goto done;
                if ((r=sd_bus_message_enter_container(m,'v',type))<0) goto done;
                if (station && !strcmp(key,"Scanning") && !strcmp(type,"b")) {
                    int v; if ((r=sd_bus_message_read(m,"b",&v))<0) goto done; s->scanning=v;
                } else if (station && !strcmp(key,"State") && !strcmp(type,"s")) {
                    const char *v; if ((r=sd_bus_message_read(m,"s",&v))<0) goto done;
                    copy(s->station_state,sizeof(s->station_state),v);
                } else if (network && (!strcmp(type,"s") || !strcmp(type,"o"))) {
                    const char *v; if ((r=sd_bus_message_read(m,type,&v))<0) goto done;
                    if (!strcmp(key,"Name")) copy(n.name,sizeof(n.name),v);
                    if (!strcmp(key,"Type")) copy(n.security,sizeof(n.security),v);
                    if (!strcmp(key,"KnownNetwork")) copy(n.known,sizeof(n.known),v);
                } else if (network && !strcmp(key,"Connected") && !strcmp(type,"b")) {
                    int v; if ((r=sd_bus_message_read(m,"b",&v))<0) goto done; n.connected=v;
                } else if ((r=sd_bus_message_skip(m,type))<0) goto done;
                if ((r=sd_bus_message_exit_container(m))<0 || (r=sd_bus_message_exit_container(m))<0) goto done;
            }
            if (r<0) goto done;
            if (station) {
                if (*w->station && strcmp(w->station,path)) { r=-1; copy(s->message,sizeof(s->message),"Multiple Wifi stations: select adapter configuration first."); goto done; }
                copy(w->station,sizeof(w->station),path);
            }
            if (network && s->count<WIFI_MAX_NETWORKS) s->networks[s->count++]=n;
            if ((r=sd_bus_message_exit_container(m))<0 || (r=sd_bus_message_exit_container(m))<0) goto done;
        }
        if (r<0) goto done;
        if ((r=sd_bus_message_exit_container(m))<0 || (r=sd_bus_message_exit_container(m))<0) goto done;
    }
    if (r<0) goto done;
    s->available=*w->station!=0;
    if (!s->available) { s->count=0; copy(s->message,sizeof(s->message),"No Wifi station available (check adapter/radio)."); }
    r=0;
done:
    if (r<0 && !*s->message) copy(s->message,sizeof(s->message),error.message ? error.message : "Wifi service unavailable.");
    sd_bus_error_free(&error); sd_bus_message_unref(m); return r;
}
static void ordered(AppWifi *w, AppWifiState *s)
{
    sd_bus_message *m=NULL;
    if (!s->available || sd_bus_call_method(w->bus,IWD,w->station,STATION,"GetOrderedNetworks",NULL,&m,"")<0) return;
    AppWifiNetwork list[WIFI_MAX_NETWORKS]; unsigned count=0;
    if (sd_bus_message_enter_container(m,'a',"(on)")>=0) {
        while (sd_bus_message_enter_container(m,'r',"on")>0) {
            const char *path; int16_t signal;
            if (sd_bus_message_read(m,"on",&path,&signal)<0) break;
            for (unsigned i=0;i<s->count;i++) if (!strcmp(path,s->networks[i].path) && count<WIFI_MAX_NETWORKS) {
                list[count]=s->networks[i]; list[count++].signal=signal/100; break;
            }
            if (sd_bus_message_exit_container(m)<0) break;
        }
        memcpy(s->networks,list,count*sizeof(*list)); s->count=count;
    }
    sd_bus_message_unref(m);
}
static void execute(AppWifi *w, AppWifiAction action, const char *path, const char *password, AppWifiState *s)
{
    sd_bus_error error=SD_BUS_ERROR_NULL;
    int r=-1;
    if (!s->available) return;
    const AppWifiNetwork *n=NULL;
    for (unsigned i=0;i<s->count;i++) if (!strcmp(path,s->networks[i].path)) n=&s->networks[i];
    if (action==APP_WIFI_SCAN) r=sd_bus_call_method(w->bus,IWD,w->station,STATION,"Scan",&error,NULL,"");
    else if (action==APP_WIFI_DISCONNECT) r=sd_bus_call_method(w->bus,IWD,w->station,STATION,"Disconnect",&error,NULL,"");
    else if (!n) { message(w,"Network disappeared. Scan again."); return; }
    else if (action==APP_WIFI_FORGET) {
        if (!*n->known) { message(w,"This network has no saved settings."); return; }
        r=sd_bus_call_method(w->bus,IWD,n->known,IWD ".KnownNetwork","Forget",&error,NULL,"");
    } else {
        if (strcmp(n->security,"open") && strcmp(n->security,"psk")) { message(w,"Only open and PSK networks are supported."); return; }
        if (!w->registered) {
            r=sd_bus_call_method(w->bus,IWD,"/net/connman/iwd",IWD ".AgentManager","RegisterAgent",&error,NULL,"o",AGENT_PATH);
            if (r<0) goto done;
            w->registered=true;
        }
        copy(w->secret,sizeof(w->secret),password); copy(w->connecting,sizeof(w->connecting),path);
        sd_bus_message *m=NULL;
        r=sd_bus_message_new_method_call(w->bus,&m,IWD,path,NETWORK,"Connect");
        if (r>=0) r=sd_bus_call_async(w->bus,&w->pending,m,completed,w,60000000);
        sd_bus_message_unref(m); w->deadline=monotonic_seconds()+65;
    }
done:
    message(w,r<0 ? (error.message ? error.message : "Wifi request failed.") :
        action==APP_WIFI_SCAN ? "Scanning..." : action==APP_WIFI_CONNECT ? "Connecting..." :
        action==APP_WIFI_FORGET ? "Network forgotten." : "Disconnected.");
    if (r<0) wipe(w->secret,sizeof(w->secret));
    sd_bus_error_free(&error);
}
#endif
static void *worker(void *data)
{
    AppWifi *w=data;
#ifdef APP_HAVE_SYSTEMD
    unsigned tick=0;
#endif
    for (;;) {
        char path[WIFI_PATH_SIZE], password[65]; AppWifiAction action;
        pthread_mutex_lock(&w->lock);
        bool stop=w->stop, queued=w->queued; action=w->action;
        copy(path,sizeof(path),w->path); copy(password,sizeof(password),w->password);
        w->queued=false; wipe(w->password,sizeof(w->password));
        pthread_mutex_unlock(&w->lock);
        if (stop) { wipe(password,sizeof(password)); break; }
#ifdef APP_HAVE_SYSTEMD
        if (!w->bus && sd_bus_open_system(&w->bus)>=0) {
            sd_bus_set_method_call_timeout(w->bus,2000000);
            if (sd_bus_add_object_vtable(w->bus,&w->agent,AGENT_PATH,IWD ".Agent",agent_vtable,w)<0) {
                w->bus=sd_bus_unref(w->bus);
            }
        }
        if (w->bus) {
            int r;
            while ((r=sd_bus_process(w->bus,NULL))>0) {}
            if (r<0) {
                w->pending=sd_bus_slot_unref(w->pending); w->agent=sd_bus_slot_unref(w->agent);
                w->bus=sd_bus_unref(w->bus); w->registered=false; wipe(w->secret,sizeof(w->secret));
            }
        }
        if (w->pending && monotonic_seconds()>w->deadline) {
            w->pending=sd_bus_slot_unref(w->pending); wipe(w->secret,sizeof(w->secret)); message(w,"Connection timed out.");
        }
        if (queued || tick++%20==0) {
            AppWifiState s={0};
            if (!w->bus || discover(w,&s)<0) {
                s.available=false; s.count=0; w->registered=false;
                if (!*s.message) copy(s.message,sizeof(s.message),"Wifi service unavailable.");
            } else ordered(w,&s);
            pthread_mutex_lock(&w->lock);
            if (!*s.message) {
                const char *old=w->state.message;
                if (!strcmp(old,"Scanning...") && !s.scanning) old="Scan complete. Select a network to connect.";
                if (!strcmp(old,"Connecting to Wifi service...") || !strcmp(old,"Wifi service unavailable.")) old="Select Scan to find Wifi networks.";
                copy(s.message,sizeof(s.message),old);
            }
            s.busy=w->state.busy; w->state=s;
            pthread_mutex_unlock(&w->lock);
            if (queued && !w->pending) execute(w,action,path,password,&s);
        }
        pthread_mutex_lock(&w->lock); w->state.busy=w->pending || w->queued; pthread_mutex_unlock(&w->lock);
#else
        (void)queued; (void)action; (void)path;
        message(w,"Wifi support unavailable in this build.");
        pthread_mutex_lock(&w->lock); w->state.busy=false; pthread_mutex_unlock(&w->lock);
#endif
        wipe(password,sizeof(password));
        struct timespec delay={.tv_nsec=100000000}; nanosleep(&delay,NULL);
    }
#ifdef APP_HAVE_SYSTEMD
    if (w->bus && w->registered) sd_bus_call_method(w->bus,IWD,"/net/connman/iwd",IWD ".AgentManager","UnregisterAgent",NULL,NULL,"o",AGENT_PATH);
    sd_bus_slot_unref(w->pending); sd_bus_slot_unref(w->agent); sd_bus_unref(w->bus); wipe(w->secret,sizeof(w->secret));
#endif
    return NULL;
}
AppWifi *app_wifi_start(void)
{
    AppWifi *w=calloc(1,sizeof(*w)); if (!w) return NULL;
    pthread_mutex_init(&w->lock,NULL); copy(w->state.message,sizeof(w->state.message),"Connecting to Wifi service...");
    if (pthread_create(&w->thread,NULL,worker,w)) { pthread_mutex_destroy(&w->lock); free(w); return NULL; }
    return w;
}
void app_wifi_stop(AppWifi *w)
{
    if (!w) return;
    pthread_mutex_lock(&w->lock); w->stop=true; pthread_mutex_unlock(&w->lock);
    pthread_join(w->thread,NULL); pthread_mutex_destroy(&w->lock); wipe(w->password,sizeof(w->password)); free(w);
}
void app_wifi_snapshot(AppWifi *w, AppWifiState *s)
{
    memset(s,0,sizeof(*s)); if (!w) { copy(s->message,sizeof(s->message),"Wifi unavailable."); return; }
    pthread_mutex_lock(&w->lock); *s=w->state; pthread_mutex_unlock(&w->lock);
}
bool app_wifi_request(AppWifi *w, AppWifiAction action, const char *path, const char *password)
{
    if (!w || action<APP_WIFI_SCAN || action>APP_WIFI_FORGET ||
        (path && strlen(path)>=WIFI_PATH_SIZE) || (password && strlen(password)>64)) return false;
    pthread_mutex_lock(&w->lock);
    bool ok=w->state.available && !w->state.busy && !w->queued;
    if (ok) { w->action=action; copy(w->path,sizeof(w->path),path); copy(w->password,sizeof(w->password),password); w->queued=true; w->state.busy=true; }
    pthread_mutex_unlock(&w->lock); return ok;
}
