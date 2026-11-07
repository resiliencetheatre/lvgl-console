#define _POSIX_C_SOURCE 200809L
#include "update_client.h"
#include <json-c/json.h>
#include <assert.h>
#include <pthread.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>

static atomic_int mode, checks, downloads, installs;
static const char *identity="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char *body="{\"device\":{\"version\":\"dev-11\",\"trial\":false,\"state\":{}},\"device_error\":null,\"offer_error\":null,\"download_ready\":false,\"job\":{\"phase\":\"idle\"},\"offer\":null}";
static json_object *snapshot(void)
{
    json_object *result=json_tokener_parse(body);
    if (atomic_load(&mode)>0) {
        json_object *offer=json_object_new_object(), *bundle=json_object_new_object();
        json_object_object_add(offer,"offer_id",json_object_new_string(identity));
        json_object_object_add(offer,"version",json_object_new_string("dev-12"));
        json_object_object_add(offer,"notes",json_object_new_string("Display and audio fixes"));
        json_object_object_add(bundle,"size",json_object_new_int64(805309300));
        json_object_object_add(offer,"bundle",bundle); json_object_object_add(result,"offer",offer);
        json_object *job=NULL; json_object_object_get_ex(result,"job",&job);
        json_object_object_add(job,"phase",json_object_new_string(atomic_load(&mode)==1 ? "available" : atomic_load(&mode)==2 ? "downloaded" : "installing"));
        json_object_object_add(result,"download_ready",json_object_new_boolean(atomic_load(&mode)>=2));
    }
    return result;
}
static void *reply(void *argument)
{
    int fd=*(int *)argument; free(argument);
    char input[4096]; size_t used=0;
    for (;;) {
        struct pollfd p={.fd=fd,.events=POLLIN}; if (poll(&p,1,3000)<=0) abort();
        ssize_t n=read(fd,input+used,sizeof(input)-1-used); if(n<=0) abort();
        used+=(size_t)n; if(memchr(input,'\n',used))break;
    }
    input[used]=0; json_object *request=json_tokener_parse(input),*value=NULL,*params=NULL;
    assert(request); json_object_object_get_ex(request,"method",&value);
    const char *method=json_object_get_string(value);
    if(strcmp(method,"status")) {
        json_object_object_get_ex(request,"params",&params);
        if(!strcmp(method,"check")) { atomic_fetch_add(&checks,1); atomic_store(&mode,1); }
        else {
            json_object_object_get_ex(params,"offer_id",&value); assert(!strcmp(json_object_get_string(value),identity));
            json_object_object_get_ex(params,"accept",&value); assert(json_object_get_boolean(value));
            if(!strcmp(method,"download")){ atomic_fetch_add(&downloads,1);atomic_store(&mode,2); }
            else { assert(!strcmp(method,"install"));atomic_fetch_add(&installs,1);atomic_store(&mode,3); }
        }
    }
    json_object *response=json_object_new_object();
    json_object_object_add(response,"api",json_object_new_int(1));
    json_object_object_get_ex(request,"id",&value); json_object_object_add(response,"id",json_object_get(value));
    json_object_object_add(response,"ok",json_object_new_boolean(true));
    json_object_object_add(response,"result",!strcmp(method,"status") ? snapshot() : json_object_new_object());
    const char *wire=json_object_to_json_string_ext(response,JSON_C_TO_STRING_PLAIN);
    assert(write(fd,wire,strlen(wire))==(ssize_t)strlen(wire)); assert(write(fd,"\n",1)==1);
    json_object_put(response);json_object_put(request);close(fd);return NULL;
}
int __wrap_socket(int domain,int type,int protocol)
{
    assert(domain==AF_UNIX); int pair[2];assert(socketpair(domain,type,protocol,pair)==0);
    int *fd=malloc(sizeof(*fd));*fd=pair[1]; pthread_t thread;
    assert(pthread_create(&thread,NULL,reply,fd)==0);pthread_detach(thread);return pair[0];
}
ssize_t __wrap_send(int fd,const void *data,size_t size,int flags)
{
    struct iovec iov={.iov_base=(void *)data,.iov_len=size};
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1};return sendmsg(fd,&msg,flags);
}
ssize_t __wrap_recv(int fd,void *data,size_t size,int flags)
{
    struct iovec iov={.iov_base=data,.iov_len=size};
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1};return recvmsg(fd,&msg,flags);
}
int __wrap_connect(int fd,const struct sockaddr *address,socklen_t size)
{
    (void)fd;(void)size;assert(!strcmp(((const struct sockaddr_un *)address)->sun_path,"/mock/updates.sock"));return 0;
}
static AppUpdateState wait_state(AppUpdates *u,int stage)
{
    AppUpdateState s;
    for(unsigned i=0;i<600;++i){
        app_updates_snapshot(u,&s);
        if((stage==0 && s.can_check) || (stage==1 && s.can_update && !s.download_ready) ||
           (stage==2 && s.can_update && s.download_ready) || (stage==3 && !strcmp(s.phase,"installing")))return s;
        struct timespec wait={.tv_nsec=10000000};nanosleep(&wait,NULL);
    }
    fprintf(stderr,"Timed out at stage %d: %s\n",stage,s.message);abort();
}
static void parser_tests(void)
{
    AppUpdateState s;
    assert(!app_updates_parse_status("{}",&s)); assert(!app_updates_parse_status("[] trailing",&s));
    atomic_store(&mode,1);json_object *root=json_object_new_object(),*result=snapshot(),*job=NULL,*device=NULL;
    json_object_object_add(root,"api",json_object_new_int(1));json_object_object_add(root,"ok",json_object_new_boolean(true));json_object_object_add(root,"result",result);
    assert(app_updates_parse_status(json_object_to_json_string(root),&s) && s.can_update);
    json_object_object_get_ex(result,"device",&device);json_object_object_add(device,"trial",json_object_new_boolean(true));
    assert(app_updates_parse_status(json_object_to_json_string(root),&s) && !s.can_update && !s.can_check);
    json_object_object_add(device,"trial",json_object_new_boolean(false));json_object_object_get_ex(result,"job",&job);
    json_object_object_add(job,"phase",json_object_new_string("downloading"));json_object_object_add(job,"bytes_received",json_object_new_int64(50));json_object_object_add(job,"bytes_total",json_object_new_int64(100));
    assert(app_updates_parse_status(json_object_to_json_string(root),&s) && s.percent==50 && s.busy && !s.can_update);
    json_object_object_add(job,"phase",json_object_new_string("installing"));json_object_object_add(job,"stage",json_object_new_string("verifying_bundle"));
    assert(app_updates_parse_status(json_object_to_json_string(root),&s) && s.percent==-1 && strstr(s.message,"Verifying"));
    json_object_object_add(job,"stage",json_object_new_string("installing_slots"));
    json_object *progress=json_tokener_parse("{\"status\":\"run\",\"step\":1,\"steps\":2,\"percent\":37,\"image\":\"boot-b.vfat\"}");json_object_object_add(job,"swupdate",progress);
    assert(app_updates_parse_status(json_object_to_json_string(root),&s) && s.percent==37 && strstr(s.message,"boot-b.vfat"));
    json_object_object_add(progress,"status",json_object_new_string("success"));
    assert(app_updates_parse_status(json_object_to_json_string(root),&s) && s.percent==-1 && strstr(s.message,"Checking installed"));
    json_object_put(root);atomic_store(&mode,0);
}
int main(void)
{
    parser_tests();AppUpdates *u=app_updates_start("/mock/updates.sock");assert(u);
    wait_state(u,0); assert(!checks && !downloads && !installs);
    assert(app_updates_request(u,APP_UPDATE_CHECK,NULL));wait_state(u,1);assert(checks==1 && !downloads && !installs);
    assert(!app_updates_request(u,APP_UPDATE_DOWNLOAD,"stale"));
    assert(app_updates_request(u,APP_UPDATE_DOWNLOAD,identity));wait_state(u,2);assert(downloads==1 && !installs);
    assert(app_updates_request(u,APP_UPDATE_INSTALL,identity));wait_state(u,3);assert(installs==1);
    assert(!app_updates_request(u,APP_UPDATE_INSTALL,identity));app_updates_stop(u);
    puts("Update client parser, consent, Unix RPC and async lifecycle: OK");return 0;
}
