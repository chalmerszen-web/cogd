#include "engine_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char flash[2*1024*1024];
static char scratch[AGENT_ENGINE_BUFFER_SIZE],read_buffer[sizeof(scratch)],print_buffer[sizeof(scratch)];
static uint64_t reserved;
static agent_err_t rd(void *p,size_t off,void *out,size_t n)
{ (void)p;assert(off+n<=sizeof(flash));memcpy(out,flash+off,n);return AGENT_OK; }
static agent_err_t wr(void *p,size_t off,const void *in,size_t n)
{
    (void)p;assert(off+n<=sizeof(flash));
    for(size_t i=0;i<n;++i) {
        unsigned char byte=((const unsigned char *)in)[i];assert((flash[off+i]&byte)==byte);flash[off+i]&=byte;
    }
    return AGENT_OK;
}
static agent_err_t er(void *p,size_t off,size_t n)
{ (void)p;assert(!(off%4096) && !(n%4096));memset(flash+off,255,n);return AGENT_OK; }
static const agent_flash_ops_t storage={rd,wr,er,NULL,sizeof(flash),4096};
static agent_err_t reserve(void *p,uint64_t *first,uint64_t *last)
{ (void)p;*first=reserved+1;reserved+=128;*last=reserved;return AGENT_OK; }
static agent_context_t fresh(void)
{
    return (agent_context_t){.device="length",.user="user",.session="session",.reserve=reserve,
        .scratch=scratch,.capacity=sizeof(scratch)};
}
static void check_lengths(agent_context_t *c)
{
    for(unsigned i=0;i<c->recent_count;++i) {
        assert(!agent_wal_read(&c->wal,&c->recent[i],read_buffer,sizeof(read_buffer)));
        cJSON *root=agent_json_parse(read_buffer,c->recent[i].length);assert(root);
        cJSON *content=cJSON_GetObjectItemCaseSensitive(root,"content");
        cJSON *messages=cJSON_GetObjectItemCaseSensitive(content,"messages");
        assert(cJSON_PrintPreallocated(messages,print_buffer,sizeof(print_buffer),false));
        assert(c->recent_message_bytes[i]==strlen(print_buffer));cJSON_Delete(root);
    }
}
static void sized(agent_context_t *c,size_t expected)
{
    agent_engine_t e={.context=c};
    assert(!esp_agent_engine_memory_prepare_input(&e,"short") && c->capacity==expected);
    assert(!esp_agent_engine_memory_release(&e));c->scratch=scratch;c->capacity=sizeof(scratch);
}
static void saved_records(agent_context_t *c)
{
    const char *path=getenv("AGENT_SCRATCH_RECORDS");if(!path)return;
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);
    assert(n>0 && n<4000000 && !fseek(f,0,SEEK_SET));char *text=malloc((size_t)n+1);assert(text);
    assert(fread(text,1,(size_t)n,f)==(size_t)n && !fclose(f));text[n]=0;
    cJSON *rows=cJSON_ParseWithLength(text,(size_t)n);assert(cJSON_IsArray(rows));free(text);
    memset(flash,255,sizeof(flash));agent_wal_t wal;assert(!agent_wal_format(&wal,&storage));
    static char device[33],user[65],session[65];unsigned count=0;
    const cJSON *row;
    cJSON_ArrayForEach(row,rows) {
        const char *body=agent_json_string(row,"body");assert(body);
        cJSON *root=agent_json_parse(body,strlen(body));assert(root);
        if(!count) {
            const char *d=agent_json_string(root,"device_id"),*u=agent_json_string(root,"user_id"),*s=agent_json_string(root,"session_id");
            assert(d && strlen(d)<sizeof(device) && u && strlen(u)<sizeof(user) && s && strlen(s)<sizeof(session));
            strcpy(device,d);strcpy(user,u);strcpy(session,s);
        }
        assert(!strcmp(device,agent_json_string(root,"device_id")) && !strcmp(user,agent_json_string(root,"user_id")) &&
               !strcmp(session,agent_json_string(root,"session_id")));
        cJSON_Delete(root);assert(!agent_wal_append(&wal,AGENT_WAL_EVENT,body,strlen(body),NULL));++count;
    }
    cJSON_Delete(rows);assert(count==128);
    *c=fresh();c->device=device;c->user=user;c->session=session;
    assert(!agent_context_open(c,&storage));check_lengths(c);
    size_t raw=0,canonical=0;
    for(unsigned i=0;i<c->recent_count;++i) {
        if(c->recent[i].length>raw)raw=c->recent[i].length;
        if(c->recent_message_bytes[i]>canonical)canonical=c->recent_message_bytes[i];
    }
    assert(raw<8192 && canonical+5<=8192);sized(c,8192);
    printf("Saved actual records:128, maximum raw=%zu, canonical=%zu, preparation8192B, budgets unchanged PASS\n",raw,canonical);
}
int main(void)
{
    memset(flash,255,sizeof(flash));agent_wal_t wal;assert(!agent_wal_format(&wal,&storage));
    static agent_context_t context,reopened;context=fresh();assert(!agent_context_open(&context,&storage));
    static char content[20000],text[18500];
    for(unsigned i=0;i<140;++i) {
        memset(text,'a'+i%26,20+i);text[20+i]=0;
        snprintf(content,sizeof(content),"{\"messages\":[{\"role\":\"user\",\"content\":\"%s\"},{\"role\":\"assistant\",\"content\":\"reply\\n粤語\"}]}",text);
        assert(!agent_context_emit(&context,"turn","assistant",content,NULL,0));
    }
    assert(context.recent_count==128 && context.recent[0].seq==13);
    check_lengths(&context);sized(&context,8192);
    reopened=fresh();assert(!agent_context_open(&reopened,&storage));
    assert(!memcmp(context.recent,reopened.recent,sizeof(context.recent)));
    assert(!memcmp(context.recent_message_bytes,reopened.recent_message_bytes,sizeof(context.recent_message_bytes)));
    check_lengths(&reopened);
    uint64_t generation=reopened.wal.generation;
    assert(!agent_context_compact(&reopened) && reopened.wal.generation==generation+1);
    check_lengths(&reopened);sized(&reopened,8192);
    /* Remote/noncanonical numeric payload: measure the actual parsed tree,
     * not raw message spans or an assumption that canonical text is shorter. */
    snprintf(content,sizeof(content),"{\"schema\":\"agent.context.event/1\",\"device_id\":\"remote\",\"event_id\":\"remote:00000000000000000001\",\"user_id\":\"user\",\"session_id\":\"session\",\"device_seq\":1,\"lamport\":500,\"type\":\"turn\",\"actor\":{},\"policy\":{},\"parents\":[],\"content\":{\"messages\":[{\"role\":\"user\",\"content\":\"hi\",\"numbers\":[1e20,2e20,3e20]},{\"role\":\"assistant\",\"content\":\"ok\"}]}}");
    assert(!agent_wal_append(&reopened.wal,AGENT_WAL_EVENT,content,strlen(content),NULL));
    context=fresh();assert(!agent_context_open(&context,&storage));check_lengths(&context);
    unsigned last=context.recent_count-1;
    assert(!agent_wal_read(&context.wal,&context.recent[last],read_buffer,sizeof(read_buffer)));
    cJSON *root=agent_json_parse(read_buffer,context.recent[last].length);assert(root);
    cJSON *messages=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"content"),"messages");
    assert(cJSON_PrintPreallocated(messages,print_buffer,sizeof(print_buffer),false));
    assert(strstr(print_buffer,"1e+20"));cJSON_Delete(root);
    for(unsigned n=12000;n<=18000;n+=6000) {
        memset(text,'x',n);text[n]=0;
        snprintf(content,sizeof(content),"{\"messages\":[{\"role\":\"user\",\"content\":\"u\"},{\"role\":\"assistant\",\"content\":\"%s\"}]}",text);
        assert(!agent_context_emit(&context,"turn","assistant",content,NULL,0));check_lengths(&context);
        sized(&context,n==12000?16384:AGENT_ENGINE_BUFFER_SIZE);
    }
    /* Corrupt bytes remain fatal even when their cached length is valid. */
    atomic_bool cancel=ATOMIC_VAR_INIT(false);
    assert(!agent_context_select(&context,context.wal.next_seq,&cancel));
    agent_record_t record=context.recent[context.prompt_start];flash[record.offset+32]^=1;
    assert(agent_context_replay(&context,agent_json_write_bytes,NULL)==AGENT_ERR_CORRUPT);
    flash[record.offset+32]^=1;agent_context_release(&context);
    saved_records(&context);
    puts("Canonical length index: shift/reopen/compact, raw numeric expansion,8/16/full capacities and unchanged CRC PASS");
}
