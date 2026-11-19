#define _POSIX_C_SOURCE 200809L
#include "wifi_client.h"
#include <systemd/sd-bus.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define IWD "net.connman.iwd"
#define DEVICE "/net/connman/iwd/0/1"
#define NETWORK DEVICE "/network1"
#define KNOWN "/net/connman/iwd/known1"
static sd_bus *server;
static atomic_int stop, scans, connections, disconnects, forgets, reject, connected;
static char agent_destination[256], agent_path[256];
static void pause_tick(void) { struct timespec t={.tv_nsec=10000000}; nanosleep(&t,NULL); }
static int property(sd_bus *bus,const char *path,const char *iface,const char *name,sd_bus_message *reply,void *data,sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)data; (void)error;
    if (!strcmp(name,"State")) return sd_bus_message_append(reply,"s",connected ? "connected" : "disconnected");
    if (!strcmp(name,"Scanning")) return sd_bus_message_append(reply,"b",0);
    if (!strcmp(name,"Connected")) return sd_bus_message_append(reply,"b",(int)connected);
    if (!strcmp(name,"Name")) return sd_bus_message_append(reply,"s","Test wifi");
    if (!strcmp(name,"Type")) return sd_bus_message_append(reply,"s","psk");
    if (!strcmp(name,"Device")) return sd_bus_message_append(reply,"o",DEVICE);
    if (!strcmp(name,"KnownNetwork")) return sd_bus_message_append(reply,"o",KNOWN);
    return -1;
}
static int method(sd_bus_message *m,void *data,sd_bus_error *error)
{
    (void)data; (void)error; const char *member=sd_bus_message_get_member(m);
    if (!strcmp(member,"RegisterAgent")) {
        const char *path; assert(sd_bus_message_read(m,"o",&path)>0);
        snprintf(agent_path,sizeof(agent_path),"%s",path);
        snprintf(agent_destination,sizeof(agent_destination),"%s",sd_bus_message_get_sender(m));
    } else if (!strcmp(member,"Scan")) scans++;
    else if (!strcmp(member,"Connect")) {
        sd_bus_message *reply=NULL;
        int r=sd_bus_call_method(server,agent_destination,agent_path,IWD ".Agent","RequestPassphrase",NULL,&reply,"o",NETWORK);
        assert(r>=0); const char *secret; assert(sd_bus_message_read(reply,"s",&secret)>0);
        assert(!strcmp(secret,"visible test password")); sd_bus_message_unref(reply);
        connections++;
        if (reject) return sd_bus_reply_method_errorf(m,IWD ".Failed","Authentication failed");
        connected=1;
    } else if (!strcmp(member,"Disconnect")) { disconnects++; connected=0; }
    else if (!strcmp(member,"Forget")) forgets++;
    else if (!strcmp(member,"GetOrderedNetworks")) {
        sd_bus_message *reply=NULL; assert(sd_bus_message_new_method_return(m,&reply)>=0);
        assert(sd_bus_message_open_container(reply,'a',"(on)")>=0);
        assert(sd_bus_message_append(reply,"(on)",NETWORK,(int16_t)-4500)>=0);
        assert(sd_bus_message_close_container(reply)>=0);
        int r=sd_bus_send(server,reply,NULL); sd_bus_message_unref(reply); return r;
    }
    return sd_bus_reply_method_return(m,"");
}
static const sd_bus_vtable station[]={
 SD_BUS_VTABLE_START(0),
 SD_BUS_METHOD("Scan","","",method,0), SD_BUS_METHOD("Disconnect","","",method,0),
 SD_BUS_METHOD("GetOrderedNetworks","","a(on)",method,0),
 SD_BUS_PROPERTY("Scanning","b",property,0,0), SD_BUS_PROPERTY("State","s",property,0,0), SD_BUS_VTABLE_END
};
static const sd_bus_vtable network[]={
 SD_BUS_VTABLE_START(0), SD_BUS_METHOD("Connect","","",method,0),
 SD_BUS_PROPERTY("Name","s",property,0,0), SD_BUS_PROPERTY("Type","s",property,0,0),
 SD_BUS_PROPERTY("Device","o",property,0,0), SD_BUS_PROPERTY("KnownNetwork","o",property,0,0),
 SD_BUS_PROPERTY("Connected","b",property,0,0), SD_BUS_VTABLE_END
};
static const sd_bus_vtable manager[]={
 SD_BUS_VTABLE_START(0), SD_BUS_METHOD("RegisterAgent","o","",method,0),
 SD_BUS_METHOD("UnregisterAgent","o","",method,0), SD_BUS_VTABLE_END
};
static const sd_bus_vtable known[]={SD_BUS_VTABLE_START(0), SD_BUS_METHOD("Forget","","",method,0), SD_BUS_VTABLE_END};
static void *serve(void *unused)
{
    (void)unused;
    while (!stop) { int r=sd_bus_process(server,NULL); assert(r>=0); if (!r) pause_tick(); }
    return NULL;
}
static AppWifiState wait_state(AppWifi *w, bool available, bool connected_expected)
{
    AppWifiState s;
    for (unsigned i=0;i<800;i++) {
        app_wifi_snapshot(w,&s);
        if (s.available==available && !s.busy && (!available || (s.count==1 && s.networks[0].connected==connected_expected))) return s;
        pause_tick();
    }
    fprintf(stderr,"Wifi state timeout: %s\n",s.message); assert(0); return s;
}
static void wait_action(AppWifi *w, atomic_int *counter)
{
    for (unsigned i=0;i<500;i++) {
        AppWifiState s; app_wifi_snapshot(w,&s);
        if (*counter && !s.busy) return;
        pause_tick();
    }
    assert(0);
}
int main(void)
{
    assert(sd_bus_open_system(&server)>=0);
    assert(sd_bus_request_name(server,IWD,0)>=0);
    assert(sd_bus_add_object_manager(server,NULL,"/")>=0);
    assert(sd_bus_add_object_vtable(server,NULL,DEVICE,IWD ".Station",station,NULL)>=0);
    assert(sd_bus_add_object_vtable(server,NULL,NETWORK,IWD ".Network",network,NULL)>=0);
    assert(sd_bus_add_object_vtable(server,NULL,KNOWN,IWD ".KnownNetwork",known,NULL)>=0);
    assert(sd_bus_add_object_vtable(server,NULL,"/net/connman/iwd",IWD ".AgentManager",manager,NULL)>=0);
    pthread_t thread; assert(!pthread_create(&thread,NULL,serve,NULL));
    AppWifi *w=app_wifi_start(); assert(w);
    AppWifiState s=wait_state(w,true,false);
    assert(s.networks[0].signal==-45); assert(!strcmp(s.networks[0].security,"psk"));
    assert(app_wifi_request(w,APP_WIFI_SCAN,NULL,NULL)); wait_action(w,&scans);
    assert(app_wifi_request(w,APP_WIFI_CONNECT,NETWORK,"visible test password"));
    assert(!app_wifi_request(w,APP_WIFI_SCAN,NULL,NULL));
    wait_action(w,&connections); wait_state(w,true,true);
    assert(app_wifi_request(w,APP_WIFI_DISCONNECT,NULL,NULL)); wait_action(w,&disconnects); wait_state(w,true,false);
    assert(app_wifi_request(w,APP_WIFI_FORGET,NETWORK,NULL)); wait_action(w,&forgets);
    reject=1; connections=0;
    assert(app_wifi_request(w,APP_WIFI_CONNECT,NETWORK,"visible test password")); wait_action(w,&connections);
    app_wifi_snapshot(w,&s); assert(strstr(s.message,"Authentication failed"));
    assert(app_wifi_request(w,APP_WIFI_CONNECT,"/stale",NULL));
    for (unsigned i=0;i<100;i++) { app_wifi_snapshot(w,&s); if (!s.busy) break; pause_tick(); }
    assert(strstr(s.message,"disappeared"));
    app_wifi_stop(w);
    stop=1; pthread_join(thread,NULL);
    assert(sd_bus_release_name(server,IWD)>=0); sd_bus_unref(server);
    w=app_wifi_start(); s=wait_state(w,false,false); assert(!app_wifi_request(w,APP_WIFI_SCAN,NULL,NULL)); app_wifi_stop(w);
    puts("Wifi discovery, scan, credential agent, connect, disconnect, forget, errors and shutdown passed");
}
