#include "asr_realtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static agent_asr_realtime_t session;
static agent_asr_segments_t segments;
static char transcript[2049],scratch[8192];
static atomic_bool cancelled;
static uint64_t ticks;
static unsigned closes,notifications,wire_round,wire_event;
static size_t split=7;
static FILE *output;
static struct { char stage[32],text[2049]; } seen[64];

static uint64_t now(void *ctx) { (void)ctx;return ticks; }
static void close_ws(void *ctx) { (void)ctx;++closes; }
static const agent_ws_ops_t ws={.close=close_ws};
static void observe(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(strcmp(stage,"asr_prefix") && strcmp(stage,"asr_partial") &&
       strcmp(stage,"asr_segment_pending") && strcmp(stage,"asr_segment_final"))return;
    assert(text==transcript && strlen(text)<session.capacity && agent_utf8_valid(text,strlen(text)));
    if(output) {
        cJSON *row=cJSON_CreateObject();assert(row);
        cJSON_AddNumberToObject(row,"round",wire_round);
        cJSON_AddNumberToObject(row,"receive_index",wire_event);
        cJSON_AddNumberToObject(row,"observed_ms",(double)ticks);
        cJSON_AddNumberToObject(row,"uploaded_samples",(double)session.input_samples);
        cJSON_AddNumberToObject(row,"revision",segments.count);
        cJSON_AddStringToObject(row,"stage",stage);cJSON_AddStringToObject(row,"text",text);
        char *json=cJSON_PrintUnformatted(row);assert(json);
        assert(fprintf(output,"%s\n",json)>0);cJSON_free(json);cJSON_Delete(row);
    } else {
        assert(notifications<64 && strlen(stage)<sizeof(seen[0].stage));
        strcpy(seen[notifications].stage,stage);strcpy(seen[notifications].text,text);
    }
    ++notifications;
}
static agent_speech_t speech={.scratch=scratch,.capacity=sizeof(scratch),
    .now_ms=now,.cancelled=&cancelled,.event=observe};
static void reset_limit(bool vad,bool wire,size_t capacity)
{
    ticks=0;closes=notifications=0;atomic_store(&cancelled,false);
    agent_asr_realtime_init(&session,&speech,&ws,transcript,capacity);
    if(vad)assert(!agent_asr_realtime_use_vad(&session,&segments));
    session.opened=true;session.deadline=180000;
    if(!wire) {session.created=session.ready=true;session.input_samples=160000;}
    assert(!transcript[0]);
}
static void reset(bool vad,bool wire) { reset_limit(vad,wire,sizeof(transcript)); }
static agent_err_t inject(const char *json)
{
    size_t n=strlen(json);
    for(size_t at=0;at<n;) {
        size_t count=n-at;if(count>split)count=split;
        agent_ws_chunk_t part={count,at?0:1,!at,at+count==n};
        agent_err_t error=agent_asr_realtime_receive(&session,json+at,&part);
        if(error)return error;
        at+=count;
    }
    return AGENT_OK;
}
static agent_err_t message(const char *type,const char *fields)
{
    char json[4096];int n=snprintf(json,sizeof(json),"{\"type\":\"%s\",\"event_id\":\"test\",%s}",type,fields);
    assert(n>0 && (size_t)n<sizeof(json));return inject(json);
}
static void start(const char *id,unsigned ms)
{
    char fields[128];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\",\"audio_start_ms\":%u",id,ms);
    assert(!message("input_audio_buffer.speech_started",fields));
}
static void commit(const char *id,unsigned ms)
{
    char fields[128];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\",\"audio_end_ms\":%u",id,ms);
    assert(!message("input_audio_buffer.speech_stopped",fields));
    snprintf(fields,sizeof(fields),"\"item_id\":\"%s\"",id);
    assert(!message("input_audio_buffer.committed",fields));
}
static void partial(const char *id,const char *prefix,const char *draft)
{
    char fields[512];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\",\"content_index\":0,\"text\":\"%s\",\"stash\":\"%s\"",id,prefix,draft);
    assert(!message("conversation.item.input_audio_transcription.text",fields));
}
static void final(const char *id,const char *text)
{
    char fields[512];snprintf(fields,sizeof(fields),"\"item_id\":\"%s\",\"content_index\":0,\"transcript\":\"%s\"",id,text);
    assert(!message("conversation.item.input_audio_transcription.completed",fields));
}
static void expect(const char *prefix,const char *preview,const char *stage)
{
    assert(notifications>=2);
    assert(!strcmp(seen[notifications-2].stage,"asr_prefix") && !strcmp(seen[notifications-2].text,prefix));
    assert(!strcmp(seen[notifications-1].stage,stage) && !strcmp(seen[notifications-1].text,preview));
    assert(!strcmp(transcript,preview)); /* Borrowed truncation must have been undone. */
}
static void promotion(bool vad)
{
    reset(vad,false);if(vad)start("a",0);
    partial("a","","Thanks");expect("","Thanks","asr_partial");
    unsigned previous=notifications;
    partial("a","Thanks","");expect("Thanks","Thanks","asr_partial");
    assert(notifications==previous+2); /* Same preview, new evidence. */
    partial("a","","");expect("","","asr_partial");
    partial("a","你好","，小言");expect("你好","你好，小言","asr_partial");
    partial("a","你好，小言","");expect("你好，小言","你好，小言","asr_partial");
    partial("a","请把灯","调成蓝色");expect("请把灯","请把灯调成蓝色","asr_partial");
    partial("a","请把灯","调成绿色");expect("请把灯","请把灯调成绿色","asr_partial");
    agent_asr_realtime_cancel(&session);assert(!transcript[0] && closes==1);
    reset(vad,false);assert(!notifications && !transcript[0]);
    if(vad)start("a",0);
    partial("a","","新的一轮");expect("","新的一轮","asr_partial");
}
static void ordered(void)
{
    reset(true,false);start("a",0);partial("a","你好","小言");
    commit("a",100);final("a","你好，小言。");
    expect("你好，小言。\n","你好，小言。\n","asr_segment_final");
    start("b",100);expect("你好，小言。\n","你好，小言。\n","asr_segment_pending");
    partial("b","请把","灯调成蓝色");
    expect("你好，小言。\n请把","你好，小言。\n请把灯调成蓝色","asr_partial");
    partial("b","","");expect("你好，小言。\n","你好，小言。\n","asr_partial");
    commit("b",200);final("b","");
    expect("你好，小言。\n","你好，小言。\n","asr_segment_final");
    assert(agent_asr_segments_settled(&segments));
}
static void missing_earlier(void)
{
    reset(true,false);start("a",0);partial("a","before"," draft");commit("a",100);
    start("b",100);expect("","","asr_segment_pending");
    unsigned before=notifications;
    partial("b","later"," unfinished");assert(notifications==before && !transcript[0]);
    commit("b",200);final("b","later");expect("","later\n","asr_segment_pending");
    start("c",200);expect("","later\n","asr_segment_pending");
    final("a","earlier");expect("earlier\nlater\n","earlier\nlater\n","asr_segment_pending");
    partial("c","last"," draft");expect("earlier\nlater\nlast","earlier\nlater\nlast draft","asr_partial");
    commit("c",300);final("c","last");
    expect("earlier\nlater\nlast\n","earlier\nlater\nlast\n","asr_segment_final");
}
static void faults(bool vad)
{
    reset(vad,false);if(vad)start("a",0);
    partial("a","", "draft");unsigned before=notifications;
    assert(message("conversation.item.input_audio_transcription.text",
        "\"item_id\":\"a\",\"content_index\":0,\"text\":\"missing stash\"")==AGENT_ERR_PROTOCOL);
    assert(notifications==before && !transcript[0] && closes==1);
    reset_limit(vad,false,7);if(vad)start("a",0);
    before=notifications;
    assert(message("conversation.item.input_audio_transcription.text",
        "\"item_id\":\"a\",\"content_index\":0,\"text\":\"你好\",\"stash\":\"言\"")==AGENT_ERR_LIMIT);
    assert(notifications==before && !transcript[0]);
    reset(vad,false);if(vad)start("a",0);
    before=notifications;
    assert(message("conversation.item.input_audio_transcription.text",
        "\"item_id\":\"a\",\"content_index\":0,\"text\":\"\xe4\",\"stash\":\"\"")==AGENT_ERR_JSON);
    assert(notifications==before && !transcript[0]);
    reset(vad,false);if(vad)start("a",0);
    before=notifications;atomic_store(&cancelled,true);
    assert(message("conversation.item.input_audio_transcription.text",
        "\"item_id\":\"a\",\"content_index\":0,\"text\":\"stale\",\"stash\":\"\"")==AGENT_ERR_CANCELLED);
    assert(notifications==before && !transcript[0]);
}
static void replay(const char *input,const char *target)
{
    FILE *file=fopen(input,"rb");assert(file);
    output=fopen(target,"wbx");assert(output);
    char line[8192];wire_round=wire_event=0;unsigned total=0;
    while(fgets(line,sizeof(line),file)) {
        assert(strchr(line,'\n'));
        cJSON *root=agent_json_parse(line,strlen(line));assert(root);
        uint64_t round;assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"round"),6,&round) && round);
        if(round!=wire_round) {
            assert(round==wire_round+1);
            if(wire_round) {
                assert(session.done && session.final && agent_asr_segments_settled(&segments));
                agent_asr_realtime_cancel(&session);assert(closes==1 && !transcript[0]);
            }
            reset(true,true);wire_round=(unsigned)round;wire_event=0;
        }
        const cJSON *clock=cJSON_GetObjectItemCaseSensitive(root,"observed_s");
        assert(cJSON_IsNumber(clock) && clock->valuedouble>=0);ticks=(uint64_t)(clock->valuedouble*1000);
        const cJSON *e=cJSON_GetObjectItemCaseSensitive(root,"event");
        const char *direction=agent_json_string(root,"direction"),*type=agent_json_string(e,"type");
        assert(direction && type);
        if(!strcmp(direction,"send")) {
            if(!strcmp(type,"session.update")) {assert(session.created && !session.updating);session.updating=true;}
            else if(!strcmp(type,"session.finish")) {assert(session.ready && !session.finishing);session.finishing=true;}
            else {
                uint64_t samples;assert(!strcmp(type,"input_audio_buffer.append") && session.ready && !session.finishing);
                assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(e,"samples"),464,&samples) && samples);
                session.input_samples+=(size_t)samples;assert(session.input_samples<=160000);
            }
        } else {
            assert(!strcmp(direction,"receive"));++wire_event;++total;
            char *json=cJSON_PrintUnformatted(e);assert(json);split=1+(wire_event*17)%43;
            assert(!inject(json));cJSON_free(json);
        }
        cJSON_Delete(root);
    }
    assert(!ferror(file) && !fclose(file) && !fclose(output));output=NULL;
    assert(session.done && session.final && agent_asr_segments_settled(&segments));
    agent_asr_realtime_cancel(&session);assert(closes==1 && !transcript[0]);
    printf("provenance_replayed rounds=%u received=%u no_network=true\n",wire_round,total);
}
int main(int argc,char **argv)
{
    if(argc==4 && !strcmp(argv[1],"--replay")) {replay(argv[2],argv[3]);return 0;}
    assert(argc==1);
    for(split=1;split<=43;split+=7) {
        promotion(false);promotion(true);ordered();missing_earlier();faults(false);faults(true);
    }
    printf("asr_provenance all_checks_passed update_bytes=%zu session_bytes=%zu segment_bytes=%zu\n",
        sizeof(agent_asr_segment_update_t),sizeof(session),sizeof(segments));
    return 0;
}
