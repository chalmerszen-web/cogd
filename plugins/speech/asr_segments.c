#include "asr_segments.h"
#include <string.h>

enum { STOPPED=1,COMMITTED=2,FINAL=4,ANNOUNCED=8 };

void agent_asr_segments_init(agent_asr_segments_t *s,char *text,size_t capacity)
{
    *s=(agent_asr_segments_t){.text=text,.capacity=(uint16_t)(capacity>AGENT_INPUT_MAX+1?AGENT_INPUT_MAX+1:capacity)};
    if(text && capacity)text[0]=0;
}
bool agent_asr_segments_settled(const agent_asr_segments_t *s)
{
    if(!s || s->error || !s->count)return false;
    for(unsigned i=0;i<s->count;++i)if(!(s->items[i].flags&FINAL))return false;
    return true;
}
static uint16_t confirmed_prefix(const agent_asr_segments_t *s)
{
    uint16_t bytes=0;
    for(unsigned i=0;i<s->count && (s->items[i].flags&FINAL);++i)bytes+=s->items[i].bytes;
    return bytes;
}
static bool equal(const cJSON *root,const char *key,const char *value)
{
    const char *text=agent_json_string(root,key);
    return text && !strcmp(text,value);
}
static agent_err_t collect(agent_asr_segments_t *s,const cJSON *root,size_t samples,agent_asr_segment_update_t *update)
{
    if(!s->text || !s->capacity || !root || !update)return AGENT_ERR_ARGUMENT;
    *update=(agent_asr_segment_update_t){0};
    if(s->error)return s->error;
    if(!samples || samples>160000)return AGENT_ERR_LIMIT;
    const char *type=agent_json_string(root,"type");
    if(!type)return AGENT_ERR_PROTOCOL;
    bool announcement=!strcmp(type,"conversation.item.created");
    const cJSON *item=announcement?cJSON_GetObjectItemCaseSensitive(root,"item"):root;
    const char *id=agent_json_string(item,announcement?"id":"item_id");
    if(!id || !*id || strlen(id)>64)return AGENT_ERR_PROTOCOL;
    unsigned index=0;
    while(index<s->count && strcmp(s->items[index].id,id))++index;
    if(!strcmp(type,"input_audio_buffer.speech_started")) {
        uint64_t start;
        if(index<s->count || !agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"audio_start_ms"),10000,&start) ||
           start>samples/16)return AGENT_ERR_PROTOCOL;
        if(s->count && (!(s->items[s->count-1].flags&STOPPED) || start<s->items[s->count-1].end_ms))return AGENT_ERR_PROTOCOL;
        if(s->count==AGENT_ASR_SEGMENT_MAX)return AGENT_ERR_LIMIT;
        agent_asr_segment_t *part=&s->items[s->count++];
        strcpy(part->id,id);part->start_ms=(uint16_t)start;
        s->text[s->used]=0; /* A new segment invalidates the old preview. */
        *update=(agent_asr_segment_update_t){.revision=s->count,.confirmed_bytes=confirmed_prefix(s),.notify=true};
        return AGENT_OK;
    }
    if(index==s->count)return AGENT_ERR_PROTOCOL;
    agent_asr_segment_t *part=&s->items[index];
    if(announcement) {
        const cJSON *content=cJSON_GetObjectItemCaseSensitive(item,"content");
        if(part->flags&ANNOUNCED || !equal(item,"type","message") || !equal(item,"role","assistant") ||
           !equal(item,"status","in_progress") || !cJSON_IsArray(content) || cJSON_GetArraySize(content)!=1 ||
           !equal(cJSON_GetArrayItem(content,0),"type","input_audio"))return AGENT_ERR_PROTOCOL;
        const cJSON *text=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content,0),"transcript");
        if(text && !cJSON_IsNull(text))return AGENT_ERR_PROTOCOL;
        part->flags|=ANNOUNCED;return AGENT_OK;
    }
    if(!strcmp(type,"input_audio_buffer.speech_stopped")) {
        uint64_t end;
        if(part->flags&STOPPED || !agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"audio_end_ms"),10000,&end) ||
           end<=part->start_ms || end>samples/16)return AGENT_ERR_PROTOCOL;
        part->end_ms=(uint16_t)end;part->flags|=STOPPED;return AGENT_OK;
    }
    if(!strcmp(type,"input_audio_buffer.committed")) {
        if(!(part->flags&STOPPED) || part->flags&COMMITTED)return AGENT_ERR_PROTOCOL;
        const cJSON *previous=cJSON_GetObjectItemCaseSensitive(root,"previous_item_id");
        if(previous && !cJSON_IsNull(previous) && (!index || !cJSON_IsString(previous) ||
           strcmp(previous->valuestring,s->items[index-1].id)))return AGENT_ERR_PROTOCOL;
        part->flags|=COMMITTED;return AGENT_OK;
    }
    bool final=!strcmp(type,"conversation.item.input_audio_transcription.completed");
    if(!final && strcmp(type,"conversation.item.input_audio_transcription.text"))return AGENT_ERR_PROTOCOL;
    uint64_t channel;
    if(part->flags&FINAL || (final && !(part->flags&COMMITTED)) ||
       !agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"content_index"),0,&channel))return AGENT_ERR_PROTOCOL;
    const char *text=agent_json_string(root,final?"transcript":"text");
    const char *stash=final?"":agent_json_string(root,"stash");
    if(!text || !stash || (final && *text && !text[strspn(text," \t\r\n")]))return AGENT_ERR_PROTOCOL;
    size_t a=strlen(text),b=strlen(stash);
    if(!agent_utf8_valid(text,a) || !agent_utf8_valid(stash,b))return AGENT_ERR_PROTOCOL;
    size_t stored=a+(a && final?1u:0u);
    if(stored+b>=(size_t)s->capacity-s->used)return AGENT_ERR_LIMIT;
    if(final) {
        size_t at=0;for(unsigned i=0;i<index;++i)at+=s->items[i].bytes;
        /* Final text can arrive after a later segment's final. Insert by
         * original source order; partial previews never enter this prefix. */
        if(stored) {
            memmove(s->text+at+stored,s->text+at,s->used-at);
            memcpy(s->text+at,text,a);s->text[at+a]='\n';
        }
        part->bytes=(uint16_t)stored;part->flags|=FINAL;s->used+=(uint16_t)stored;
        s->text[s->used]=0;
        update->notify=true;update->revision=s->count;
        update->confirmed_bytes=confirmed_prefix(s);
        update->settled=agent_asr_segments_settled(s);
        /* An identified empty final closes that acoustic segment only. It
         * cannot extend an earlier text's endpoint or revive its candidate. */
        if(update->settled && s->items[s->count-1].bytes)update->end_ms=s->items[s->count-1].end_ms;
    } else {
        /* Never display a later prefix as if missing earlier segments had
         * already been understood. Its start notification still revokes. */
        if(index+1!=s->count)return AGENT_OK;
        for(unsigned i=0;i<index;++i)if(!(s->items[i].flags&FINAL))return AGENT_OK;
        memcpy(s->text+s->used,text,a);memcpy(s->text+s->used+a,stash,b+1);
        *update=(agent_asr_segment_update_t){.revision=s->count,
            .confirmed_bytes=(uint16_t)(s->used+a),.notify=true,.partial=true};
    }
    return AGENT_OK;
}
agent_err_t agent_asr_segments_receive(agent_asr_segments_t *s,const cJSON *root,size_t samples,agent_asr_segment_update_t *update)
{
    if(!s)return AGENT_ERR_ARGUMENT;
    agent_err_t error=collect(s,root,samples,update);
    if(error) {
        if(!s->error)s->error=error;
        if(s->text && s->capacity)s->text[0]=0;
        if(update)*update=(agent_asr_segment_update_t){0};
    }
    return error;
}
