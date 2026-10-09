#include "prefetch.h"
#include <string.h>
void agent_prefetch_init(agent_prefetch_t *p,agent_realtime_t *q,void *memory,size_t capacity,
                         char *input,size_t input_capacity)
{
    memset(p,0,sizeof(*p));p->session=q;p->input=input;p->input_capacity=input_capacity;
    if(capacity>AGENT_PREFETCH_BYTES)capacity=AGENT_PREFETCH_BYTES;
    agent_speech_draft_init(&p->audio,memory,capacity);
    if(input_capacity)input[0]=0;
}
static void invalidate(agent_prefetch_t *p)
{
    if(p->attempted && !p->invalid) {
        p->invalid=true;++p->discarded;agent_speech_draft_reset(&p->audio);
    }
}
void agent_prefetch_preview(agent_prefetch_t *p,const char *text)
{
    if(p->committed)return;
    p->proposed=agent_speech_guess(text,false);
    if(p->attempted && p->proposed && p->proposed!=p->guess)invalidate(p);
}
bool agent_prefetch_prepare(agent_prefetch_t *p,unsigned uploaded)
{
    if(p->attempted || p->unusable || !p->proposed || !uploaded ||
       uploaded>AGENT_RT_INPUT_MAX || p->session->message)return false;
    p->attempted=true;p->guess=p->proposed;p->bound_samples=uploaded;return true;
}
agent_err_t agent_prefetch_pcm(agent_prefetch_t *p,const int16_t *pcm,size_t count)
{
    if(!p->requested || p->released)return AGENT_ERR_PROTOCOL;
    if(p->invalid || p->unusable)return AGENT_OK;
    agent_err_t error=agent_speech_draft_write(&p->audio,pcm,count);
    if(error==AGENT_ERR_FULL) {p->unusable=true;return AGENT_OK;}
    if(!error && count && !p->first_pcm_at)
        p->first_pcm_at=(unsigned)p->session->speech->now_ms(p->session->speech->clock_ctx);
    return error;
}
static bool thinking_end(const char *text,size_t n)
{
    while(n && strchr(" \t\r\n",text[n-1]))--n;
    return (n>=9 && !memcmp(text+n-9,"我想想",9)) ||
           (n>=12 && !memcmp(text+n-12,"我諗諗先",12));
}
static bool late_stop(const agent_prefetch_t *p,const char *text)
{
    /* A completed neutral receipt can stream before punctuation arrives.
     * Permit one terminal mark only; new words cannot revise audible text. */
    if(p->guess!=AGENT_GUESS_THINK || !p->released || p->text_done ||
       !thinking_end(p->text,p->text_length))return false;
    text+=strspn(text," \t\r\n");
    if(*text=='.' || *text=='!')++text;
    else if(!strncmp(text,"。",3) || !strncmp(text,"！",3))text+=3;
    else return false;
    return !text[strspn(text," \t\r\n")];
}
agent_err_t agent_prefetch_event(agent_prefetch_t *p,const cJSON *root)
{
    const char *type=agent_json_string(root,"type");if(!type)return AGENT_ERR_PROTOCOL;
    if(!strcmp(type,"input_audio_buffer.committed")) {
        const char *id=agent_json_string(root,"item_id");
        if(!p->commit_sent || p->committed || !id || !*id || strlen(id)>=sizeof(p->input_id))return AGENT_ERR_PROTOCOL;
        strcpy(p->input_id,id);p->committed=true;
    } else if(!strcmp(type,"conversation.item.input_audio_transcription.completed")) {
        const char *id=agent_json_string(root,"item_id"),*text=agent_json_string(root,"transcript");
        if(!p->committed || !id || strcmp(id,p->input_id) || !text || !agent_utf8_valid(text,strlen(text)))return AGENT_ERR_PROTOCOL;
        if(p->final_seen)return strcmp(text,p->input)?AGENT_ERR_PROTOCOL:AGENT_OK;
        if(strlen(text)>=p->input_capacity)return AGENT_ERR_LIMIT;
        strcpy(p->input,text);p->final_seen=true;
        p->incomplete_input=!text[strspn(text," \t\r\n")];
        if(p->attempted && agent_speech_guess(text,true)!=p->guess)invalidate(p);
    } else if(!strncmp(type,"response.",9)) {
        if(!p->requested || p->complete)return AGENT_ERR_PROTOCOL;
        if(!strcmp(type,"response.output_item.added")) {
            const char *kind=agent_json_string(cJSON_GetObjectItemCaseSensitive(root,"item"),"type");
            if(!kind || strcmp(kind,"message"))p->unusable=true;
        } else if(!strcmp(type,"response.audio_transcript.delta") || !strcmp(type,"response.text.delta")) {
            const char *text=agent_json_string(root,"delta");if(!text)return AGENT_ERR_PROTOCOL;
            if((p->released || p->text_done) && text[strspn(text," \t\r\n")] &&
               !late_stop(p,text))p->unusable=true;
            size_t n=strlen(text);
            if(n>AGENT_PREFETCH_TEXT-p->text_length)p->unusable=true;
            else {memcpy(p->text+p->text_length,text,n+1);p->text_length+=n;}
        } else if(!strcmp(type,"response.audio_transcript.done") && p->guess==AGENT_GUESS_ANSWER) {
            const char *id=agent_json_string(root,"response_id"),*text=agent_json_string(root,"transcript");
            if(p->text_done || !p->session->active || !id || strcmp(id,p->session->response_id) ||
               !text || strcmp(text,p->text))return AGENT_ERR_PROTOCOL;
            p->text_done=true;
        } else if(!strcmp(type,"response.done")) {
            const cJSON *output=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"response"),"output");
            const cJSON *item=cJSON_IsArray(output)?output->child:NULL;
            const char *kind=agent_json_string(item,"type");
            const cJSON *content=cJSON_GetObjectItemCaseSensitive(item,"content");
            const cJSON *part=cJSON_IsArray(content)?content->child:NULL;
            const char *part_type=agent_json_string(part,"type"),*text=agent_json_string(part,"transcript");
            if(!item || item->next || !kind || strcmp(kind,"message") || !part || part->next ||
               !part_type || strcmp(part_type,"audio") || !text || strcmp(text,p->text))p->unusable=true;
            p->complete=true;
        }
    }
    return p->released && (p->unusable || p->invalid)?AGENT_ERR_PROTOCOL:AGENT_OK;
}
bool agent_prefetch_input_ready(const agent_prefetch_t *p)
{ return !p->session->error && p->committed && p->final_seen && !p->incomplete_input && p->input[0]; }
bool agent_prefetch_output_ready(const agent_prefetch_t *p)
{
    size_t n=p->text_length;
    while(n && strchr(" \t\r\n",p->text[n-1]))--n;
    bool sentence=n && (p->text[n-1]=='!' || p->text[n-1]=='.' ||
        (n>=3 && (!memcmp(p->text+n-3,"。",3) || !memcmp(p->text+n-3,"！",3))));
    bool receipt=p->guess==AGENT_GUESS_THINK && thinking_end(p->text,n);
    return !p->session->error && (p->complete || sentence || receipt) && !p->invalid && !p->unusable &&
        p->audio.samples && n &&
        agent_speech_guess_reply(p->guess,p->text);
}
bool agent_prefetch_ready(const agent_prefetch_t *p)
{ return agent_prefetch_input_ready(p) && agent_prefetch_output_ready(p); }
