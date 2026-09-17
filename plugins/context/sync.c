#include "sync.h"
#include <stdlib.h>
#include <string.h>

typedef struct { char *data; size_t used; } response_t;
static agent_err_t feed(void *ctx,const char *data,size_t n)
{
    response_t *r=ctx; if(n>AGENT_REQUEST_MAX-r->used) return AGENT_ERR_LIMIT;
    memcpy(r->data+r->used,data,n); r->used+=n; r->data[r->used]=0; return AGENT_OK;
}
static agent_err_t request(const agent_transport_ops_t *t,const atomic_bool *cancel,const char *method,const char *path,char *buf,cJSON **root)
{
    if(cancel && atomic_load(cancel)) return AGENT_ERR_CANCELLED;
    agent_http_request_t req={.target=AGENT_HTTP_GATEWAY,.method=method,.path=path,.body=buf,
        .length=!strcmp(method,"GET")?0:strlen(buf),.cancelled=cancel};
    response_t r={buf,0}; agent_err_t e=t->perform(t->ctx,&req,feed,&r);
    if(e) return e;
    *root=agent_json_parse(buf,r.used); return *root ? AGENT_OK : AGENT_ERR_JSON;
}
static agent_err_t quote_url(const char *src,char *dst,size_t cap)
{
    static const char hex[]="0123456789ABCDEF"; size_t used=0;
    for(const unsigned char *p=(const unsigned char *)src;*p;++p) {
        bool simple=(*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || strchr("-_.~",*p);
        size_t n=simple?1:3; if(used+n>=cap) return AGENT_ERR_LIMIT;
        if(simple) dst[used++]=(char)*p;
        else { dst[used++]='%'; dst[used++]=hex[*p>>4]; dst[used++]=hex[*p&15]; }
    }
    dst[used]=0; return AGENT_OK;
}
agent_err_t agent_context_sync(agent_context_t *c,const agent_transport_ops_t *t,const atomic_bool *cancel,bool *more)
{
    *more=false;
    if(c->mode==AGENT_CONTEXT_LOCAL) return AGENT_ERR_CONFIG;
    if(!c->wal.ready) return AGENT_ERR_STORAGE;
    char *buf=malloc(AGENT_REQUEST_MAX+1); if(!buf) return AGENT_ERR_MEMORY;
    cJSON *root=NULL; agent_err_t e=AGENT_OK;
    if(c->pending) {
        uint64_t sent,ack;
        e=agent_context_batch(c,buf,AGENT_REQUEST_MAX+1,&sent); if(e) goto done;
        if(sent<=c->acked) { e=AGENT_ERR_PROTOCOL; goto done; }
        e=request(t,cancel,"POST","/v1/context/events:batch",buf,&root); if(e) goto done;
        if(!agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"acked_seq"),sent,&ack) || ack!=sent) { e=AGENT_ERR_PROTOCOL; goto done; }
        cJSON_Delete(root); root=NULL;
        e=agent_context_checkpoint(c,c->cursor,ack,c->mode); if(e) goto done;
    }
    char user[193],session[193],path[512];
    e=quote_url(c->user,user,sizeof(user)); if(!e) e=quote_url(c->session,session,sizeof(session)); if(e) goto done;
    agent_json_writer_t w; agent_json_writer_init(&w,path,sizeof(path));
    agent_json_printf(&w,"/v1/context/events?cursor=%llu&user_id=%s&session_id=%s&device_id=%s",
        (unsigned long long)c->cursor,user,session,c->device);
    if(w.error) { e=w.error; goto done; }
    e=request(t,cancel,"GET",path,buf,&root); if(e) goto done;
    uint64_t cursor;
    cJSON *events=cJSON_GetObjectItemCaseSensitive(root,"events"),*remaining=cJSON_GetObjectItemCaseSensitive(root,"more");
    if(!agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"cursor"),AGENT_SEQ_MAX,&cursor) || cursor<c->cursor ||
       !cJSON_IsArray(events) || cJSON_GetArraySize(events)>4 || !cJSON_IsBool(remaining)) { e=AGENT_ERR_PROTOCOL; goto done; }
    *more=cJSON_IsTrue(remaining);
    for(const cJSON *event=events->child;event;event=event->next) {
        if(cancel && atomic_load(cancel)) { e=AGENT_ERR_CANCELLED; goto done; }
        e=agent_context_ingest(c,event); if(e) goto done;
    }
    cJSON_Delete(root); root=NULL;
    /* Cursor cannot pass an event until both the event and cursor are durable. */
    if(cursor!=c->cursor) { e=agent_context_checkpoint(c,cursor,c->acked,c->mode); if(e) goto done; }
    agent_json_writer_init(&w,buf,AGENT_REQUEST_MAX+1);
    agent_json_raw(&w,"{\"device_id\":"); agent_json_quote(&w,c->device);
    agent_json_raw(&w,",\"user_id\":"); agent_json_quote(&w,c->user);
    agent_json_raw(&w,",\"session_id\":"); agent_json_quote(&w,c->session);
    agent_json_printf(&w,",\"cursor\":%llu}",(unsigned long long)c->cursor);
    e=w.error; if(!e) e=request(t,cancel,"POST","/v1/context/ack",buf,&root);
    if(!e && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"ok"))) e=AGENT_ERR_PROTOCOL;
done:
    cJSON_Delete(root); free(buf); *more=*more || c->pending>0;
    return e;
}
