#include "asr_segments.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static agent_asr_segments_t state;
static agent_asr_segment_update_t update;
static char text[2049];
static void reset(void) { agent_asr_segments_init(&state,text,sizeof(text)); }
static agent_err_t receive(const char *type,const char *fields)
{
    char json[4096];int n=snprintf(json,sizeof(json),"{\"type\":\"%s\",%s}",type,fields);
    assert(n>0 && (size_t)n<sizeof(json));
    cJSON *root=agent_json_parse(json,(size_t)n);assert(root);
    agent_err_t error=agent_asr_segments_receive(&state,root,160000,&update);
    cJSON_Delete(root);
    if(error)assert(!text[0] && !agent_asr_segments_settled(&state) && !update.notify);
    return error;
}
static agent_err_t start(const char *id,unsigned ms)
{
    char fields[160];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\",\"audio_start_ms\":%u",id,ms);
    return receive("input_audio_buffer.speech_started",fields);
}
static agent_err_t stop(const char *id,unsigned ms)
{
    char fields[160];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\",\"audio_end_ms\":%u",id,ms);
    return receive("input_audio_buffer.speech_stopped",fields);
}
static agent_err_t commit(const char *id)
{
    char fields[160];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\"",id);
    return receive("input_audio_buffer.committed",fields);
}
static agent_err_t final(const char *id,const char *words)
{
    char fields[2300];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\",\"content_index\":0,\"transcript\":\"%s\"",id,words);
    return receive("conversation.item.input_audio_transcription.completed",fields);
}
static void phrase(const char *id,unsigned begin,unsigned end,const char *words)
{
    assert(!start(id,begin) && update.notify && !update.settled && !update.end_ms);
    assert(!stop(id,end) && !commit(id));
    assert(!final(id,words) && update.settled && update.end_ms==end);
}
static void correction(void)
{
    reset();
    phrase("a",351,3422,"请把灯调成蓝色，不对。");
    assert(agent_asr_segments_settled(&state) && update.revision==1);
    assert(!start("b",3422));
    assert(update.notify && update.revision==2 && !update.settled && !agent_asr_segments_settled(&state));
    assert(!receive("conversation.item.input_audio_transcription.text",
        "\"item_id\":\"b\",\"content_index\":0,\"text\":\"不要\",\"stash\":\"蓝色。\""));
    assert(update.partial && !strcmp(text,"请把灯调成蓝色，不对。\n不要蓝色。"));
    assert(!stop("b",4669) && !commit("b") && !final("b","不要蓝色。"));
    phrase("c",4669,5916,"改成绿色。");
    assert(!strcmp(text,"请把灯调成蓝色，不对。\n不要蓝色。\n改成绿色。\n"));
    assert(final("a","旧蓝色")==AGENT_ERR_PROTOCOL); /* No stale final may revise output. */
}
static void delayed(void)
{
    reset();assert(!start("a",100) && !stop("a",200) && !commit("a"));
    assert(!start("b",200));
    assert(!receive("conversation.item.input_audio_transcription.text","\"item_id\":\"b\",\"content_index\":0,\"text\":\"later\",\"stash\":\"\""));
    assert(!update.notify && !text[0]); /* Missing earlier final cannot be hidden. */
    assert(!stop("b",300) && !commit("b") && !final("b","later"));
    assert(!update.settled && !strcmp(text,"later\n"));
    assert(!final("a","earlier") && update.settled && update.revision==2 && update.end_ms==300);
    assert(!strcmp(text,"earlier\nlater\n"));
    assert(!start("c",300) && !update.settled && !agent_asr_segments_settled(&state));
}
static void rejected(void)
{
    reset();assert(!start("a",100));assert(start("b",101)==AGENT_ERR_PROTOCOL);
    reset();assert(!start("a",100));assert(stop("a",99)==AGENT_ERR_PROTOCOL);
    reset();assert(start("a",10001)==AGENT_ERR_PROTOCOL);
    reset();assert(!start("a",100));assert(commit("a")==AGENT_ERR_PROTOCOL);
    reset();assert(!start("a",100) && !stop("a",200));assert(final("a","missing commit")==AGENT_ERR_PROTOCOL);
    reset();phrase("a",100,200,"hello");assert(start("a",201)==AGENT_ERR_PROTOCOL);
    reset();phrase("a",100,200,"hello");assert(start("b",199)==AGENT_ERR_PROTOCOL);
    reset();assert(stop("unknown",20)==AGENT_ERR_PROTOCOL);
    reset();assert(!start("a",100) && !stop("a",200) && !commit("a"));assert(final("a"," ")==AGENT_ERR_PROTOCOL);
    reset();assert(!start("a",100) && !stop("a",200) && !commit("a"));assert(commit("a")==AGENT_ERR_PROTOCOL);
    reset();phrase("a",100,200,"hello");assert(!start("b",200) && !stop("b",300));
    assert(receive("input_audio_buffer.committed","\"item_id\":\"b\",\"previous_item_id\":\"foreign\"")==AGENT_ERR_PROTOCOL);
    reset();assert(receive("input_audio_buffer.speech_started","\"audio_start_ms\":0")==AGENT_ERR_PROTOCOL);
    reset();assert(!start("a",100));
    assert(receive("conversation.item.input_audio_transcription.text","\"item_id\":\"a\",\"content_index\":1,\"text\":\"hi\",\"stash\":\"\"")==AGENT_ERR_PROTOCOL);
}
static void limits(void)
{
    reset();
    for(unsigned i=0;i<AGENT_ASR_SEGMENT_MAX;++i) {
        char id[8];snprintf(id,sizeof(id),"i%u",i);phrase(id,i*100,i*100+100,"x");
    }
    assert(start("overflow",900)==AGENT_ERR_LIMIT);
    char full[2049];memset(full,'x',sizeof(full)-1);full[sizeof(full)-1]=0;
    reset();assert(!start("a",0) && !stop("a",1) && !commit("a"));
    assert(final("a",full)==AGENT_ERR_LIMIT); /* Includes the stored separator. */
    full[2047]=0;
    reset();phrase("a",0,1,full);assert(strlen(text)==2048);
    assert(!start("b",1) && !stop("b",2) && !commit("b"));assert(final("b","x")==AGENT_ERR_LIMIT);
    reset();
    /* The explicit uploaded source count is the only fault. */
    cJSON *root=cJSON_CreateObject();cJSON_AddStringToObject(root,"type","input_audio_buffer.speech_started");
    cJSON_AddStringToObject(root,"item_id","a");cJSON_AddNumberToObject(root,"audio_start_ms",2);
    assert(agent_asr_segments_receive(&state,root,16,&update)==AGENT_ERR_PROTOCOL && !text[0]);
    cJSON_Delete(root);
}
static void empty_segments(void)
{
    reset();assert(!start("noise",0) && !stop("noise",200) && !commit("noise"));
    assert(!final("noise","") && update.settled && !update.end_ms && !text[0] && !state.used);
    phrase("voice",200,1000,"hello");assert(!strcmp(text,"hello\n"));
    assert(!start("tail",1000) && !stop("tail",1200) && !commit("tail"));
    assert(!receive("conversation.item.input_audio_transcription.text",
        "\"item_id\":\"tail\",\"content_index\":0,\"text\":\"provisional\",\"stash\":\"\""));
    assert(!final("tail","") && update.settled && !update.end_ms && !strcmp(text,"hello\n"));
    assert(final("tail","")==AGENT_ERR_PROTOCOL); /* Still rejects duplicate finals. */
    reset();assert(!start("noise",0) && !stop("noise",200) && !commit("noise"));
    assert(!start("voice",200) && !stop("voice",1000) && !commit("voice"));
    assert(!final("voice","hello") && !update.settled);
    assert(!final("noise","") && update.settled && update.end_ms==1000 && !strcmp(text,"hello\n"));
}
int main(void)
{
    correction();delayed();rejected();limits();empty_segments();
    printf("asr_segments bytes=%zu all_checks_passed\n",sizeof(state));return 0;
}
