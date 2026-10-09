#include "context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t scan_start(const agent_context_t *c)
{ return c->now_ms?c->now_ms(c->clock_ctx):0; }
static agent_err_t scan_check(const agent_context_t *c,uint64_t start)
{
    if(c->cancelled && atomic_load(c->cancelled)) return AGENT_ERR_CANCELLED;
    return c->now_ms && c->now_ms(c->clock_ctx)-start>AGENT_CONTEXT_SCAN_MS?AGENT_ERR_TIMEOUT:AGENT_OK;
}

/* UTF-8 exact phrase search; ASCII letters are case-insensitive. */
static unsigned fold(unsigned c) { return c>='A' && c<='Z'?c+32:c; }
static const char *find(const char *text,const char *query)
{
    if(!text) return NULL;
    for(const char *p=text;*p;++p) {
        size_t i=0;
        while(query[i] && p[i] && fold((unsigned char)p[i])==fold((unsigned char)query[i])) ++i;
        if(!query[i]) return p;
    }
    return NULL;
}
static const char *match(const cJSON *root,const char *query,const char **text,const char **answer)
{
    const cJSON *content=cJSON_GetObjectItemCaseSensitive(root,"content");
    const cJSON *messages=cJSON_GetObjectItemCaseSensitive(content,"messages");
    const char *at=NULL;
    if(answer)*answer=NULL;
    for(const cJSON *m=messages?messages->child:NULL;m;m=m->next) {
        const char *role=agent_json_string(m,"role");
        if(!role || (!strcmp(role,"tool"))) continue;
        if(at) {
            if(!strcmp(role,"user"))break;
            const char *value=agent_json_string(m,"content");
            if(!strcmp(role,"assistant") && value && *value)*answer=value;
            continue;
        }
        *text=agent_json_string(m,"content");
        at=find(*text,query);
        if(at && (!answer || strcmp(role,"user")))return at;
    }
    return at;
}
static size_t excerpt(const char *text,const char *at,char *output,size_t cap)
{
    size_t start=(size_t)(at-text)>48?(size_t)(at-text)-48:0;
    while(((unsigned char)text[start]&0xc0)==0x80) ++start;
    size_t end=strlen(text); if(end>start+cap-1) end=start+cap-1;
    while(end>start && ((unsigned char)text[end]&0xc0)==0x80) --end;
    memcpy(output,text+start,end-start); output[end-start]=0;
    return end-start;
}
typedef struct {
    agent_context_t *context;
    const char *query;
    uint64_t before,start;
    unsigned limit,count,total;
    agent_record_t hits[3];
} search_t;
static agent_err_t search_record(void *ctx,const agent_record_t *r,const char *data)
{
    search_t *s=ctx;
    agent_err_t error=scan_check(s->context,s->start); if(error) return error;
    if(r->kind!=AGENT_WAL_EVENT || (s->before && r->seq>=s->before)) return AGENT_OK;
    cJSON *root=agent_json_parse(data,r->length); if(!root) return AGENT_ERR_CORRUPT;
    const char *id=agent_json_string(root,"event_id"),*text=NULL;
    bool found=(id && !strcmp(id,s->query)) || match(root,s->query,&text,NULL);
    if(found) {
        ++s->total;
        if(s->count==s->limit) { memmove(s->hits,s->hits+1,(s->count-1)*sizeof(*s->hits)); --s->count; }
        s->hits[s->count++]=*r;
    }
    cJSON_Delete(root); return AGENT_OK;
}
agent_err_t agent_context_search(agent_context_t *c,const char *query,uint64_t before,unsigned limit,char *output,size_t cap)
{
    if(!query || !*query || strlen(query)>128 || !agent_utf8_valid(query,strlen(query)) || !limit || limit>3)
        return AGENT_ERR_ARGUMENT;
    if(c->prompt_locked) return AGENT_ERR_BUSY;
    search_t s={.context=c,.query=query,.before=before,.start=scan_start(c),.limit=limit};
    agent_err_t error=agent_wal_iterate(&c->wal,0,c->scratch,c->capacity,search_record,&s);
    if(error) return error;
    agent_json_writer_t w; agent_json_writer_init(&w,output,cap); agent_json_raw(&w,"{\"hits\":[");
    unsigned emitted=0; uint64_t next=0;
    for(unsigned i=s.count;i;--i) {
        error=scan_check(c,s.start); if(error) return error;
        const agent_record_t *r=&s.hits[i-1];
        error=agent_wal_read(&c->wal,r,c->scratch,c->capacity); if(error) return error;
        cJSON *root=agent_json_parse(c->scratch,r->length); if(!root) return AGENT_ERR_CORRUPT;
        const char *text=NULL,*answer=NULL,*at=match(root,query,&text,&answer);
        if(!at) {
            cJSON *content=cJSON_GetObjectItemCaseSensitive(root,"content");
            if(!content || !cJSON_PrintPreallocated(content,c->scratch,(int)c->capacity,false)) { cJSON_Delete(root); return AGENT_ERR_CORRUPT; }
            text=at=c->scratch;
        }
        char snippet[241],item[1792];
        if(answer) {
            /* A question alone is not useful recall. Retain its last spoken
             * answer in the same 240-byte excerpt, without crossing a later
             * user message or including tool payloads. Preserve UTF-8. */
            memcpy(snippet,"user: ",6);
            size_t n=6+excerpt(text,at,snippet+6,113);
            memcpy(snippet+n,"\nassistant: ",12);n+=12;
            const char *answer_at=find(answer,query);
            excerpt(answer,answer_at?answer_at:answer,snippet+n,sizeof(snippet)-n);
        } else excerpt(text,at,snippet,sizeof(snippet));
        agent_json_writer_t hit; agent_json_writer_init(&hit,item,sizeof(item));
        agent_json_raw(&hit,"{\"event_id\":"); agent_json_quote(&hit,agent_json_string(root,"event_id"));
        agent_json_printf(&hit,",\"record_seq\":%llu,\"excerpt\":",(unsigned long long)r->seq);
        agent_json_quote(&hit,snippet); agent_json_raw(&hit,"}"); cJSON_Delete(root);
        if(hit.error) return hit.error;
        if(w.used+hit.used+80>=cap) break;
        if(emitted++) agent_json_raw(&w,",");
        agent_json_raw(&w,item); next=r->seq;
    }
    agent_json_printf(&w,"],\"more\":%s,\"next_before\":%llu}",s.total>emitted?"true":"false",(unsigned long long)next);
    if(!emitted && s.total) return AGENT_ERR_LIMIT;
    return w.error;
}

typedef struct { agent_context_t *context; const char *device; uint64_t through,start; bool found; } source_t;
static agent_err_t summary_source(void *ctx,const agent_record_t *r,const char *data)
{
    source_t *s=ctx;
    agent_err_t error=scan_check(s->context,s->start); if(error) return error;
    if(r->kind!=AGENT_WAL_EVENT) return AGENT_OK;
    cJSON *root=agent_json_parse(data,r->length); if(!root) return AGENT_ERR_CORRUPT;
    uint64_t seq; const char *device=agent_json_string(root,"device_id"),*type=agent_json_string(root,"type");
    if(device && type && !strcmp(device,s->device) && !strcmp(type,"turn") &&
       agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"device_seq"),AGENT_SEQ_MAX,&seq) && seq==s->through) s->found=true;
    cJSON_Delete(root); return AGENT_OK;
}
static agent_err_t find_summary_source(source_t *source)
{
    agent_context_t *c=source->context;
    if(!c->wal.ready)return AGENT_ERR_STORAGE;
    /* The existing index is rebuilt after open/compaction. Re-read and CRC
     * check the source, including its device identity; never trust a sequence
     * number alone. Most summaries refer to the newest local turn. Older
     * retained sources still use the complete WAL scan. */
    for(unsigned i=c->recent_count;i;--i) {
        agent_err_t error=scan_check(c,source->start);
        const agent_record_t *record=&c->recent[i-1];
        if(!error)error=agent_wal_read(&c->wal,record,c->scratch,c->capacity);
        if(!error)error=summary_source(source,record,c->scratch);
        if(error || source->found)return error;
    }
    return agent_wal_iterate(&c->wal,0,c->scratch,c->capacity,summary_source,source);
}
agent_err_t agent_context_summary_set(agent_context_t *c,const char *text,uint64_t through)
{
    if(!text || !*text || strlen(text)>AGENT_SUMMARY_MAX || !agent_utf8_valid(text,strlen(text))) return AGENT_ERR_ARGUMENT;
    if(c->prompt_locked) return AGENT_ERR_BUSY;
    char copy[AGENT_SUMMARY_MAX+1]; strcpy(copy,text);
    source_t source={.context=c,.device=c->device,.through=through?through:c->last_turn_seq,.start=scan_start(c)};
    agent_err_t error=find_summary_source(&source);
    if(error || !source.found) return error?error:AGENT_ERR_NOT_FOUND;
    /* Only controls need six-byte escapes; UTF-8 is copied unchanged. Keep
     * the fixed envelope/sequence allowance without a 6x text allocation. */
    size_t capacity=80;
    for(const unsigned char *p=(const unsigned char *)copy;*p;++p)
        capacity+=*p<0x20?6u:(*p=='"' || *p=='\\')?2u:1u;
    char *content=malloc(capacity); if(!content) return AGENT_ERR_MEMORY;
    agent_json_writer_t w; agent_json_writer_init(&w,content,capacity);
    agent_json_raw(&w,"{\"text\":"); agent_json_quote(&w,copy);
    agent_json_printf(&w,",\"through_seq\":%llu}",(unsigned long long)source.through);
    error=w.error?w.error:agent_context_emit(c,"summary","assistant",content,NULL,0);
    free(content); return error;
}
agent_err_t agent_context_summary_get(agent_context_t *c,char *output,size_t cap)
{
    const agent_summary_t *s=&c->summary;
    agent_json_writer_t w; agent_json_writer_init(&w,output,cap);
    agent_json_printf(&w,"{\"present\":%s,\"through_event_id\":",s->through?"true":"false");
    char id[64]; snprintf(id,sizeof(id),"%s:%020llu",s->device,(unsigned long long)s->through);
    agent_json_quote(&w,s->through?id:""); agent_json_raw(&w,",\"text\":");
    if(w.error || cap-w.used<40) return AGENT_ERR_LIMIT;
    size_t n=0,encoded=0,budget=cap-w.used-40;
    while(s->text[n]) {
        unsigned char ch=(unsigned char)s->text[n];
        size_t bytes=ch<0x20?6:ch=='"' || ch=='\\'?2:1;
        if(encoded+bytes>budget) break;
        encoded+=bytes; ++n;
    }
    while(n && ((unsigned char)s->text[n]&0xc0)==0x80) --n;
    char text[AGENT_SUMMARY_MAX+1]; memcpy(text,s->text,n); text[n]=0;
    agent_json_quote(&w,text); agent_json_printf(&w,",\"truncated\":%s}",s->text[n]?"true":"false");
    return w.error;
}
