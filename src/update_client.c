#define _POSIX_C_SOURCE 200809L
#include "update_client.h"
#include <json-c/json.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define REPLY_MAX 131072
struct AppUpdates {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    bool stop, queued;
    AppUpdateAction action;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)], offer[65], error[256];
    AppUpdateState state;
    unsigned sequence;
};
static json_object *field(json_object *object, const char *key)
{
    json_object *value = NULL;
    if (object && json_object_is_type(object, json_type_object)) json_object_object_get_ex(object, key, &value);
    return value;
}
static const char *text(json_object *object, const char *key)
{
    json_object *value = field(object, key);
    return value && json_object_is_type(value, json_type_string) ? json_object_get_string(value) : "";
}
static bool flag(json_object *object, const char *key)
{
    json_object *value = field(object, key);
    return value && json_object_is_type(value, json_type_boolean) && json_object_get_boolean(value);
}
static uint64_t number(json_object *object, const char *key)
{
    json_object *value = field(object, key);
    return value && json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0 ? json_object_get_uint64(value) : 0;
}
static void copy(char *dest, size_t size, const char *source)
{
    snprintf(dest, size, "%s", source);
}
static json_object *parse(const char *json)
{
    size_t size = strlen(json);
    if (!size || size > REPLY_MAX) return NULL;
    json_tokener *tokener = json_tokener_new_ex(32);
    if (!tokener) return NULL;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    json_object *value = json_tokener_parse_ex(tokener, json, (int)size);
    size_t end = json_tokener_get_parse_end(tokener);
    while (end < size && (json[end] == ' ' || json[end] == '\n' || json[end] == '\r' || json[end] == '\t')) ++end;
    if (json_tokener_get_error(tokener) != json_tokener_success || end != size) {
        if (value) json_object_put(value);
        value = NULL;
    }
    json_tokener_free(tokener);
    return value;
}
static bool id_valid(const char *id)
{
    if (strlen(id) != 64) return false;
    for (unsigned i = 0; i < 64; ++i) if (!strchr("0123456789abcdef", id[i])) return false;
    return true;
}
bool app_updates_parse_status(const char *json, AppUpdateState *s)
{
    memset(s, 0, sizeof(*s)); s->percent = -1;
    json_object *root = parse(json);
    if (!root) return false;
    json_object *result = field(root, "result"), *device = field(result, "device");
    json_object *job = field(result, "job"), *offer = field(result, "offer");
    json_object *api = field(root, "api");
    if (!api || !json_object_is_type(api, json_type_int) || json_object_get_int(api) != 1 || !flag(root, "ok") ||
        !job || !json_object_is_type(job, json_type_object) || strlen(text(device, "version")) > 64 ||
        strlen(text(offer, "version")) > 64 || strlen(text(offer, "offer_id")) > 64) {
        json_object_put(root); return false;
    }
    s->connected = true;
    copy(s->phase, sizeof(s->phase), text(job, "phase"));
    copy(s->version, sizeof(s->version), text(device, "version"));
    copy(s->offer_version, sizeof(s->offer_version), text(offer, "version"));
    copy(s->offer_id, sizeof(s->offer_id), text(offer, "offer_id"));
    copy(s->notes, sizeof(s->notes), text(offer, "notes"));
    s->download_ready = flag(result, "download_ready"); s->trial = flag(device, "trial");
    s->busy = !strcmp(s->phase,"checking") || !strcmp(s->phase,"downloading") || !strcmp(s->phase,"installing");
    const char *transaction = text(field(device, "state"), "phase");
    bool normal = device && json_object_is_type(field(device, "trial"), json_type_boolean) && *s->version && !s->trial && strcmp(transaction,"pending") &&
        strcmp(transaction,"trial") && strcmp(transaction,"committing") && strcmp(transaction,"installing") &&
        !*text(result,"device_error");
    s->can_finish = s->trial && !s->busy && !*text(result,"device_error") &&
        !strcmp(transaction,"committed") && !strcmp(text(field(device,"state"),"target"),text(device,"slot")) &&
        *s->version && !strcmp(text(field(device,"state"),"version"),s->version);
    s->can_check = normal && !s->busy;
    s->can_update = s->can_check && id_valid(s->offer_id) && *s->offer_version &&
        strcmp(s->offer_version,s->version) && !field(result,"offer_error") &&
        (!strcmp(s->phase,"available") || !strcmp(s->phase,"downloaded") ||
         (!strcmp(s->phase,"failed") && s->download_ready));
    s->offer_size = number(field(offer,"bundle"),"size");
    s->received = number(job,"bytes_received"); s->total = number(job,"bytes_total");
    if (!strcmp(s->phase,"checking")) copy(s->message,sizeof(s->message),"Checking for updates...");
    else if (!strcmp(s->phase,"downloading")) {
        if (s->total && s->received <= s->total) s->percent = (int)((double)s->received * 100.0 / (double)s->total);
        copy(s->message,sizeof(s->message),s->received == s->total && s->total ? "Verifying and saving download..." : "Downloading update...");
        snprintf(s->detail,sizeof(s->detail),"%.1f / %.1f MiB",(double)s->received/1048576.0,(double)s->total/1048576.0);
    } else if (!strcmp(s->phase,"installing")) {
        json_object *progress = field(job,"swupdate");
        const char *status = text(progress,"status");
        if (!strcmp(text(job,"stage"),"verifying_bundle")) copy(s->message,sizeof(s->message),"Verifying signed update...");
        else if (!strcmp(status,"failure")) copy(s->message,sizeof(s->message),"Installer reported a failure; waiting for the final result...");
        else if (!strcmp(status,"success") || !strcmp(status,"done")) copy(s->message,sizeof(s->message),"Checking installed data and preparing reboot...");
        else if (progress && *text(progress,"image")) {
            uint64_t percent = number(progress,"percent");
            s->percent = percent <= 100 ? (int)percent : -1;
            snprintf(s->message,sizeof(s->message),"Installing %s",text(progress,"image"));
            snprintf(s->detail,sizeof(s->detail),"Step %llu / %llu, %d%%",
                     (unsigned long long)number(progress,"step"),(unsigned long long)number(progress,"steps"),s->percent);
        } else copy(s->message,sizeof(s->message),"Installing update; keep power connected...");
    } else if (!strcmp(transaction,"committed") && *s->offer_version && !strcmp(s->offer_version,s->version)) {
        copy(s->message,sizeof(s->message),s->trial ? "Update committed. Restart normally before another update." : "Update committed. Running the new release.");
    } else if (!strcmp(s->phase,"installed")) copy(s->message,sizeof(s->message),"Installation complete. Restarting for the trial boot...");
    else if (!strcmp(s->phase,"failed") || !strcmp(s->phase,"interrupted")) {
        copy(s->message,sizeof(s->message),text(field(job,"error"),"message"));
        if (!*s->message) copy(s->message,sizeof(s->message),"Update interrupted. Check device status before retrying.");
    } else if (!strcmp(s->phase,"cancelled")) copy(s->message,sizeof(s->message),"Download cancelled.");
    else if (*s->offer_version && !strcmp(s->offer_version,s->version)) copy(s->message,sizeof(s->message),"This release is already installed.");
    else if (s->download_ready) copy(s->message,sizeof(s->message),"Download ready. Review and accept installation.");
    else if (*s->offer_version) copy(s->message,sizeof(s->message),"An update is available.");
    else copy(s->message,sizeof(s->message),"Check for a new release over the network.");
    if (s->can_finish) copy(s->message,sizeof(s->message),"Update confirmed. Select Finish update to restart, or continue using the device.");
    else if (!s->busy && s->trial) copy(s->message,sizeof(s->message),"Trial boot: finish confirmation and restart before updating.");
    if (*text(field(result,"offer_error"),"message")) copy(s->message,sizeof(s->message),text(field(result,"offer_error"),"message"));
    if (!s->busy && *text(result,"device_error")) copy(s->message,sizeof(s->message),"Device status unavailable; another update may be running.");
    json_object_put(root);
    return true;
}

static int64_t milliseconds(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (int64_t)now.tv_sec*1000 + now.tv_nsec/1000000;
}
static bool ready(int fd, short events, int64_t until)
{
    for (;;) {
        int64_t left=until-milliseconds(); if (left<=0) return false;
        struct pollfd p={.fd=fd,.events=events}; int result=poll(&p,1,(int)left);
        if (result<0 && errno==EINTR) continue;
        return result>0 && (p.revents & events);
    }
}
static json_object *rpc(AppUpdates *u, const char *method, const char *offer, char *buffer)
{
    json_object *request=json_object_new_object(), *params=json_object_new_object();
    char id[40]; snprintf(id,sizeof(id),"lvgl-%u",++u->sequence);
    json_object_object_add(request,"api",json_object_new_int(1));
    json_object_object_add(request,"id",json_object_new_string(id));
    json_object_object_add(request,"method",json_object_new_string(method));
    json_object_object_add(request,"params",params);
    if (!strcmp(method,"finish")) json_object_object_add(params,"accept",json_object_new_boolean(true));
    if (offer) {
        json_object_object_add(params,"offer_id",json_object_new_string(offer));
        json_object_object_add(params,"accept",json_object_new_boolean(true));
    }
    const char *body=json_object_to_json_string_ext(request,JSON_C_TO_STRING_PLAIN);
    size_t length=strlen(body), sent=0;
    char *wire=malloc(length+2);
    if (!wire) { json_object_put(request); return NULL; }
    memcpy(wire,body,length); wire[length++]='\n'; wire[length]=0;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    bool ok=fd>=0; int64_t until=milliseconds()+4000;
    struct sockaddr_un address={.sun_family=AF_UNIX}; copy(address.sun_path,sizeof(address.sun_path),u->path);
    if (ok && connect(fd,(struct sockaddr *)&address,sizeof(address))<0) {
        ok=errno==EINPROGRESS && ready(fd,POLLOUT,until);
        int error=0; socklen_t size=sizeof(error);
        if (ok) ok=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0 && !error;
    }
    while (ok && sent<length) {
        if (!ready(fd,POLLOUT,until)) { ok=false; break; }
        ssize_t n=send(fd,wire+sent,length-sent,MSG_NOSIGNAL);
        if (n<0 && (errno==EINTR || errno==EAGAIN)) continue;
        if (n<=0) { ok=false; break; } sent+=(size_t)n;
    }
    size_t used=0;
    while (ok && used<REPLY_MAX) {
        if (!ready(fd,POLLIN,until)) { ok=false; break; }
        ssize_t n=recv(fd,buffer+used,REPLY_MAX-used,0);
        if (n<0 && (errno==EINTR || errno==EAGAIN)) continue;
        if (n<=0) { ok=false; break; } used+=(size_t)n;
        if (memchr(buffer,'\n',used)) break;
    }
    if (fd>=0) close(fd);
    buffer[used]=0; free(wire); json_object_put(request);
    if (!ok || !used || buffer[used-1]!='\n') return NULL;
    json_object *reply=parse(buffer);
    if (reply && strcmp(text(reply,"id"),id)) { json_object_put(reply); reply=NULL; }
    return reply;
}
static void *worker(void *argument)
{
    AppUpdates *u=argument; char *buffer=malloc(REPLY_MAX+1);
    if (!buffer) return NULL;
    for (;;) {
        pthread_mutex_lock(&u->lock);
        if (u->stop) { pthread_mutex_unlock(&u->lock); break; }
        bool queued=u->queued; AppUpdateAction action=u->action;
        char offer[65]; copy(offer,sizeof(offer),u->offer); u->queued=false;
        pthread_mutex_unlock(&u->lock);
        if (queued) {
            const char *method=action==APP_UPDATE_CHECK ? "check" : action==APP_UPDATE_DOWNLOAD ? "download" : action==APP_UPDATE_INSTALL ? "install" : "finish";
            json_object *response=rpc(u,method,(action==APP_UPDATE_CHECK || action==APP_UPDATE_FINISH) ? NULL : offer,buffer);
            pthread_mutex_lock(&u->lock);
            copy(u->error,sizeof(u->error),!response ? "Request connection lost. Refresh status before retrying." :
                 flag(response,"ok") ? "" : text(field(response,"error"),"message"));
            pthread_mutex_unlock(&u->lock);
            if (response) json_object_put(response);
        }
        json_object *reply=rpc(u,"status",NULL,buffer);
        AppUpdateState state;
        bool valid=reply && app_updates_parse_status(buffer,&state);
        if (reply) json_object_put(reply);
        if (!valid) {
            memset(&state,0,sizeof(state)); state.percent=-1;
            copy(state.message,sizeof(state.message),"Update service unavailable.");
        }
        pthread_mutex_lock(&u->lock);
        if (valid && state.busy) *u->error=0;
        if (*u->error) copy(state.message,sizeof(state.message),u->error);
        u->state=state;
        if (!u->stop && !u->queued) {
            struct timespec next; clock_gettime(CLOCK_REALTIME,&next); ++next.tv_sec;
            pthread_cond_timedwait(&u->wake,&u->lock,&next);
        }
        pthread_mutex_unlock(&u->lock);
    }
    free(buffer); return NULL;
}
AppUpdates *app_updates_start(const char *path)
{
    AppUpdates *u=calloc(1,sizeof(*u)); if (!u) return NULL;
    if (!path || !*path || strlen(path)>=sizeof(u->path)) { free(u); return NULL; }
    copy(u->path,sizeof(u->path),path); u->state.percent=-1;
    copy(u->state.message,sizeof(u->state.message),"Connecting to update service...");
    pthread_mutex_init(&u->lock,NULL); pthread_cond_init(&u->wake,NULL);
    if (pthread_create(&u->thread,NULL,worker,u)) {
        pthread_cond_destroy(&u->wake); pthread_mutex_destroy(&u->lock); free(u); return NULL;
    }
    return u;
}
void app_updates_stop(AppUpdates *u)
{
    if (!u) return;
    pthread_mutex_lock(&u->lock); u->stop=true; pthread_cond_signal(&u->wake); pthread_mutex_unlock(&u->lock);
    pthread_join(u->thread,NULL); pthread_cond_destroy(&u->wake); pthread_mutex_destroy(&u->lock); free(u);
}
void app_updates_snapshot(AppUpdates *u, AppUpdateState *state)
{
    memset(state,0,sizeof(*state)); state->percent=-1;
    if (!u) { copy(state->message,sizeof(state->message),"Update service unavailable."); return; }
    pthread_mutex_lock(&u->lock); *state=u->state; pthread_mutex_unlock(&u->lock);
}
bool app_updates_request(AppUpdates *u, AppUpdateAction action, const char *offer)
{
    if (!u || action<APP_UPDATE_CHECK || action>APP_UPDATE_FINISH) return false;
    pthread_mutex_lock(&u->lock);
    bool ok=!u->queued && (action==APP_UPDATE_CHECK ? u->state.can_check : action==APP_UPDATE_FINISH ? u->state.can_finish : u->state.can_update);
    if (action==APP_UPDATE_DOWNLOAD || action==APP_UPDATE_INSTALL) ok=ok && offer && !strcmp(offer,u->state.offer_id) &&
        (action==APP_UPDATE_INSTALL ? u->state.download_ready : !u->state.download_ready);
    if (ok) {
        u->action=action; copy(u->offer,sizeof(u->offer),offer ? offer : "");
        *u->error=0; u->queued=true; u->state.busy=true; u->state.can_check=u->state.can_update=u->state.can_finish=false;
        copy(u->state.message,sizeof(u->state.message),"Submitting request..."); pthread_cond_signal(&u->wake);
    }
    pthread_mutex_unlock(&u->lock); return ok;
}
