#include "voice_fast.h"
#include "audio_board.h"
#include "realtime.h"
#include "capture_radio.h"
#include "progress.h"
#include "prefetch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PLAIN,BLUE,DELEGATE,INVALID_BATCH,EARLY_CAPTURE,CANCEL,NO_PCM,MAX_UTF8,MAX_ESCAPED,RESPONSE_ERROR,MIXED_AUDIO_TOOL,UNCLEAR_COLOR,MIXED_AUDIO_DELEGATE,MIXED_DELEGATE_TOOL,CASES };
static unsigned scenario,opens,closes,creates,results,effects,source_reads,stream_opens,stream_finishes,appends,updates;
static unsigned users,turns,errors,tool_messages;
static unsigned measure_resets,measure_reads;
static unsigned commits;
static unsigned early_commits;
static agent_err_t late_capture_error,commit_send_error;
static agent_err_t source_fault,network_input_fault,join_fault;
static unsigned capture_input_errors,asr_input_errors,capture_join_errors;
static unsigned reuse_round;
static bool reuse_no_clear;
static bool search_mode,search_mixed,search_delegate,search_repeat,search_busy;
static bool search_preamble;
static unsigned searches;
static const char *search_args;
static agent_err_t search_failure;
static const char *search_bad_result;
static bool inject_draft;
static const char *manual_preview;
static bool manual_no_preview,manual_changed,manual_formal,manual_bad_echo;
static unsigned manual_drafts,manual_overlap,manual_hold;
static unsigned manual_stream,manual_tail,manual_observed;
static int16_t manual_pcm[8];
static const char *activity_wire;
static unsigned activity_hints;
static bool long_unrouted_audio;
static bool closed_with_unread;
static unsigned draft_notices,draft_revisions;
static bool radio_active;
static unsigned radio_begins,radio_restores;
static agent_err_t radio_begin_error;
static unsigned tail_samples,uploaded_samples;
static bool bad_vad_echo;
static bool socket_open,speaker,joined,ready,cancel_next,stall_fragment,inject_capture_response,no_endpoint;
agent_err_t esp_agent_capture_radio_begin(esp_agent_capture_radio_t *status)
{ assert(!radio_active && !joined);*status=(esp_agent_capture_radio_t){80,8,-40};++radio_begins;radio_active=true;return radio_begin_error; }
agent_err_t esp_agent_capture_radio_end(void)
{ if(radio_active) {assert(joined);radio_active=false;++radio_restores;}return AGENT_OK; }
static bool stall_send;
static bool diagnostic_echo,missing_echo;
static bool failed_asr;
static unsigned provider_notices;
static unsigned session_notices,start_notices,end_notices;
static bool asr_after_response,asr_before_endpoint,cancel_on_close;
static bool delegate_after_tool;
static bool silent_before_asr;
static bool declared_function;
static bool cancel_after_pcm_event,long_function_audio,transition_before_asr;
static const char *wire_fixture;
static const char *native_reply,*late_transcript,*delegate_arguments;
static const char *plain_final,*plain_suffix;
static bool plain_late_function,plain_late_asr;
static unsigned prefetch_live,prefetch_hold,prefetch_streams;
static unsigned prefetch_chunks,prefetch_proof;
static unsigned prefetch_emitted;
static bool prefetch_protocol_fault,prefetch_capture_fault,prefetch_cancel_capture;
static bool prefetch_empty,mock_auto_vad,replaying;
static unsigned replay_reads,replay_releases,cued_opens;
static agent_err_t replay_error;
static bool local_literal,early_literal_metadata;
static uint8_t expected_rgb[3];
static const char *early_arguments,*late_arguments,*native_transcript;
static bool native_ack,bad_ack_item;
static bool read_light,valid_light_batch,cancel_after_light;
static agent_err_t light_error,local_write_error,persist_error;
static unsigned local_replies;
static bool task_alias,task_name_mismatch;
enum { START_NORMAL,START_CREATED,START_AUDIO,START_FRAGMENT,START_NO_FINAL,
       START_AFTER_TOOL,START_AFTER_PCM,START_CANCEL,START_SEND_BUSY,START_CASES };
static unsigned start_fault,timeout_notices,stall_ms;
static uint64_t fault_start;
static agent_err_t drain_failure;
static unsigned native_fallback_notices,native_stats_notices,native_discarded;
static bool native_before_arguments;
static char native_fallback_reason[40];
static size_t read_limit;
static uint64_t ticks,reserved;
static agent_engine_storage_t engine_workspace;
static agent_engine_t engine={AGENT_ENGINE_STORAGE_INIT(engine_workspace)};
static agent_context_t context;
static agent_core_t core;
static unsigned char flash[2*1024*1024];
static char input[AGENT_INPUT_MAX+1],original[AGENT_INPUT_MAX+1];
static char *queue[256];static unsigned head,tail;static size_t offset,sample_count,capture_capacity;
static int16_t *ring;
static size_t ring_samples;
static bool native_cached,cache_sealed,cache_deferred;
static agent_err_t cache_begin_error,cache_seal_error;
static int16_t cached_pcm[144000];
static agent_err_t drained;
static unsigned progress_begins,progress_joins;
static unsigned progress_kind;
static bool progress_owned,progress_cantonese;
static agent_err_t progress_begin_error,progress_join_error;

static agent_err_t progress_start(bool cantonese,unsigned kind,const atomic_bool *cancelled)
{
    assert(!progress_owned && !atomic_load(cancelled));
    ++progress_begins;progress_owned=true;progress_cantonese=cantonese;progress_kind=kind;
    return progress_begin_error;
}
agent_err_t esp_hi_progress_begin(bool cantonese,const atomic_bool *cancelled)
{ return progress_start(cantonese,0,cancelled); }
agent_err_t esp_hi_progress_search_begin(bool cantonese,const atomic_bool *cancelled)
{ return progress_start(cantonese,1,cancelled); }
agent_err_t esp_hi_progress_memory_begin(bool cantonese,const atomic_bool *cancelled)
{ return progress_start(cantonese,2,cancelled); }
agent_err_t esp_hi_progress_join(void)
{ assert(progress_owned);++progress_joins;progress_owned=false;return progress_join_error; }

uint64_t esp_agent_now(void) { return ticks; }
void vTaskDelay(unsigned delay)
{ ticks+=delay;if(prefetch_cancel_capture && !socket_open)atomic_store(&core.cancelled,true); }
void esp_agent_http_release(void) { assert(!socket_open); }
void esp_agent_speech_ws_measure_reset(void) { ++measure_resets; }
void esp_agent_speech_ws_measure(char *out,size_t capacity)
{ assert(measure_resets && out && capacity>=3);++measure_reads;snprintf(out,capacity,"{}"); }
static void enqueue(const char *text)
{ assert(tail<256);size_t n=strlen(text)+1;queue[tail]=malloc(n);assert(queue[tail]);memcpy(queue[tail++],text,n); }
static void enqueue_wire(void)
{
    FILE *file=fopen(wire_fixture,"rb");assert(file);
    char *line=malloc(65536);assert(line);
    while(fgets(line,65536,file)) {
        size_t n=strlen(line);assert(n && line[n-1]=='\n');
        cJSON *item=agent_json_parse(line,n);assert(item);
        const char *type=agent_json_string(item,"type");assert(type);
        if(strncmp(type,"session.",8))enqueue(line);
        cJSON_Delete(item);
    }
    assert(!ferror(file));free(line);fclose(file);
}
static void enqueue_string(const char *type,const char *key,const char *value,bool response)
{
    char text[6144];agent_json_writer_t w;agent_json_writer_init(&w,text,sizeof(text));
    agent_json_raw(&w,"{\"type\":");agent_json_quote(&w,type);
    if(response)agent_json_raw(&w,",\"response_id\":\"r1\"");
    agent_json_raw(&w,",");agent_json_quote(&w,key);agent_json_raw(&w,":");agent_json_quote(&w,value);agent_json_raw(&w,"}");
    assert(!w.error);enqueue(text);
}
static void native_argument(const char *args)
{
    char data[2048];agent_json_writer_t w;agent_json_writer_init(&w,data,sizeof(data));
    agent_json_raw(&w,"{\"type\":\"response.function_call_arguments.delta\",\"response_id\":\"r1\",\"output_index\":0,\"call_id\":\"call_1\",\"item_id\":");
    agent_json_quote(&w,bad_ack_item?"wrong_item":"ack_item");agent_json_raw(&w,",\"delta\":");
    agent_json_quote(&w,args);agent_json_raw(&w,"}");assert(!w.error);enqueue(data);
}
static void response(bool tools)
{
    if(transition_before_asr)enqueue("{\"type\":\"response.created\",\"response\":{\"id\":\"preamble\"}}");
    enqueue("{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}");
    if(silent_before_asr) {
        enqueue("{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AAAAAAAA\"}");
        enqueue_string("conversation.item.input_audio_transcription.completed","transcript",original,false);
    }
    if(tools) {
        if(search_preamble) {
            enqueue("{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"output_index\":0,\"item\":{\"type\":\"function_call\",\"id\":\"search_item\",\"call_id\":\"call_1\",\"name\":\"agent_context_search\",\"arguments\":\"\"}}");
            enqueue_string("response.audio_transcript.delta","delta","我查一下之前的记录。",true);
            char packet[8256];
            size_t n=(size_t)sprintf(packet,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"");
            for(unsigned i=0;i<2000;++i) {memcpy(packet+n,"EjQS",4);n+=4;}
            strcpy(packet+n,"\"}");enqueue(packet);
        }
        if(native_ack) {
            enqueue(task_alias?
                "{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"output_index\":0,\"item\":{\"type\":\"function_call\",\"id\":\"ack_item\",\"call_id\":\"call_1\",\"name\":\"agent_task_start\",\"arguments\":\"\"}}":
                "{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"output_index\":0,\"item\":{\"type\":\"function_call\",\"id\":\"ack_item\",\"call_id\":\"call_1\",\"name\":\"delegate_deepseek\",\"arguments\":\"\"}}");
            const char *args=early_arguments?early_arguments:delegate_arguments;
            native_argument(args);
            enqueue_string("response.audio_transcript.delta","delta",native_transcript?native_transcript:"噢，我找找你给灯起的名字。",true);
            enqueue("{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AAAAAAAAEjR4Vg==\"}");
            if(late_arguments)native_argument(late_arguments);
        }
        if(declared_function)enqueue("{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"output_index\":0,\"item\":{\"type\":\"function_call\"}}");
        if(scenario==MIXED_AUDIO_TOOL || scenario==MIXED_AUDIO_DELEGATE) {
            enqueue_string("response.audio_transcript.delta","delta","已设为蓝色。",true);
            enqueue("{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}");
        }
        if(long_function_audio) {
            char *packet=malloc(128256);assert(packet);
            size_t n=(size_t)sprintf(packet,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"");
            for(unsigned i=0;i<32000;++i) {memcpy(packet+n,"EjQS",4);n+=4;}
            strcpy(packet+n,"\"}");
            for(unsigned i=0;i<4;++i)enqueue(packet);
            free(packet);
        }
        bool delegate=scenario==DELEGATE || scenario==MIXED_AUDIO_DELEGATE || scenario==MIXED_DELEGATE_TOOL || (delegate_after_tool && effects) || (search_delegate && searches);
        const char *name=delegate?(task_alias!=task_name_mismatch?"agent_task_start":"delegate_deepseek"):
            search_mode?"agent_context_search":read_light?"device_light_get":"device_light_set_rgb";
        const char *args=delegate?delegate_arguments:search_mode?search_args:read_light?"{}":"{\"r\":0,\"g\":0,\"b\":255}";
        char text[2048];agent_json_writer_t w;agent_json_writer_init(&w,text,sizeof(text));
        agent_json_raw(&w,"{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":");
        agent_json_quote(&w,scenario==RESPONSE_ERROR?"failed":"completed");
        if(scenario==RESPONSE_ERROR)agent_json_raw(&w,",\"status_details\":{\"type\":\"failed\",\"reason\":\"provider_failure\",\"error\":{\"code\":\"overloaded\",\"message\":\"do-not-log\",\"param\":\"private\"}}");
        agent_json_raw(&w,",\"output\":[");
        if(scenario==MIXED_AUDIO_TOOL)agent_json_raw(&w,"{\"type\":\"message\",\"content\":[{\"type\":\"audio\",\"transcript\":\"已设为蓝色。\"}]},");
        agent_json_raw(&w,"{\"type\":\"function_call\",\"call_id\":\"call_1\",\"name\":");
        agent_json_quote(&w,name);agent_json_raw(&w,",\"arguments\":");agent_json_quote(&w,args);agent_json_raw(&w,"}");
        if(scenario==INVALID_BATCH)agent_json_raw(&w,",{\"type\":\"function_call\",\"call_id\":\"call_2\",\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":300,\\\"g\\\":0,\\\"b\\\":0}\"}");
        if(valid_light_batch)agent_json_raw(&w,",{\"type\":\"function_call\",\"call_id\":\"call_2\",\"name\":\"device_light_get\",\"arguments\":\"{}\"}");
        if(scenario==MIXED_DELEGATE_TOOL)agent_json_raw(&w,",{\"type\":\"function_call\",\"call_id\":\"call_2\",\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":0,\\\"g\\\":0,\\\"b\\\":255}\"}");
        if(search_mixed)agent_json_raw(&w,",{\"type\":\"function_call\",\"call_id\":\"call_2\",\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":0,\\\"g\\\":0,\\\"b\\\":255}\"}");
        agent_json_raw(&w,"]}}");assert(!w.error);enqueue(text);return;
    }
    if(prefetch_live)enqueue("{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"output_index\":0,\"item\":{\"type\":\"message\"}}");
    enqueue_string("response.audio_transcript.delta","delta",scenario==BLUE?"已设为蓝色。":native_reply?native_reply:"你好。",true);
    if(plain_late_asr)enqueue_string("conversation.item.input_audio_transcription.completed","transcript",original,false);
    if(late_transcript && scenario==BLUE)enqueue_string("conversation.item.input_audio_transcription.completed","transcript",late_transcript,false);
    if(scenario!=NO_PCM)enqueue("{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AAAAAAAAEjR4Vg==\"}");
    if(manual_stream) {
        manual_tail=tail;
        if(manual_stream==3)enqueue_string("response.audio_transcript.delta","delta","其实换一句。",true);
        if(manual_stream==4)enqueue("{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"item\":{\"type\":\"function_call\"}}");
        enqueue("{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}");
        if(manual_stream==8 || manual_stream==9) {
            char packet[8128];
            size_t n=(size_t)sprintf(packet,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"");
            for(unsigned i=0;i<2000;++i) {memcpy(packet+n,"EjQS",4);n+=4;}
            strcpy(packet+n,"\"}");
            for(unsigned i=0;i<28;++i)enqueue(packet);
        }
    }
    if(prefetch_live) {
        prefetch_hold=tail;
        enqueue("{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}");
        if(prefetch_live==4)enqueue("{\"type\":\"input_audio_buffer.speech_started\",\"item_id\":\"i2\",\"audio_start_ms\":1000}");
    }
    if(long_unrouted_audio) {
        char packet[8256];
        size_t n=(size_t)sprintf(packet,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"");
        for(unsigned i=0;i<2000;++i) {memcpy(packet+n,"EjQS",4);n+=4;}
        strcpy(packet+n,"\"}");enqueue(packet);
    }
    if(late_transcript && scenario!=BLUE)enqueue_string("conversation.item.input_audio_transcription.completed","transcript",late_transcript,false);
    if(plain_suffix)enqueue_string("response.audio_transcript.delta","delta",plain_suffix,true);
    if(plain_late_function)enqueue("{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"output_index\":0,\"item\":{\"type\":\"function_call\",\"name\":\"device_light_set_rgb\"}}");
    char final[2048];agent_json_writer_t w;agent_json_writer_init(&w,final,sizeof(final));
    agent_json_raw(&w,"{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"audio\",\"transcript\":");
    agent_json_quote(&w,plain_final?plain_final:scenario==BLUE?"已设为蓝色。":native_reply?native_reply:"你好。");
    agent_json_raw(&w,"}]}]}}");assert(!w.error);enqueue(final);
}
static agent_err_t ws_open(void *p,const atomic_bool *flag)
{
    (void)p;assert(!socket_open && !atomic_load(flag));
    if(replaying) {assert(speaker && cued_opens==1);commits=0;} /* Cue already scheduled before reconnect. */
    socket_open=true;++opens;enqueue("{\"type\":\"session.created\"}");return AGENT_OK;
}
static void ws_close(void *p)
{
    (void)p;assert(socket_open);socket_open=false;++closes;
    closed_with_unread=head<tail;
    for(;head<tail;++head)free(queue[head]);
    head=tail=0;offset=0;
    if(cancel_on_close)atomic_store(&core.cancelled,true);
}
static agent_err_t ws_send(void *p,bool binary,char *data,size_t length,const atomic_bool *flag)
{
    (void)p;assert(socket_open && !binary && length<2048 && !atomic_load(flag));
    cJSON *json=agent_json_parse(data,length);assert(json);const char *type=agent_json_string(json,"type");assert(type);
    if(esp_agent_voice_reuse_enabled() && (!esp_agent_voice_prefetch_enabled() || replaying || manual_formal) && !strcmp(type,"input_audio_buffer.commit")) {
        assert(joined && speaker && !commits && appends);++commits;
        char wire[4096];agent_json_writer_t w;
        snprintf(wire,sizeof(wire),"{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"reuse_i%u\"}",reuse_round);enqueue(wire);
        agent_json_writer_init(&w,wire,sizeof(wire));
        agent_json_printf(&w,"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"reuse_i%u\",\"transcript\":",reuse_round);
        agent_json_quote(&w,original);agent_json_raw(&w,"}");assert(!w.error);enqueue(wire);
        cJSON_Delete(json);return AGENT_OK;
    }
    if(esp_agent_voice_reuse_enabled() && (!esp_agent_voice_prefetch_enabled() || replaying || manual_formal) && !strcmp(type,"response.create")) {
        assert((scenario==PLAIN || scenario==DELEGATE) && joined && commits==1);++creates;
        if(scenario==DELEGATE) {response(true);cJSON_Delete(json);return AGENT_OK;}
        char wire[256];
        snprintf(wire,sizeof(wire),"{\"type\":\"response.created\",\"response\":{\"id\":\"reuse_r%u\"}}",reuse_round);enqueue(wire);
        snprintf(wire,sizeof(wire),"{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"reuse_r%u\",\"delta\":\"你好\"}",reuse_round);enqueue(wire);
        snprintf(wire,sizeof(wire),"{\"type\":\"response.audio.delta\",\"response_id\":\"reuse_r%u\",\"delta\":\"EjR4Vg==\"}",reuse_round);enqueue(wire);
        snprintf(wire,sizeof(wire),"{\"type\":\"response.done\",\"response\":{\"id\":\"reuse_r%u\",\"status\":\"completed\",\"output\":[]}}",reuse_round);enqueue(wire);
        cJSON_Delete(json);return AGENT_OK;
    }
    if(esp_agent_voice_reuse_enabled() && !strcmp(type,"input_audio_buffer.clear")) {
        assert(!speaker && joined && commits==1);
        if(!reuse_no_clear)enqueue("{\"type\":\"input_audio_buffer.cleared\"}");
        cJSON_Delete(json);return AGENT_OK;
    }
    if(!strcmp(type,"session.update")) {
        const cJSON *session=cJSON_GetObjectItemCaseSensitive(json,"session");
        const cJSON *vad=cJSON_GetObjectItemCaseSensitive(session,"turn_detection");
        mock_auto_vad=!cJSON_IsNull(vad);
        if(esp_agent_voice_prefetch_enabled() && !replaying) {
            assert(cJSON_IsNull(vad));++updates;
            const cJSON *available=cJSON_GetObjectItemCaseSensitive(session,"tools");
            manual_formal=cJSON_GetArraySize(available)==4;
            assert(manual_formal || cJSON_GetArraySize(available)==0);
            cJSON *echo=cJSON_Duplicate(json,true);assert(echo);
            cJSON_ReplaceItemInObject(echo,"type",cJSON_CreateString("session.updated"));
            cJSON *echo_session=cJSON_GetObjectItemCaseSensitive(echo,"session");
            if(!cJSON_GetObjectItemCaseSensitive(echo_session,"audio"))
                cJSON_AddItemToObject(echo_session,"audio",cJSON_Parse("{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":16000}}}"));
            /* Actual Omni traces omit both fields even after accepting them. */
            if(!bad_vad_echo) {
                cJSON_DeleteItemFromObject(cJSON_GetObjectItemCaseSensitive(echo,"session"),"turn_detection");
                cJSON_DeleteItemFromObject(cJSON_GetObjectItemCaseSensitive(echo,"session"),"tools");
            }
            if(manual_bad_echo && updates==1)
                cJSON_ReplaceItemInObject(cJSON_GetObjectItemCaseSensitive(echo,"session"),"instructions",cJSON_CreateString("wrong prompt"));
            char *text=cJSON_PrintUnformatted(echo);assert(text);enqueue(text);free(text);cJSON_Delete(echo);
            cJSON_Delete(json);return AGENT_OK;
        }
        assert(cJSON_IsNull(vad));
        const cJSON *available=cJSON_GetObjectItemCaseSensitive(session,"tools");
        assert(cJSON_IsArray(available) && cJSON_GetArraySize(available)==4);
        manual_formal=true;
        bool recall=false;
        for(const cJSON *tool=available->child;tool;tool=tool->next)
            recall|=!strcmp(agent_json_string(cJSON_GetObjectItemCaseSensitive(tool,"function"),"name"),"agent_context_search");
        assert(recall);
        ++updates;
        if(bad_vad_echo)enqueue("{\"type\":\"session.updated\",\"session\":{\"turn_detection\":{\"type\":\"server_vad\"}}}");
        else if(missing_echo)enqueue("{\"type\":\"session.updated\"}");
        else if(diagnostic_echo)enqueue("{\"type\":\"session.updated\",\"session\":{\"api_key\":\"do-not-log\",\"turn_detection\":null,\"private\":\"do-not-log\",\"audio\":{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":16000}}}}}");
        else enqueue("{\"type\":\"session.updated\",\"session\":{\"turn_detection\":null,\"audio\":{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":16000}}}}}");
    }
    else if(!strcmp(type,"input_audio_buffer.append")) {
        if(stall_send) {cJSON_Delete(json);return AGENT_ERR_BUSY;}
        assert(!joined || replaying);++appends;
        const char *audio=agent_json_string(json,"audio");assert(audio);
        size_t bytes=strlen(audio)*3/4;
        if(audio[strlen(audio)-1]=='=')--bytes;
        if(audio[strlen(audio)-2]=='=')--bytes;
        uploaded_samples+=(unsigned)bytes/2;
        if(esp_agent_voice_prefetch_enabled() && !replaying) {
            if(!manual_no_preview && (appends==1 || (manual_changed && appends==32))) {
                char text[1024];agent_json_writer_t w;agent_json_writer_init(&w,text,sizeof(text));
                agent_json_raw(&w,"{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"preview\",\"text\":");
                agent_json_quote(&w,appends==1 && manual_preview?manual_preview:original);
                agent_json_raw(&w,",\"stash\":\"\"}");assert(!w.error);enqueue(text);
            }
            if(prefetch_protocol_fault && appends==25)enqueue("{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"unsolicited\"}");
            cJSON_Delete(json);return AGENT_OK;
        }
        if(inject_draft && appends==1)
            enqueue("{\"type\":\"conversation.item.input_audio_transcription.delta\",\"text\":\"先查历史\",\"stash\":\"。\"}");
        if(activity_wire && appends==1) {enqueue(activity_wire);enqueue(activity_wire);}
        if(inject_capture_response)enqueue("{\"type\":\"response.text.delta\",\"delta\":\"unexpected\"}");
        else if(asr_before_endpoint)enqueue_string("conversation.item.input_audio_transcription.completed","transcript",original,false);
    }
    else if(!strcmp(type,"input_audio_buffer.commit")) {
        bool early=esp_agent_voice_prefetch_enabled() && !replaying && !manual_formal;
        assert(!commits && appends);
        if(early) {
            assert(!joined && !speaker && !effects && !sample_count && source_reads>prefetch_chunks);
            ++early_commits;
            if(commit_send_error) {cJSON_Delete(json);return commit_send_error;}
        } else assert(joined && speaker);
        ++commits;
        if(esp_agent_voice_prefetch_enabled() && !replaying && !manual_formal) {
            char text[3072];agent_json_writer_t w;agent_json_writer_init(&w,text,sizeof(text));
            char id[32];snprintf(id,sizeof(id),"complete%u",esp_agent_voice_reuse_enabled()?reuse_round:0);
            snprintf(text,sizeof(text),"{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"%s\"}",id);enqueue(text);
            agent_json_writer_init(&w,text,sizeof(text));
            agent_json_printf(&w,"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"%s\",\"transcript\":",id);
            agent_json_quote(&w,prefetch_empty?"":original);agent_json_raw(&w,"}");assert(!w.error);enqueue(text);
            if(manual_stream) {
                /* Full ASR arrives between an early cached prefix and a slow
                 * remaining response; it must not wait behind the whole tail. */
                char *ack=queue[tail-2],*asr=queue[tail-1];
                memmove(queue+manual_tail+2,queue+manual_tail,(tail-2-manual_tail)*sizeof(*queue));
                queue[manual_tail]=ack;queue[manual_tail+1]=asr;manual_tail+=2;
            }
            cJSON_Delete(json);return AGENT_OK;
        }
        if(early_literal_metadata) {
            enqueue("{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}");
            enqueue("{\"type\":\"response.output_item.added\",\"response_id\":\"r1\",\"output_index\":0,\"item\":{\"type\":\"function_call\",\"name\":\"unapproved_tool\"}}");
        }
        if(failed_asr)enqueue("{\"type\":\"conversation.item.input_audio_transcription.failed\",\"item_id\":\"i1\",\"content_index\":0,\"error\":{\"type\":\"transcription_error\",\"code\":\"transcription_failed\",\"message\":\"do-not-log\",\"param\":\"private\"}}");
        else if(!wire_fixture && !asr_after_response && !asr_before_endpoint && !silent_before_asr && !plain_late_asr)
            enqueue_string("conversation.item.input_audio_transcription.completed","transcript",original,false);
    } else if(!strcmp(type,"response.create")) {
        if(esp_agent_voice_prefetch_enabled() && !replaying && !manual_formal) {
            assert(!joined && !commits && !manual_drafts && !speaker && !effects && !sample_count);
            ++manual_drafts;unsigned first=tail;
            response(false);
            if(manual_stream==10) {
                cJSON *failed=agent_json_parse(queue[tail-1],strlen(queue[tail-1]));assert(failed);
                cJSON_ReplaceItemInObject(cJSON_GetObjectItemCaseSensitive(failed,"response"),"status",cJSON_CreateString("failed"));
                free(queue[tail-1]);queue[tail-1]=cJSON_PrintUnformatted(failed);assert(queue[tail-1]);cJSON_Delete(failed);
            }
            /* A unique draft identity, never reused by the normal response. */
            for(unsigned i=first;i<tail;++i) {
                char *at;while((at=strstr(queue[i],"r1"))) {
                    at[0]='d';if(esp_agent_voice_reuse_enabled())at[1]=(char)('0'+reuse_round);
                }
            }
            if(manual_overlap)manual_hold=first+(manual_overlap==2?1:0);
            if(manual_stream)manual_hold=manual_tail;
            cJSON_Delete(json);return AGENT_OK;
        }
        if(start_fault==START_SEND_BUSY) {cJSON_Delete(json);return AGENT_ERR_BUSY;}
        assert(joined);++creates;
        assert(commits==1);
        bool tools=delegate_after_tool || (creates==1 && (scenario==BLUE || scenario==DELEGATE || scenario==INVALID_BATCH || scenario==RESPONSE_ERROR || scenario==MIXED_AUDIO_TOOL || scenario==UNCLEAR_COLOR || scenario==MIXED_AUDIO_DELEGATE || scenario==MIXED_DELEGATE_TOOL));
        tools|=search_mode && (creates==1 || search_delegate || search_repeat);
        if(wire_fixture)enqueue_wire();
        else if(!failed_asr) {
            response(tools);
            if(creates==1 && asr_after_response)enqueue_string("conversation.item.input_audio_transcription.completed","transcript",original,false);
        }
    } else if(!strcmp(type,"conversation.item.create")) {
        assert((scenario==BLUE && effects>=1 && effects<=2) || (scenario==UNCLEAR_COLOR && !effects) || (search_mode && searches && !effects));++results;
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(json,"item");
        assert(!strcmp(agent_json_string(item,"call_id"),valid_light_batch && results==2?"call_2":"call_1"));
        cJSON *output=agent_json_parse(agent_json_string(item,"output"),strlen(agent_json_string(item,"output")));assert(output);
        if(search_mode)assert(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(output,"hits")));
        else if(scenario==BLUE)assert(cJSON_GetObjectItemCaseSensitive(output,"b")->valueint==255);
        else assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(output,"ok")));
        cJSON_Delete(output);
    } else assert(false);
    cJSON_Delete(json);return AGENT_OK;
}
static agent_err_t ws_read(void *p,char *data,size_t cap,agent_ws_chunk_t *part,unsigned ms,const atomic_bool *flag)
{
    (void)p;assert(socket_open && !atomic_load(flag));
    if(network_input_fault && ready && appends && !joined)return network_input_fault;
    if(cancel_next) {atomic_store(&core.cancelled,true);return AGENT_ERR_CANCELLED;}
    if(head==tail || (stall_fragment && offset)) {ticks+=ms;return AGENT_OK;}
    if(manual_overlap && manual_drafts && !joined && head>=manual_hold) {ticks+=ms;return AGENT_OK;}
    if(manual_stream && joined && head>=manual_tail) {
        assert(sample_count>0 && !effects && !creates); /* Release precedes tail read. */
        if(manual_stream==6) {atomic_store(&core.cancelled,true);return AGENT_ERR_CANCELLED;}
        if(manual_stream==7)return AGENT_ERR_NETWORK;
    }
    if(prefetch_live && prefetch_hold && head>=prefetch_hold) {
        if(!joined) {ticks+=ms;return AGENT_OK;}
        assert(prefetch_streams==1 && sample_count>0);
        if(prefetch_live==5 && head>prefetch_hold) {
            atomic_store(&core.cancelled,true);return AGENT_ERR_CANCELLED;
        }
    }
    bool created=strstr(queue[head],"\"type\":\"response.created\"")!=NULL;
    bool audio=strstr(queue[head],"\"type\":\"response.audio.delta\"")!=NULL;
    bool done=strstr(queue[head],"\"type\":\"response.done\"")!=NULL;
    bool asr=strstr(queue[head],"input_audio_transcription.completed")!=NULL;
    bool delayed=(start_fault==START_CREATED && ((created && ticks<fault_start+2000) || audio)) ||
        ((start_fault==START_AUDIO || start_fault==START_CANCEL) && audio) ||
        (start_fault==START_FRAGMENT && audio && offset) ||
        (start_fault==START_NO_FINAL && asr) ||
        (start_fault==START_AFTER_TOOL && creates>1 && audio) ||
        (start_fault==START_AFTER_PCM && sample_count && done);
    if(delayed && ticks<fault_start+stall_ms) {ticks+=ms;return AGENT_OK;}
    /* A ready buffered frame does not consume the read's entire wait timeout. */
    ++ticks;
    if(strstr(queue[head],"\"type\":\"response."))assert(joined || inject_capture_response || !appends || esp_agent_voice_prefetch_enabled());
    size_t length=strlen(queue[head]),n=length-offset;if(n>cap)n=cap;if(n>read_limit)n=read_limit;
    *part=(agent_ws_chunk_t){n,offset?0:1,true,offset+n==length};memcpy(data,queue[head]+offset,n);offset+=n;
    if(offset==length) {
        if(scenario==CANCEL && strstr(queue[head],"input_audio_transcription.completed"))cancel_next=true;
        if(cancel_after_pcm_event && strstr(queue[head],"response.audio.delta"))cancel_next=true;
        free(queue[head++]);offset=0;
    }
    return AGENT_OK;
}
const agent_ws_ops_t esp_agent_realtime_ws={ws_open,ws_send,ws_read,ws_close,NULL};
static void capture_intact(void)
{
    size_t bytes;unsigned char *arena=agent_engine_scratch(&engine,&bytes);(void)bytes;
    size_t begin=esp_agent_voice_prefetch_enabled()?AGENT_PREFETCH_BYTES:0;
    for(size_t i=begin;i<capture_capacity;++i)assert(arena[i]==0xa5);
}
static agent_err_t next(void *p,int16_t *out,size_t cap,size_t *count,bool *end)
{
    (void)p;assert(!joined && ready && cap==ESP_AGENT_FAST_UPLOAD_SAMPLES);capture_intact();++source_reads;
    if(source_fault)return source_fault;
    if(esp_agent_voice_prefetch_enabled()) {
        assert(!sample_count && !effects);
        if(prefetch_capture_fault && prefetch_emitted>=32)return AGENT_ERR_CORRUPT;
        *end=prefetch_emitted==prefetch_chunks;*count=!*end && source_reads%8==1?cap:0;
        if(*end && no_endpoint)return AGENT_ERR_TIMEOUT;
        if(*count && prefetch_emitted+1==prefetch_chunks && tail_samples)*count=tail_samples;
        if(*count)++prefetch_emitted;
        for(size_t i=0;i<*count;++i)out[i]=(int16_t)i;
        return AGENT_OK;
    }
    if(scenario==EARLY_CAPTURE)return AGENT_ERR_NETWORK;
    *count=source_reads==1?cap:source_reads==2?tail_samples:0;*end=source_reads>1;
    for(size_t i=0;i<*count;++i)out[i]=(int16_t)i;
    return AGENT_OK;
}
agent_err_t esp_hi_voice_live_input(agent_speech_live_t *out)
{ *out=(agent_speech_live_t){next,NULL};return AGENT_OK; }
agent_err_t esp_hi_voice_live_workspace(void **memory,size_t *capacity)
{ *memory=agent_engine_scratch(&engine,capacity);*capacity=capture_capacity-3000;return AGENT_OK; }
void esp_hi_voice_live_sentence(uint32_t id,unsigned end,bool final,bool nonempty,bool timed)
{ assert(esp_agent_voice_prefetch_enabled());
  if(id==UINT32_MAX) {assert((prefetch_protocol_fault || manual_bad_echo) && !final && !end && !nonempty && !timed);return;}
  assert(id>=1 && id<=2);assert(final?(id==1 && nonempty==!prefetch_empty && timed && end==800):(!nonempty && !timed && !end)); }
unsigned esp_hi_voice_live_final(void) {assert(joined);return prefetch_proof;}
static agent_err_t replay_read(void *ctx,size_t at,int16_t *out,size_t n)
{ (void)ctx;assert(replaying && joined && at+n<=prefetch_chunks*ESP_AGENT_FAST_UPLOAD_SAMPLES);++replay_reads;memset(out,0,n*sizeof(*out));return replay_error; }
agent_err_t esp_hi_voice_input(agent_speech_input_t *source)
{ assert(joined && !speaker && !effects && !replaying);replaying=true;*source=(agent_speech_input_t){prefetch_chunks*ESP_AGENT_FAST_UPLOAD_SAMPLES,replay_read,NULL};return AGENT_OK; }
void esp_hi_voice_release(void) {assert(replaying);replaying=false;++replay_releases;}
void esp_hi_voice_live_ready(void) { ready=true; }
void esp_hi_voice_live_hint(const char *text,bool settled)
{ assert(text && !settled && !joined && !speaker && !effects && appends);
  if(agent_speech_meaningful_partial(text))++activity_hints; }
agent_err_t esp_hi_voice_live_join(agent_err_t error)
{
    assert(error || source_reads>1);
    capture_intact();joined=true;
    size_t bytes;void *arena=agent_engine_scratch(&engine,&bytes);
    if(!esp_agent_voice_prefetch_enabled())memset(arena,0,capture_capacity);
    if(join_fault)return error?error:join_fault;
    if(late_capture_error) {
        assert(early_commits==1 && commits==1 && !effects && !sample_count && !speaker);
        if(late_capture_error==AGENT_ERR_CANCELLED)atomic_store(&core.cancelled,true);
        return error?error:late_capture_error;
    }
    return error?error:no_endpoint?AGENT_ERR_TIMEOUT:AGENT_OK;
}
agent_err_t esp_hi_stream_open(unsigned rate,void *memory,size_t bytes)
{
    assert(joined && !speaker && rate==16000);
    if(esp_agent_voice_prefetch_enabled() && !replaying && bytes==4096) {
        size_t n;char *base=agent_engine_scratch(&engine,&n);
        assert(memory==base+capture_capacity-4096 && bytes==4096);
    } else assert(memory==engine.buffer && bytes==8192);
    speaker=true;ring=memory;ring_samples=bytes/2;++stream_opens;memset(memory,0,bytes);return AGENT_OK;
}
agent_err_t esp_hi_stream_open_cued(unsigned rate,void *memory,size_t bytes)
{ assert(joined && !atomic_load(&core.cancelled));++cued_opens;return esp_hi_stream_open(rate,memory,bytes); }
agent_err_t esp_hi_stream_write(const int16_t *pcm,size_t count,const atomic_bool *flag)
{
    assert(joined && speaker && !atomic_load(flag));
    if(!socket_open && local_write_error)return local_write_error;
    assert(sample_count+count<=144000);
    if(manual_stream)for(size_t i=0;i<count && manual_observed<8;++i)manual_pcm[manual_observed++]=pcm[i];
    for(size_t i=0;i<count;++i) {
        if(native_cached)cached_pcm[sample_count+i]=pcm[i];
        else ring[(sample_count+i)%ring_samples]=pcm[i];
    }
    sample_count+=count;return AGENT_OK;
}
agent_err_t esp_hi_stream_finish(agent_err_t error)
{
    assert(speaker && (!socket_open || (esp_agent_voice_reuse_enabled() && scenario==PLAIN)));
    /* The deferred board consumer cannot consume a single sample before seal.
     * Cancellation discards its unpublished speech, not already-audible PCM. */
    if(cache_deferred && !cache_sealed) {assert(error);sample_count=0;}
    speaker=false;ring=NULL;++stream_finishes;drained=drain_failure?drain_failure:error;return drained;
}
agent_err_t esp_hi_stream_cache_begin(const atomic_bool *flag,bool deferred)
{
    assert(speaker && !sample_count && !native_cached && !atomic_load(flag));
    if(cache_begin_error)return cache_begin_error;
    native_cached=true;cache_deferred=deferred;return AGENT_OK;
}
agent_err_t esp_hi_stream_cache_seal(void)
{
    assert(speaker && native_cached && !socket_open && !cache_sealed);
    if(cache_seal_error)return cache_seal_error;
    cache_sealed=true;ring=NULL;return AGENT_OK;
}
unsigned esp_hi_stream_remaining(void) { return (unsigned)sample_count; }
unsigned esp_hi_stream_underruns(void) { return 0; }
/* Existing full-turn checks keep their original terminal contract. Separate
 * detached checks below inspect the new ownership interval before joining. */
static agent_err_t finish_join(agent_err_t error,bool *delegate)
{
    error=esp_agent_voice_fast_finish(error,delegate);
    error=esp_agent_voice_fast_ack_join(error);
    if(error)*delegate=false;
    return error;
}
static agent_err_t rd(void *p,size_t at,void *dst,size_t n)
{
    (void)p;assert(at+n<=sizeof(flash));
    if(search_busy && search_failure) {
        if(search_failure==AGENT_ERR_CANCELLED)atomic_store(&core.cancelled,true);
        if(search_failure==AGENT_ERR_TIMEOUT)ticks+=AGENT_CONTEXT_SCAN_MS+1;
        else return search_failure;
    }
    memcpy(dst,flash+at,n);return AGENT_OK;
}
static agent_err_t wr(void *p,size_t at,const void *src,size_t n)
{
    (void)p;assert((!socket_open || (esp_agent_voice_reuse_enabled() && scenario==PLAIN)) && !speaker && at+n<=sizeof(flash));const unsigned char *bytes=src;
    if(persist_error)return persist_error;
    for(size_t i=0;i<n;++i) {assert((flash[at+i]&bytes[i])==bytes[i]);flash[at+i]&=bytes[i];}return AGENT_OK;
}
static agent_err_t erase(void *p,size_t at,size_t n)
{ (void)p;assert((!socket_open || (esp_agent_voice_reuse_enabled() && scenario==PLAIN)) && !speaker && at+n<=sizeof(flash));memset(flash+at,255,n);return AGENT_OK; }
static agent_err_t reserve(void *p,uint64_t *first,uint64_t *last)
{ (void)p;assert((!socket_open || (esp_agent_voice_reuse_enabled() && scenario==PLAIN)) && !speaker);*first=reserved+1;reserved+=128;*last=reserved;return AGENT_OK; }
static agent_err_t light(void *p,const uint8_t rgb[3])
{
    (void)p;
    if(!joined || !speaker || (!socket_open && !local_literal) || memcmp(rgb,expected_rgb,3))
        fprintf(stderr,"light: %s joined=%u speaker=%u socket=%u literal=%u RGB=%u,%u,%u expected=%u,%u,%u\n",
            original,joined,speaker,socket_open,local_literal,rgb[0],rgb[1],rgb[2],expected_rgb[0],expected_rgb[1],expected_rgb[2]);
    assert(joined && speaker && (socket_open || local_literal) && !memcmp(rgb,expected_rgb,3));
    if(light_error)return light_error;
    ++effects;if(cancel_after_light)atomic_store(&core.cancelled,true);return AGENT_OK;
}
static agent_err_t light_get(void *p,uint8_t rgb[3])
{ rgb[0]=rgb[1]=0;rgb[2]=255;return light(p,rgb); }
static uint64_t tool_time(void *p) { (void)p;return ticks; }
static agent_err_t recall(void *p,const char *query,uint64_t before,unsigned limit,char *output,size_t cap)
{
    (void)p;assert(socket_open && speaker && joined && !sample_count && !effects);
    assert(output==engine.workspace->reply.text && cap==768 && !strcmp(query,"名字"));
    assert(strstr(engine.workspace->messages.data,"agent_context_search") && !search_busy);
    ++searches;search_busy=true;
    agent_err_t error=agent_context_search(&context,query,before,limit,output,cap);
    if(!error && search_bad_result)strcpy(output,search_bad_result);
    search_busy=false;return error;
}
static agent_err_t inspect_event(void *p,const agent_record_t *record,const char *data)
{
    (void)p;if(record->kind!=AGENT_WAL_EVENT)return AGENT_OK;
    cJSON *root=agent_json_parse(data,record->length);assert(root);
    const char *type=agent_json_string(root,"type");const cJSON *content=cJSON_GetObjectItemCaseSensitive(root,"content");
    if(!strcmp(type,"message")) {++users;assert(!strcmp(agent_json_string(content,"text"),original));}
    if(!strcmp(type,"error")) {
        ++errors;
        if(scenario==MIXED_AUDIO_TOOL) {
            assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(content,"partial")));
            assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(content,"effects")));
        }
    }
    if(!strcmp(type,"turn")) {
        if(search_mode && strstr(data,"source_fixture")) {cJSON_Delete(root);return AGENT_OK;}
        ++turns;const cJSON *messages=cJSON_GetObjectItemCaseSensitive(content,"messages");assert(cJSON_IsArray(messages));
        assert(!strcmp(agent_json_string(messages->child,"content"),original));
        for(const cJSON *item=messages->child;item;item=item->next)if(!strcmp(agent_json_string(item,"role"),"tool"))++tool_messages;
        assert(strstr(data,local_literal?agent_progress_light_text(strstr(original,"唔")!=NULL):scenario==BLUE?(read_light || valid_light_batch?"已设为蓝色":
            agent_progress_light_text(strstr(original,"唔")!=NULL)):"你好"));
    }
    cJSON_Delete(root);return AGENT_OK;
}
static void reset(void)
{
    esp_agent_voice_prefetch_set(false);
    esp_agent_voice_reuse_set(false);reuse_round=0;reuse_no_clear=false;
    for(;head<tail;++head)free(queue[head]);
    head=tail=0;offset=0;
    opens=closes=creates=results=effects=source_reads=stream_opens=stream_finishes=appends=updates=0;
    measure_resets=measure_reads=commits=tail_samples=uploaded_samples=0;bad_vad_echo=false;
    early_commits=0;late_capture_error=commit_send_error=AGENT_OK;
    source_fault=network_input_fault=join_fault=AGENT_OK;
    capture_input_errors=asr_input_errors=capture_join_errors=0;
    search_mode=search_mixed=search_delegate=search_repeat=search_busy=search_preamble=false;searches=0;
    search_args="{\"query\":\"名字\"}";search_failure=AGENT_OK;search_bad_result=NULL;
    manual_preview=NULL;manual_no_preview=manual_changed=manual_formal=manual_bad_echo=false;
    manual_drafts=manual_overlap=manual_hold=0;
    manual_stream=manual_tail=manual_observed=0;memset(manual_pcm,0,sizeof(manual_pcm));
    inject_draft=long_unrouted_audio=closed_with_unread=false;draft_notices=draft_revisions=0;
    assert(!radio_active);radio_begins=radio_restores=0;radio_begin_error=AGENT_OK;
    users=turns=errors=tool_messages=0;socket_open=speaker=joined=ready=cancel_next=stall_fragment=inject_capture_response=no_endpoint=false;
    stall_send=false;
    diagnostic_echo=missing_echo=false;session_notices=start_notices=end_notices=0;
    failed_asr=false;provider_notices=0;
    asr_after_response=asr_before_endpoint=cancel_on_close=delegate_after_tool=silent_before_asr=declared_function=false;native_reply=late_transcript=NULL;drain_failure=AGENT_OK;
    wire_fixture=NULL;cancel_after_pcm_event=long_function_audio=transition_before_asr=false;
    plain_final=plain_suffix=NULL;plain_late_function=plain_late_asr=false;
    prefetch_live=prefetch_hold=prefetch_streams=0;
    prefetch_chunks=40;prefetch_proof=0;
    prefetch_emitted=0;prefetch_protocol_fault=prefetch_capture_fault=prefetch_cancel_capture=false;
    prefetch_empty=mock_auto_vad=replaying=false;replay_reads=replay_releases=cued_opens=0;replay_error=AGENT_OK;
    local_literal=early_literal_metadata=false;expected_rgb[0]=expected_rgb[1]=0;expected_rgb[2]=255;
    native_ack=bad_ack_item=false;early_arguments=late_arguments=native_transcript=NULL;native_before_arguments=false;
    activity_wire=NULL;activity_hints=0;
    read_light=valid_light_batch=cancel_after_light=false;light_error=local_write_error=persist_error=AGENT_OK;local_replies=0;
    task_alias=task_name_mismatch=false;
    start_fault=START_NORMAL;timeout_notices=0;fault_start=0;stall_ms=6000;
    native_fallback_notices=native_stats_notices=native_discarded=0;native_fallback_reason[0]=0;
    delegate_arguments="{}";
    ticks=0;sample_count=0;read_limit=31;drained=AGENT_OK;
    native_cached=cache_sealed=cache_deferred=false;cache_begin_error=cache_seal_error=AGENT_OK;
    memset(&engine,0,sizeof(engine)); assert(!agent_engine_bind_workspace(&engine,&engine_workspace,sizeof(engine_workspace))); memset(&engine_workspace,0,sizeof(engine_workspace));memset(&context,0,sizeof(context));memset(flash,255,sizeof(flash));
    agent_core_init(&core);assert(!agent_begin_turn(&core));
    static const agent_flash_ops_t storage={rd,wr,erase,NULL,sizeof(flash),4096};
    static const agent_tool_ops_t tools={.light_set=light,.light_get=light_get,.context_search=recall,.now_ms=tool_time};
    agent_wal_t wal;assert(!agent_wal_format(&wal,&storage));
    context.device="fast-test";context.user="user";context.session="session";context.reserve=reserve;
    context.scratch=engine.buffer;context.capacity=AGENT_ENGINE_BUFFER_SIZE;assert(!agent_context_open(&context,&storage));
    context.now_ms=tool_time;context.cancelled=&core.cancelled;
    engine.core=&core;engine.context=&context;engine.context_ops=&agent_context_ops;engine.tools=&tools;
    strcpy(original,"你好，小言。");
    if(scenario==BLUE)strcpy(original,"请把灯设为蓝色。设置完成后回答我。");
    if(scenario==UNCLEAR_COLOR)strcpy(original,"请把四个。");
    if(scenario==MAX_UTF8) {for(unsigned i=0;i<682;++i)memcpy(original+i*3,"言",3);original[2046]='A';original[2047]='B';original[2048]=0;}
    if(scenario==MAX_ESCAPED) {memset(original,'"',2048);original[2048]=0;}
    size_t bytes;void *memory=agent_engine_scratch(&engine,&bytes);capture_capacity=(bytes-AGENT_RT_SCRATCH)&~(size_t)7;
    memset(memory,0xa5,capture_capacity);
}
static void warm_start(void)
{
    assert(!esp_agent_voice_fast_ready());
    assert(!esp_agent_voice_fast_warm(&engine,input,sizeof(input),NULL));
    assert(esp_agent_voice_fast_ready() && socket_open && !speaker && !appends && !source_reads && !creates);
    assert(updates==opens);capture_intact();
}
static void draft_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(!strcmp(stage,"intent_draft")) {
        assert(!strcmp(text,"deepseek") && !input[0] && !effects && !sample_count);
        ++draft_notices;
    }
    if(!strcmp(stage,"intent_final")) {
        assert(!strcmp(text,"route_revised") && !effects && !sample_count);
        ++draft_revisions;
    }
}
static void draft_checks(void)
{
    scenario=BLUE;reset();inject_draft=true;
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),draft_notice));
    assert(!effects && !input[0] && !commits);
    bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
    assert(!delegate && effects==1 && draft_notices==1 && draft_revisions==1);
    assert(!strcmp(input,original));agent_end_turn(&core);
}
static void warm_checks(void)
{
    scenario=PLAIN;reset();warm_start();
    assert(!esp_agent_voice_fast_poll_idle());capture_intact();
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    assert(!esp_agent_voice_fast_ready() && opens==1 && updates==1 && appends==1);
    bool delegate=false;assert(!finish_join(AGENT_OK,&delegate) && !delegate);
    assert(opens==1 && closes==1 && sample_count==2 && !socket_open && !speaker);
    assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
    assert(users==1 && turns==1 && !errors);agent_end_turn(&core);

    reset();warm_start();esp_agent_voice_fast_discard();esp_agent_voice_fast_discard();
    assert(!esp_agent_voice_fast_ready() && !socket_open && closes==1 && !stream_opens);
    assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_CONFIG);
    warm_start();assert(opens==2);esp_agent_voice_fast_discard();assert(closes==2);agent_end_turn(&core);

    reset();warm_start();ticks+=60000;
    assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_TIMEOUT);
    assert(!socket_open && !esp_agent_voice_fast_ready() && closes==1 && !stream_opens);agent_end_turn(&core);

    const char *unexpected[]={
        "{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}",
        "{\"type\":\"response.text.delta\",\"delta\":\"unexpected\"}",
        "{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjQ=\"}",
        "{\"type\":\"conversation.item.input_audio_transcription.completed\",\"transcript\":\"unexpected\"}"
    };
    for(unsigned i=0;i<sizeof(unexpected)/sizeof(*unexpected);++i) {
        reset();strcpy(input,"unchanged");warm_start();enqueue(unexpected[i]);
        assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_PROTOCOL);
        assert(!esp_agent_voice_fast_ready() && !socket_open && closes==1 && !sample_count && !stream_opens && !appends);
        assert(!strcmp(input,"unchanged"));capture_intact();agent_end_turn(&core);
    }

    reset();warm_start();enqueue("{\"type\":\"rate_limits.updated\",\"limits\":[{\"name\":\"requests\",\"remaining\":50}]}");
    assert(!esp_agent_voice_fast_poll_idle() && head==tail && !offset && socket_open);
    /* Simulate another work-lock owner overwriting idle scratch. A later event
     * must start afresh, never reference a partial JSON prefix from last poll. */
    size_t bytes;char *scratch=agent_engine_scratch(&engine,&bytes);scratch+=capture_capacity;
    memset(scratch,0xd3,AGENT_RT_SCRATCH);
    enqueue("{\"type\":\"rate_limits.updated\",\"limits\":[]}");
    assert(!esp_agent_voice_fast_poll_idle() && head==tail && !offset);capture_intact();
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    assert(!finish_join(AGENT_OK,&delegate));assert(opens==1 && closes==1);agent_end_turn(&core);

    reset();warm_start();stall_fragment=true;
    enqueue("{\"type\":\"rate_limits.updated\",\"limits\":[{\"name\":\"requests\",\"remaining\":50}]}");
    uint64_t start=ticks;assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_TIMEOUT);
    assert(ticks-start>=1000 && ticks-start<=1005 && !socket_open && !esp_agent_voice_fast_ready() && closes==1);
    capture_intact();agent_end_turn(&core);

    reset();warm_start();inject_capture_response=true;read_limit=512;
    /* Capture now owns reply/VAD memory, but warm ownership has transferred.
     * Unsolicited response text must still fail before reading reply counters. */
    agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
    assert(error==AGENT_ERR_PROTOCOL && joined && !stream_opens && closes==1);
    assert(finish_join(error,&delegate)==AGENT_ERR_PROTOCOL);
    assert(!delegate && !speaker && !socket_open && !sample_count);agent_end_turn(&core);

    reset();warm_start();no_endpoint=true;
    error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
    assert(error==AGENT_ERR_TIMEOUT && joined && !commits && !stream_opens);
    assert(finish_join(error,&delegate)==AGENT_ERR_TIMEOUT);
    assert(!delegate && !speaker && !socket_open && !sample_count && !creates);agent_end_turn(&core);

    reset();warm_start();stall_send=true;start=ticks;
    error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
    assert(error==AGENT_ERR_TIMEOUT && ticks-start>=15000 && ticks-start<=15020);
    assert(joined && !commits && !appends && !stream_opens);
    assert(finish_join(error,&delegate)==AGENT_ERR_TIMEOUT);
    assert(!delegate && !speaker && !socket_open && !sample_count);agent_end_turn(&core);
}
static void diagnostic_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    bool session=!strcmp(stage,"fast_session"),start=!strcmp(stage,"cloud_speech_begin"),end=!strcmp(stage,"cloud_speech_end");
    if(!session && !start && !end)return;
    assert(text && strlen(text)<256 && !strstr(text,"do-not-log") && !strstr(text,"private"));
    cJSON *json=agent_json_parse(text,strlen(text));assert(cJSON_IsObject(json));
    assert(cJSON_GetArraySize(json)==3);
    if(session) {
        ++session_notices;
        {
            assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(json,"type")));
            assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(json,"threshold")));
            assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(json,"silence_duration_ms")));
        }
    } else {
        if(start)++start_notices;else ++end_notices;
        assert(!strcmp(agent_json_string(json,"item_id"),"i\"1"));
        assert(cJSON_GetObjectItemCaseSensitive(json,"input_samples")->valueint==ESP_AGENT_FAST_UPLOAD_SAMPLES);
        assert(cJSON_GetObjectItemCaseSensitive(json,start?"audio_start_ms":"audio_end_ms")->valueint==(start?0:32));
    }
    cJSON_Delete(json);
}
static void diagnostic_checks(void)
{
    for(unsigned missing=0;missing<2;++missing) {
        scenario=PLAIN;reset();diagnostic_echo=true;missing_echo=missing!=0;read_limit=7;
        if(missing) {
            assert(esp_agent_voice_fast_warm(&engine,input,sizeof(input),diagnostic_notice)==AGENT_ERR_PROTOCOL);
            assert(!session_notices && !socket_open && !speaker && !commits && !appends);
            agent_end_turn(&core);continue;
        }
        assert(!esp_agent_voice_fast_warm(&engine,input,sizeof(input),diagnostic_notice));
        assert(session_notices==1 && !start_notices && !end_notices);
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),diagnostic_notice));
        assert(!start_notices && !end_notices && appends==1 && opens==1 && !commits);
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
        assert(commits==1);
        assert(!delegate && !effects && sample_count==2 && !socket_open && !speaker);
        agent_end_turn(&core);
    }
    scenario=PLAIN;reset();bad_vad_echo=true;
    assert(esp_agent_voice_fast_warm(&engine,input,sizeof(input),NULL)==AGENT_ERR_PROTOCOL);
    assert(!socket_open && !speaker && !commits && !appends);agent_end_turn(&core);

    reset();tail_samples=73;
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    assert(joined && appends==2 && uploaded_samples==ESP_AGENT_FAST_UPLOAD_SAMPLES+73 && !commits && !creates && !speaker);
    bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
    assert(commits==1 && creates==1 && !delegate && !socket_open);agent_end_turn(&core);
}
static void asr_activity_checks(void)
{
    static const struct {const char *fields;unsigned hints;} cases[]={
        {"\"text\":\"请\",\"stash\":\"\"",0},
        {"\"text\":\"请\",\"stash\":\"记住\"",1},
        {"\"text\":\"\",\"stash\":\"你好\"",1},
        {"\"text\":\"嗯\",\"stash\":\"...\"",0},
        {"\"text\":\"，。！\",\"stash\":\"😀\"",0},
        {"\"text\":\"he\",\"stash\":\"llo\"",1},
        {"\"text\":\"ok\",\"stash\":\"\"",0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=PLAIN;reset();read_limit=512;char wire[256];
        snprintf(wire,sizeof(wire),"{\"type\":\"conversation.item.input_audio_transcription.delta\",%s}",cases[i].fields);
        activity_wire=wire;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        assert(activity_hints==cases[i].hints && !input[0] && !commits && !effects && !speaker);
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
        assert(!delegate && !strcmp(input,original) && !effects);
        assert(activity_hints==cases[i].hints);agent_end_turn(&core);
    }
}
static void policy_checks(void)
{
    static const struct {const char *text;bool delegate;} cases[]={
        {"我刚给这盏灯取的名字是什么？",true},{"刚才我说了什么？",true},
        {"之前让你记下的事情呢？",true},{"上次那首歌叫什么？",true},
        {"请记住，这盏灯叫小星星。",true},{"你记得我喜欢的颜色吗？",true},
        {"頭先盞燈叫咩名？",true},{"記住，盞燈叫小星星。",true},
        {"把GPIO8设为高电平。",true},{"屏幕上显示时间。",true},{"播放一分钟音乐。",true},
        {"你叫什么名字？",false},{"请介绍一下你自己。",false}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=PLAIN;reset();strcpy(original,cases[i].text);
        /* Provider ignores its delegate tool and attempts a spoken guess. */
        native_reply=cases[i].delegate?"不记得。":"你好，我是小言。";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
        assert(delegate==cases[i].delegate && !strcmp(input,original));
        assert(!effects && !results && creates==1 && opens==1 && closes==1 && !socket_open && !speaker);
        assert(sample_count==(delegate?0u:2u));
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
        assert(users==(delegate?0u:1u) && turns==users && !errors);
        agent_end_turn(&core);
    }
    for(unsigned empty=0;empty<3;++empty) {
        scenario=PLAIN;reset();strcpy(original,empty==0?"":empty==1?" \r\n\t":"你好。");
        asr_after_response=empty==2;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;assert(finish_join(AGENT_OK,&delegate)==AGENT_ERR_PROTOCOL);
        assert(!delegate && !input[0] && !sample_count && !effects && !results && !socket_open && !speaker);
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
        assert(!users && !turns && !errors);agent_end_turn(&core);
    }
    scenario=PLAIN;reset();strcpy(original,"请记住我喜欢蓝色。");cancel_on_close=true;
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    bool delegate=false;assert(finish_join(AGENT_OK,&delegate)==AGENT_ERR_CANCELLED);
    assert(atomic_load(&core.cancelled) && !delegate && !sample_count && !effects && !socket_open);
    assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
    assert(users==1 && !turns && errors==1);agent_end_turn(&core);

    /* A policy hit before the endpoint no longer closes the input session.
     * Unrelated capture/join failures must still never become a handoff. */
    for(unsigned failure=0;failure<2;++failure) {
        scenario=PLAIN;reset();strcpy(original,"之前的名字是什么？");asr_before_endpoint=true;read_limit=512;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        agent_err_t incoming=failure?AGENT_ERR_NETWORK:AGENT_ERR_CANCELLED;
        assert(finish_join(incoming,&delegate)==incoming);
        assert(!delegate && !sample_count && !effects && !stream_opens && !socket_open);
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
        assert(users==1 && errors==1 && !turns);agent_end_turn(&core);
    }
    scenario=PLAIN;reset();strcpy(original,"上次说了什么？");drain_failure=AGENT_ERR_NETWORK;
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    assert(finish_join(AGENT_OK,&delegate)==AGENT_ERR_NETWORK);
    assert(!delegate && !sample_count && !effects && !socket_open);agent_end_turn(&core);

    /* A changed late final must neither replay an audible answer nor repeat
     * a tool effect. The original accepted input remains the WAL source. */
    for(unsigned effect=0;effect<2;++effect) {
        scenario=effect?BLUE:PLAIN;reset();late_transcript="我刚给灯取的名字是什么？";
        read_light=effect!=0;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        assert(finish_join(AGENT_OK,&delegate)==AGENT_ERR_PROTOCOL);
        assert(!delegate && effects==effect && sample_count==(effect?0u:2u) && !strcmp(input,original));
        assert(!socket_open && !speaker);agent_end_turn(&core);
    }
    scenario=BLUE;reset();delegate_after_tool=true;
    read_light=true;
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    assert(finish_join(AGENT_OK,&delegate)==AGENT_ERR_PROTOCOL);
    assert(!delegate && effects==1 && results==1 && creates==2 && !sample_count);
    assert(!socket_open && !speaker);agent_end_turn(&core);
}
static void provider_failure_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(strcmp(stage,"provider_response") && strcmp(stage,"asr_failed"))return;
    assert(text && strlen(text)<320 && !strstr(text,"do-not-log") && !strstr(text,"private"));
    cJSON *json=agent_json_parse(text,strlen(text));assert(cJSON_IsObject(json));
    assert(cJSON_GetArraySize(json)==4);
    if(failed_asr) {
        assert(!strcmp(stage,"asr_failed"));
        assert(!strcmp(agent_json_string(json,"code"),"transcription_failed"));
        assert(!strcmp(agent_json_string(json,"type"),"transcription_error"));
        assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(json,"status")));
    } else {
        assert(!strcmp(stage,"provider_response"));
        assert(!strcmp(agent_json_string(json,"status"),"failed"));
        assert(!strcmp(agent_json_string(json,"reason"),"provider_failure"));
        assert(!strcmp(agent_json_string(json,"code"),"overloaded"));
    }
    ++provider_notices;cJSON_Delete(json);
}
static void provider_failure_checks(void)
{
    for(unsigned asr=0;asr<2;++asr) {
        scenario=asr?PLAIN:RESPONSE_ERROR;reset();failed_asr=asr!=0;read_limit=512;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),provider_failure_notice));
        uint64_t at=ticks;bool delegate=false;
        assert(finish_join(AGENT_OK,&delegate)==AGENT_ERR_SERVER);
        assert(provider_notices==1 && ticks-at<2000 && !delegate && !sample_count && !effects && !results);
        assert(!speaker && !socket_open && opens==1 && closes==1);
        if(asr)assert(!input[0]);
        agent_end_turn(&core);
    }
}
static void acknowledgement_checks(void)
{
    /* Model-selected complex work without a keyword policy hit is also safe:
     * the native function item gates its preamble before PCM can be played. */
    scenario=MIXED_AUDIO_DELEGATE;reset();declared_function=true;
    delegate_arguments="{\"ack\":\"噢，让我认真想想。\",\"language\":\"zh\"}";
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    bool routed=false;assert(!finish_join(AGENT_OK,&routed));
    assert(routed && !sample_count && !effects && !results && !speaker && !socket_open);
    agent_end_turn(&core);
    /* In real sessions the final ASR can follow the tool transition and PCM.
     * Neither metadata nor inaudible PCM may execute anything without it. */
    for(unsigned empty=0;empty<2;++empty) {
        scenario=MIXED_AUDIO_DELEGATE;reset();declared_function=transition_before_asr=asr_after_response=true;
        strcpy(original,empty?"":"之前灯的名字是什么？");
        delegate_arguments="{\"ack\":\"噢，我查查灯的名字。\",\"language\":\"zh\"}";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;agent_err_t e=finish_join(AGENT_OK,&delegate);
        assert(e==(empty?AGENT_ERR_PROTOCOL:AGENT_OK) && delegate==!empty);
        assert(!sample_count && !effects && !results && !speaker && !socket_open);agent_end_turn(&core);
    }
    for(unsigned cancel=0;cancel<2;++cancel) {
        scenario=MIXED_AUDIO_DELEGATE;reset();declared_function=true;
        strcpy(original,"请查询之前的名字。");
        cancel_after_pcm_event=cancel!=0;long_function_audio=cancel==0;read_limit=512;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;agent_err_t e=finish_join(AGENT_OK,&delegate);
        assert(e==(cancel?AGENT_ERR_CANCELLED:AGENT_OK) && delegate==!cancel);
        assert(!sample_count && !effects && !results && !speaker && !socket_open);
        assert(!esp_agent_voice_fast_ack()->text[0]);agent_end_turn(&core);
    }
    /* Production traces: final ASR and inaudible padding may arrive in either
     * order; spoken acknowledgement bytes may precede the native delegate. */
    for(unsigned late=0;late<2;++late) {
        scenario=MIXED_AUDIO_DELEGATE;reset();silent_before_asr=late!=0;
        strcpy(original,"我刚给这盏灯取的名字是什么？");
        delegate_arguments="{\"ack\":\"噢，我找找灯的名字。\",\"language\":\"zh\"}";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
        assert(delegate && !sample_count && !effects && !results && !speaker && !socket_open);
        assert(!strcmp(esp_agent_voice_fast_ack()->text,"噢，我找找灯的名字。"));
        agent_end_turn(&core);
    }
    static const struct {const char *arguments,*text,*language;bool valid;} cases[]={
        {"{\"ack\":\"噢，想知道灯的名字呀，让我想想。\",\"language\":\"zh\"}","噢，想知道灯的名字呀，让我想想。","zh",true},
        {"{\"ack\":\"嗯，想睇返盞燈個名呀，等我諗諗。\",\"language\":\"yue\"}","嗯，想睇返盞燈個名呀，等我諗諗。","yue",true},
        {"{\"ack\":\"嗯，我想想这个名字。\"}","嗯，我想想这个名字。","",true},
        {"{}","","",true},
        {"{\"ack\":\"\"}",NULL,NULL,false},
        {"{\"ack\":\"   \"}",NULL,NULL,false},
        {"{\"ack\":123}",NULL,NULL,false},
        {"{\"ack\":\"ok\",\"language\":null}",NULL,NULL,false},
        {"{\"ack\":\"ok\",\"language\":\"en\"}",NULL,NULL,false},
        {"{\"ack\":\"ok\",\"action\":\"done\"}",NULL,NULL,false},
        {"{\"ack\":\"bad\\nline\"}",NULL,NULL,false},
        {"{\"ack\":\"123456789012345678901234\"}","123456789012345678901234","",true},
        {"{\"ack\":\"12345678901234567890123456789012\"}","12345678901234567890123456789012","",true},
        {"{\"ack\":\"123456789012345678901234567890123\"}",NULL,NULL,false},
        {"{\"ack\":\"12345678901234567890123456789012345\"}",NULL,NULL,false}
    };
    for(unsigned early=0;early<2;++early)for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=DELEGATE;reset();strcpy(original,"之前灯的名字是什么？");asr_before_endpoint=early!=0;
        delegate_arguments=cases[i].arguments;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        if(error!=(cases[i].valid?AGENT_OK:AGENT_ERR_ARGUMENT) || delegate!=cases[i].valid)
            fprintf(stderr,"ack case %u early %u: %s delegate %u\n",i,early,agent_err_name(error),delegate);
        assert(error==(cases[i].valid?AGENT_OK:AGENT_ERR_ARGUMENT) && delegate==cases[i].valid);
        const esp_agent_voice_ack_t *ack=esp_agent_voice_fast_ack();
        if(cases[i].valid)assert(!strcmp(ack->text,cases[i].text) && !strcmp(ack->language,cases[i].language));
        else assert(!ack->text[0]);
        assert(!sample_count && !effects && !results && !socket_open && !speaker);
        assert(!strcmp(input,original) && creates==1 && opens==1 && closes==1);
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
        assert(users==!cases[i].valid && errors==users && !turns);agent_end_turn(&core);
    }
    scenario=BLUE;reset();strcpy(original,"刚才那盏灯设成蓝色。") ;
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
    assert(delegate && !effects && !results && !sample_count && !esp_agent_voice_fast_ack()->text[0]);
    agent_end_turn(&core);
}
static void progress_checks(void)
{
    static const struct {const char *input;bool cantonese;unsigned kind;} cases[]={
        {"请帮我查看之前的名字。",false,1},{"啱啱嗰個名係咩？",true,1},
        /* Existing routing treats a lamp plus '先' as compound: preserve it. */
        {"頭先盞燈叫咩名？",true,0},
        {"帮我睇下屏幕。",true,0},{"请播放一分钟音乐。",false,0},
        {"请记住，我给这盏灯取名叫小星星。",false,2},
        {"唔該記低，盞燈叫小星星。",true,2},
        {"请记住",false,0},{"请记住，小星星，不要记。",false,0},
        {"请不要保存之前的名字。",false,0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        atomic_bool cancelled=false;esp_agent_voice_progress_t progress={0};
        progress_begins=progress_joins=0;progress_begin_error=progress_join_error=AGENT_OK;
        assert(!esp_agent_voice_progress_join(&progress) && !progress_joins);
#if AGENT_CONTEXTUAL_CACHED_ACK
        _Static_assert(sizeof(progress)<=8,"Per-turn contextual state budget");
        assert(!esp_agent_voice_progress_text(&progress));
#endif
        assert(!esp_agent_voice_progress_begin(&progress,cases[i].input,&cancelled));
        assert(progress.attempted && progress.active && progress_owned && progress_cantonese==cases[i].cantonese);
#if AGENT_CONTEXTUAL_CACHED_ACK
        if(progress_kind!=cases[i].kind)
            fprintf(stderr,"contextual input %u kind %u expected %u: %s\n",i,progress_kind,cases[i].kind,cases[i].input);
        assert(progress_kind==cases[i].kind && progress.queued && !progress.completed);
        const char *text=esp_agent_voice_progress_text(&progress);
        assert(text && !strcmp(text,progress_kind==2?agent_progress_memory_text(cases[i].cantonese):
            progress_kind==1?agent_progress_search_text(cases[i].cantonese):agent_progress_text(cases[i].cantonese)));
        agent_engine_t heard={.voice_mode=true,.progress_text="untouched"};
        /* Queued audio is never included as if the user had heard it. */
        assert(!esp_agent_voice_progress_heard(&progress,1,&heard));
        assert(!strcmp(heard.progress_text,"untouched"));
#else
        assert(progress_kind==0);
#endif
        assert(esp_agent_voice_progress_begin(&progress,cases[i].input,&cancelled)==AGENT_ERR_BUSY);
        assert(progress_begins==1 && !progress_joins);
        assert(!esp_agent_voice_progress_join(&progress) && !progress_owned && !progress.active);
#if AGENT_CONTEXTUAL_CACHED_ACK
        assert(progress.completed);
        assert(!esp_agent_voice_progress_heard(&progress,0,&heard));
        assert(esp_agent_voice_progress_heard(&progress,1,&heard));
        assert(!strcmp(heard.progress_text,text) && !strcmp(heard.progress_language,cases[i].cantonese?"yue":"zh"));
        heard.voice_mode=false;
        assert(!esp_agent_voice_progress_heard(&progress,1,&heard));
#endif
        assert(!esp_agent_voice_progress_join(&progress) && progress_joins==1);
        /* Completion does not authorize a second acknowledgement in this turn. */
        assert(esp_agent_voice_progress_begin(&progress,cases[i].input,&cancelled)==AGENT_ERR_BUSY);
    }
    for(unsigned mode=0;mode<4;++mode) {
        atomic_bool cancelled=mode==0;esp_agent_voice_progress_t progress={0};
        progress_begins=progress_joins=0;
        progress_begin_error=mode==1?AGENT_ERR_MEMORY:AGENT_OK;
        progress_join_error=mode==2?AGENT_ERR_CANCELLED:mode==3?AGENT_ERR_NETWORK:AGENT_OK;
        agent_err_t begun=esp_agent_voice_progress_begin(&progress,"请帮我查一下。",&cancelled);
        assert(begun==(mode==0?AGENT_ERR_CANCELLED:progress_begin_error));
        if(mode==2)atomic_store(&cancelled,true);
        assert(esp_agent_voice_progress_join(&progress)==progress_join_error);
        assert(!progress.active && !progress_owned && progress_begins==(mode?1u:0u) && progress_joins==progress_begins);
#if AGENT_CONTEXTUAL_CACHED_ACK
        assert(!progress.completed);
        agent_engine_t heard={.voice_mode=true};
        assert(!esp_agent_voice_progress_heard(&progress,1,&heard));
        assert(!heard.progress_text);
#endif
        assert(!esp_agent_voice_progress_join(&progress) && progress_joins==progress_begins);
    }
}
static void empty_route_checks(void)
{
    atomic_bool cancelled=false;
    esp_agent_voice_progress_t progress={0};
    esp_agent_voice_ack_t ack={0};
    progress_begins=progress_joins=0;progress_begin_error=progress_join_error=AGENT_OK;
    /* A staged candidate must not steal the pending producer's speaker. */
    assert(!esp_agent_voice_progress_fallback(&progress,&ack,true,false,"请帮我查一下。",&cancelled));
    assert(!progress.attempted && !progress.active && !progress_owned && !progress_begins);
    ack.pending=true;
    assert(!esp_agent_voice_progress_fallback(&progress,&ack,false,false,"请帮我查一下。",&cancelled));
    assert(!progress.attempted && !progress_owned && !progress_begins);
    ack.pending=false;ack.spoken=true;
    assert(!esp_agent_voice_progress_fallback(&progress,&ack,false,false,"请帮我查一下。",&cancelled));
    ack.spoken=false;strcpy(ack.text,"嗯，我来查一下。");
    assert(!esp_agent_voice_progress_fallback(&progress,&ack,false,false,"请帮我查一下。",&cancelled));
    ack.text[0]=0;
    assert(!esp_agent_voice_progress_fallback(&progress,&ack,false,true,"把灯改成。",&cancelled));
    assert(!progress.attempted && !progress_owned && !progress_begins);
    /* A fully joined miss admits exactly one cached job. Cancellation or a
     * partial board allocation still requires the same terminal join. */
    assert(!esp_agent_voice_progress_fallback(&progress,&ack,false,false,"帮我睇下屏幕。",&cancelled));
    assert(progress.active && progress_owned && progress_cantonese && progress_begins==1);
    assert(esp_agent_voice_progress_fallback(&progress,&ack,false,false,"帮我睇下屏幕。",&cancelled)==AGENT_ERR_BUSY);
    atomic_store(&cancelled,true);progress_join_error=AGENT_ERR_CANCELLED;
    assert(esp_agent_voice_progress_join(&progress)==AGENT_ERR_CANCELLED);
    assert(!progress.active && !progress_owned && progress_joins==1);
    assert(!esp_agent_voice_progress_join(&progress) && progress_joins==1);
    progress=(esp_agent_voice_progress_t){0};atomic_store(&cancelled,false);
    progress_begin_error=AGENT_ERR_MEMORY;progress_join_error=AGENT_OK;
    assert(esp_agent_voice_progress_fallback(&progress,&ack,false,false,"请帮我查一下。",&cancelled)==AGENT_ERR_MEMORY);
    assert(progress.active && progress_owned);
    assert(!esp_agent_voice_progress_join(&progress) && !progress_owned && progress_joins==2);
}
static void native_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(!strcmp(stage,"progress_native_fallback")) {
        assert(text && strlen(text)<sizeof(native_fallback_reason));
        ++native_fallback_notices;strcpy(native_fallback_reason,text);
    } else if(!strcmp(stage,"progress_native_gate")) {
        native_before_arguments=!strcmp(text,"pending_ack_validation");
    } else if(!strcmp(stage,"progress_native_stats")) {
        cJSON *root=agent_json_parse(text,strlen(text));assert(root);
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"native"))==esp_agent_voice_fast_ack_native());
        const cJSON *discarded=cJSON_GetObjectItemCaseSensitive(root,"discarded_samples");
        assert(cJSON_IsNumber(discarded));native_discarded=(unsigned)discarded->valueint;
        ++native_stats_notices;cJSON_Delete(root);
    }
}
static void native_ack_checks(void)
{
    scenario=PLAIN;reset();long_unrouted_audio=true;
    strcpy(original,"刚才这盏灯叫什么名字？");
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),native_notice));
    bool discarded_delegate=false;
    assert(!finish_join(AGENT_OK,&discarded_delegate));
    assert(discarded_delegate && !sample_count && !effects && !results && !socket_open && !speaker);
    assert(native_discarded>=1600 && native_discarded<1728 && closed_with_unread);
    assert(!strcmp(input,original) && !esp_agent_voice_fast_ack()->text[0]);
    agent_end_turn(&core);
    for(unsigned split=1;split<=256;split*=4) {
        scenario=DELEGATE;reset();native_ack=true;read_limit=split;
        delegate_arguments="{\"ack\":\"哦，我查查名字。\",\"language\":\"zh\"}";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),native_notice));
        assert(!esp_agent_voice_fast_ack_native());
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
        const esp_agent_voice_ack_t *ack=esp_agent_voice_fast_ack();
        assert(delegate && ack->spoken && !effects && !results && sample_count==2 && drained==AGENT_OK);
        assert(esp_agent_voice_fast_ack_native() && native_stats_notices==1 && !native_fallback_notices && !native_discarded);
        assert(!strcmp(ack->text,"噢，我找找你给灯起的名字。") && !strcmp(ack->language,"zh"));
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
        assert(!users && !turns && !errors);agent_end_turn(&core);
    }
    for(unsigned mode=0;mode<7;++mode) {
        scenario=mode==3?MIXED_DELEGATE_TOOL:DELEGATE;reset();native_ack=true;
        delegate_arguments="{\"ack\":\"嗯，我想想。\",\"language\":\"zh\"}";
        if(mode==0)early_arguments="{\"ack\":\"different\",\"language\":\"zh\"}";
        if(mode==1)bad_ack_item=true;
        if(mode==2)cancel_after_pcm_event=true;
        if(mode==4)drain_failure=AGENT_ERR_CANCELLED;
        if(mode==5)asr_after_response=true;
        if(mode==6)early_arguments="{\"ack\":\"not complete";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),native_notice));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        agent_err_t expected=mode==5?AGENT_OK:mode==2 || mode==4?AGENT_ERR_CANCELLED:AGENT_ERR_PROTOCOL;
        assert(error==expected && delegate==(mode==5));
        assert(sample_count==(mode==1 || mode==5?0u:2u));
        assert(!esp_agent_voice_fast_ack()->spoken && !effects && !results && !socket_open && !speaker);
        assert(esp_agent_voice_fast_ack_native()==(sample_count!=0));
        if(mode==5) {
            assert(native_fallback_notices==1 && native_stats_notices==1 && native_discarded==2);
            assert(!strcmp(native_fallback_reason,"asr_pending"));
        }
        agent_end_turn(&core);
    }
    /* Real provider ordering: audio is emitted between argument fragments.
     * Incomplete/invalid final JSON still cannot delegate or execute tools. */
    for(unsigned valid=0;valid<2;++valid) {
        scenario=DELEGATE;reset();native_ack=true;
        early_arguments="{\"ack\":\"嗯";
        late_arguments=valid?"，我想想。\",\"language\":\"zh\"}":"，我想想。\",\"language\":\"bad\"}";
        delegate_arguments=valid?"{\"ack\":\"嗯，我想想。\",\"language\":\"zh\"}":"{\"ack\":\"嗯，我想想。\",\"language\":\"bad\"}";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),native_notice));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        assert(error==(valid?AGENT_OK:AGENT_ERR_ARGUMENT) && delegate==(valid!=0));
        assert(native_before_arguments && esp_agent_voice_fast_ack_native() && sample_count==2);
        assert(esp_agent_voice_fast_ack()->spoken==(valid!=0) && !effects && !results);
        assert(native_stats_notices==1 && !native_fallback_notices && !native_discarded);
        assert(!speaker && !socket_open);agent_end_turn(&core);
    }
    for(unsigned padding=0;padding<2;++padding) {
        scenario=DELEGATE;reset();native_ack=true;
        native_transcript=padding?" \t噢，我找找你给灯起的名字。\r\n\n":"噢，我\n找找你给灯起的名字。";
        delegate_arguments="{\"ack\":\"噢，我查查。\",\"language\":\"zh\"}";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),native_notice));
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate) && delegate);
        assert(esp_agent_voice_fast_ack_native()==(padding!=0) && sample_count==(padding?2u:0u));
        assert(esp_agent_voice_fast_ack()->spoken==(padding!=0) && !effects && !results);
        if(padding) {
            assert(!strcmp(esp_agent_voice_fast_ack()->text,"噢，我找找你给灯起的名字。"));
            assert(!native_fallback_notices);
        } else assert(native_fallback_notices==1 && !strcmp(native_fallback_reason,"transcript_limit"));
        assert(!speaker && !socket_open);agent_end_turn(&core);
    }
}
static void receipt_metadata_checks(void)
{
    static const char *const malformed[]={
        "{\"ack\":\"嗯，我来记一下。\",\"language\":\"Mandarin\"}",
        "{\"ack\":\"嗯，我来记一下。\",\"extra\":true}",
        "{\"ack\":null}","[0]","{\"ack\":42}"};
    for(unsigned mode=0;mode<11;++mode) {
        scenario=mode==7?MIXED_DELEGATE_TOOL:DELEGATE;reset();
        native_ack=task_alias=true;
        strcpy(original,"请记住，我给这盏灯取名叫小星星。");
        native_transcript="嗯，小星星，我来记一下。";
        delegate_arguments=malformed[mode<5?mode:0];
        if(mode==5)strcpy(original,"你好。");
        if(mode==6)native_transcript="小星星已经记住了。";
        if(mode==8)cancel_on_close=true;
        if(mode==9)early_arguments="{\"ack\":null}";
        if(mode==10)cache_seal_error=AGENT_ERR_STORAGE;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        agent_err_t expected=mode<5?AGENT_OK:mode<=6?AGENT_ERR_ARGUMENT:
            mode==8?AGENT_ERR_CANCELLED:mode==10?AGENT_ERR_STORAGE:AGENT_ERR_PROTOCOL;
        if(error!=expected)fprintf(stderr,"receipt metadata %u: %s expected %s\n",
            mode,agent_err_name(error),agent_err_name(expected));
        assert(error==expected && delegate==(mode<5));
        assert(!strcmp(input,original) && !effects && !results && !socket_open && !speaker);
        assert(esp_agent_voice_fast_ack()->spoken==(mode<5));
        if(mode<5) {
            const esp_agent_voice_ack_t *ack=esp_agent_voice_fast_ack();
            assert(!strcmp(ack->text,native_transcript) && !strcmp(ack->language,"zh"));
            assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
            assert(!users && !turns && !errors); /* Full engine owns the turn. */
        }
        agent_end_turn(&core);
    }
}
static void detached_ack_checks(void)
{
    for(unsigned mode=0;mode<4;++mode) {
        scenario=DELEGATE;reset();task_alias=true;native_ack=mode!=0;
        task_name_mismatch=mode==2;
        if(mode==3)scenario=MIXED_DELEGATE_TOOL;
        delegate_arguments="{\"ack\":\"嗯嗯，我来查一下。\",\"language\":\"zh\"}";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;agent_err_t e=finish_join(AGENT_OK,&delegate);
        assert(e==(mode>=2?AGENT_ERR_PROTOCOL:AGENT_OK) && delegate==(mode<2));
        assert(!effects && !results && !socket_open && !speaker);
        agent_end_turn(&core);
    }
    for(unsigned mode=0;mode<5;++mode) {
        scenario=DELEGATE;reset();native_ack=true;
        delegate_arguments="{\"ack\":\"噢，我查查。\",\"language\":\"zh\"}";
        if(mode==3)cache_begin_error=AGENT_ERR_FULL;
        if(mode==4)cache_seal_error=AGENT_ERR_STORAGE;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;agent_err_t e=esp_agent_voice_fast_finish(AGENT_OK,&delegate);
        if(mode>=3) {
            assert(e==(mode==3?AGENT_ERR_FULL:AGENT_ERR_STORAGE));
            assert(!delegate && !speaker && !esp_agent_voice_fast_ack()->pending);
        } else {
            assert(!e && delegate && speaker && cache_sealed && !socket_open && !ring);
            assert(esp_agent_voice_fast_ack()->pending && !esp_agent_voice_fast_ack()->spoken);
            assert(esp_agent_voice_fast_warm(&engine,input,sizeof(input),NULL)==AGENT_ERR_BUSY);
            assert(esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL)==AGENT_ERR_BUSY);
            /* Engine can immediately reclaim every scratch byte while board
             * playback owns only its isolated immutable audio source. */
            memset(engine.buffer,0xa5,AGENT_ENGINE_BUFFER_SIZE);
            assert(cached_pcm[0]==0x3412 && cached_pcm[1]==0x5678);
            if(mode==1)atomic_store(&core.cancelled,true);
            agent_err_t why=mode==2?AGENT_ERR_NETWORK:AGENT_OK;
            agent_err_t want=mode==1?AGENT_ERR_CANCELLED:why;
            assert(esp_agent_voice_fast_ack_join(why)==want);
            assert(!speaker && !esp_agent_voice_fast_ack()->pending);
            assert(esp_agent_voice_fast_ack()->spoken==(want==AGENT_OK));
            assert(!esp_agent_voice_fast_ack_join(AGENT_OK) && stream_finishes==1);
        }
        agent_end_turn(&core);
    }
}
static void plain_ack_checks(void)
{
    for(unsigned mode=0;mode<15;++mode) {
        scenario=PLAIN;reset();strcpy(original,"请记住这盏灯叫小星星。");
        /* No terminal punctuation: retain coverage of whole-response native
         * validation, independent of the new completed-sentence handoff. */
        native_reply="噢，小星星，我来记一下";
        if(mode==1)native_reply="小星星这个名字我已经记住了。";
        if(mode==2) {plain_suffix="已经记住了。";plain_final="噢，小星星，我来记一下已经记住了。";}
        if(mode==3)plain_final="另一句话。";
        if(mode==4)cancel_after_pcm_event=true;
        if(mode==5)start_fault=START_AFTER_PCM;
        if(mode==6)cache_begin_error=AGENT_ERR_FULL;
        if(mode==7)cache_seal_error=AGENT_ERR_STORAGE;
        if(mode==8)plain_late_function=true;
        if(mode==9)native_reply="好呀，等我記低個名先";
        if(mode==10) {native_reply="小星星，我记住啦。";long_unrouted_audio=true;}
        if(mode==11) {native_reply="小星星灯名已记。";long_unrouted_audio=true;}
        if(mode>=12) {plain_late_asr=true;native_reply=mode==12?"小星星灯名已记。":"好呀，等我記低個名先";}
        if(mode==14)native_reply="嗯嗯，好的，我来记一下，你给这盏灯取名叫小星星噢";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        fault_start=ticks;bool delegate=false;
        agent_err_t error=esp_agent_voice_fast_finish(AGENT_OK,&delegate);
        agent_err_t expected=mode==3 || mode==8?AGENT_ERR_PROTOCOL:mode==4?AGENT_ERR_CANCELLED:
            mode==6?AGENT_ERR_FULL:mode==7?AGENT_ERR_STORAGE:AGENT_OK;
        assert(error==expected && delegate==(expected==AGENT_OK));
        bool accepted=mode==0 || mode==9 || mode>=13;
        if(mode==1 || (mode>=10 && mode<=12))assert(!native_cached && closed_with_unread);
        assert(cache_sealed==accepted && esp_agent_voice_fast_ack()->pending==accepted);
        assert(!effects && !results && !socket_open && creates==1 && !strcmp(input,original));
        if(accepted) {
            assert(cache_deferred && speaker && sample_count==2 && !ring);
            assert(!strcmp(esp_agent_voice_fast_ack()->text,native_reply));
            memset(engine.buffer,0xa5,AGENT_ENGINE_BUFFER_SIZE);
            assert(cached_pcm[0]==0x3412 && cached_pcm[1]==0x5678);
            assert(!esp_agent_voice_fast_ack_join(AGENT_OK));
        } else assert(!speaker && !sample_count && !esp_agent_voice_fast_ack()->spoken);
        agent_end_turn(&core);
    }
}
static void startup_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(!strcmp(stage,"fast_start_timeout")) {
        assert(text && !strcmp(text,"2500ms_without_accepted_audio"));++timeout_notices;
    }
}
static void startup_deadline_checks(void)
{
    for(unsigned fault=START_CREATED;fault<START_CASES;++fault) {
        scenario=fault==START_AFTER_TOOL?BLUE:PLAIN;reset();start_fault=fault;
        read_light=fault==START_AFTER_TOOL;
        if(fault==START_CANCEL)cancel_on_close=true;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),startup_notice));
        fault_start=ticks;bool delegate=false;
        agent_err_t error=finish_join(AGENT_OK,&delegate);
        bool successful=fault==START_AFTER_PCM;
        bool retryable=!successful && fault!=START_NO_FINAL && fault!=START_AFTER_TOOL && fault!=START_CANCEL;
        agent_err_t expected=successful || retryable?AGENT_OK:fault==START_CANCEL?AGENT_ERR_CANCELLED:AGENT_ERR_TIMEOUT;
        assert(error==expected && delegate==retryable && !socket_open && !speaker && closes==1);
        assert(effects==(fault==START_AFTER_TOOL) && sample_count==(successful?2u:0u));
        if(successful)assert(!timeout_notices && ticks-fault_start>=6000);
        else {
            assert(timeout_notices==1);
            unsigned tolerance=fault==START_AFTER_TOOL?500:1;
            assert(ticks-fault_start>=2500 && ticks-fault_start<=2500+tolerance);
        }
        if(retryable) {
            assert(!strcmp(input,original) && !esp_agent_voice_fast_ack()->text[0]);
            assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
            assert(!users && !turns && !errors && !effects && !results);
        }
        if(fault==START_NO_FINAL)assert(!input[0]);
        if(fault==START_AFTER_TOOL)assert(effects==1 && results==1 && creates==2);
        agent_end_turn(&core);
    }
}
static void history_seed(bool hit)
{
    /* A long WAL record overwrites even the WS tail of engine.buffer. Its
     * result includes UTF-8 and escaped quotes, then crosses WS fragments. */
    char *content=malloc(21000);assert(content);
    agent_json_writer_t w;agent_json_writer_init(&w,content,21000);
    agent_json_raw(&w,"{\"source_fixture\":true,\"messages\":[{\"role\":\"user\",\"content\":");
    agent_json_quote(&w,hit?"灯的名字是\"小星星\"，你好。":"不同的内容");
    agent_json_raw(&w,"},{\"role\":\"assistant\",\"content\":\"你好。\"}],\"padding\":\"");
    for(unsigned i=0;i<18500;++i)agent_json_raw(&w,"x");
    agent_json_raw(&w,"\"}");assert(!w.error);
    assert(!agent_context_emit(&context,"turn","user",content,NULL,0));free(content);
    size_t bytes;void *arena=agent_engine_scratch(&engine,&bytes);
    memset(arena,0xa5,capture_capacity);
}
static void history_checks(void)
{
    for(unsigned mode=0;mode<17;++mode) {
        scenario=PLAIN;reset();search_mode=true;
        strcpy(original,(mode==3 || mode==16)?"请记住，我刚取的名字。":mode==4?"请在屏幕显示刚取的名字。":"我刚给这盏灯取的名字是什么？");
        history_seed(mode!=1);
        if(mode==2)search_delegate=true;
        if(mode==5)search_args="{\"query\":\"名字\",\"limit\":4}";
        if(mode==6)search_mixed=true;
        if(mode==7)search_failure=AGENT_ERR_CANCELLED;
        if(mode==8)search_failure=AGENT_ERR_CORRUPT;
        if(mode==9)search_failure=AGENT_ERR_TIMEOUT;
        if(mode==10)search_repeat=true;
        if(mode==11)asr_after_response=true;
        if(mode==12)search_bad_result="{\"hits\":[{}]}";
        if(mode==13)search_bad_result="{\"hits\":false}";
        if(mode>=14)search_preamble=true;
        if(mode==15)search_mixed=true;
        read_limit=7;
        if(search_preamble)read_limit=512;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        agent_err_t expected=mode==5?AGENT_ERR_ARGUMENT:mode==6?AGENT_ERR_PROTOCOL:
            mode==7?AGENT_ERR_CANCELLED:mode==8?AGENT_ERR_CORRUPT:mode==9?AGENT_ERR_TIMEOUT:
            mode==10?AGENT_ERR_LIMIT:(mode==12 || mode==13 || mode==15)?AGENT_ERR_PROTOCOL:AGENT_OK;
        if(error!=expected)fprintf(stderr,"fast history %u expected %d got %d\n",mode,expected,error);
        assert(error==expected && !effects && !socket_open && !speaker);
        assert(delegate==(mode==1 || mode==2 || mode==3 || mode==4 || mode==16));
        assert(searches==((mode==5 || mode==6 || mode==15 || mode==16)?0u:mode==10?4u:1u));
        bool answered=mode==0 || mode==11 || mode==14;
        assert(sample_count==(answered?2u:0u) && !strcmp(input,original));
        if(answered) {
            assert(results==1 && creates==2 && core.tool_rounds==1);
            assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
            assert(users==1 && turns==1 && tool_messages==1 && !errors);
        }
        if(mode==10)assert(results==4 && creates==5 && core.tool_rounds==4);
        agent_end_turn(&core);
    }
}
static void local_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(strcmp(stage,"fast_local_reply"))return;
    assert(!socket_open && speaker && effects==1 && !sample_count && !results && creates==1);
    assert(!strcmp(text,agent_progress_light_text(strstr(original,"唔")!=NULL)));
    ++local_replies;
}
static void local_confirmation_checks(void)
{
    for(unsigned mode=0;mode<9;++mode) {
        scenario=BLUE;reset();
        if(mode==1)strcpy(original,"唔該，幫我把燈設為藍色。");
        if(mode==2)light_error=AGENT_ERR_BUSY;
        if(mode==3)light_error=AGENT_ERR_TOOL;
        if(mode==4)cancel_after_light=true;
        if(mode==5)cancel_on_close=true;
        if(mode==6)local_write_error=AGENT_ERR_NETWORK;
        if(mode==7)drain_failure=AGENT_ERR_NETWORK;
        if(mode==8)valid_light_batch=true;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),local_notice));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        const agent_err_t expected[]={AGENT_OK,AGENT_OK,AGENT_ERR_BUSY,AGENT_ERR_TOOL,
            AGENT_ERR_CANCELLED,AGENT_ERR_CANCELLED,AGENT_ERR_NETWORK,AGENT_ERR_NETWORK,AGENT_OK};
        assert(error==expected[mode] && !delegate && !socket_open && !speaker);
        assert(opens==1 && closes==1 && stream_opens==1 && stream_finishes==1);
        assert(creates==(mode==8?2u:1u) && results==(mode==8?2u:0u));
        assert(effects==(mode==2 || mode==3?0u:mode==8?2u:1u));
        assert(local_replies==(mode<2 || mode==6 || mode==7?1u:0u));
        /* Independent decoded PCM begins with five exact zeros in zh. */
        if(mode<2 || mode==7)assert(sample_count==(mode==1?15424u:18033u));
        else assert(sample_count==(mode==8?2u:0u));
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
        assert(users==1 && turns==(!error?1u:0u) && errors==(error?1u:0u));
        assert(tool_messages==(!error?(mode==8?2u:1u):0u));
        agent_end_turn(&core);
    }
}
static unsigned sentence_streams;
static void sentence_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;if(strcmp(stage,"progress_transport") || strcmp(text,"omni_sentence"))return;
    assert(socket_open && speaker && !sample_count && !effects);++sentence_streams;
}
static void sentence_stream_checks(void)
{
    for(unsigned mode=0;mode<10;++mode) {
        scenario=PLAIN;reset();sentence_streams=0;
        strcpy(original,mode==1?"唔該，記住盞燈叫小星星。":"请记住这盏灯叫小星星。");
        native_reply=mode==1?"噢，等我記低小星星個名先。":"噢，小星星，我来记一下。";
        if(mode==2) {plain_suffix="已经完成了。";plain_final="不会采纳的后缀";}
        if(mode==3)plain_late_function=true;
        if(mode==4)cancel_on_close=true;
        if(mode==5)native_reply="噢，我来记一下。已经记住了。";
        if(mode==6)strcpy(original,"你好，小言。");
        if(mode==7)plain_final="另一句话。";
        if(mode==8) {start_fault=START_AFTER_PCM;stall_ms=9000;}
        if(mode==9)cancel_after_pcm_event=true;
        read_limit=7;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),sentence_notice));
        fault_start=ticks;bool delegate=false;
        agent_err_t error=esp_agent_voice_fast_finish(AGENT_OK,&delegate);
        const agent_err_t expected[]={AGENT_OK,AGENT_OK,AGENT_ERR_PROTOCOL,AGENT_ERR_PROTOCOL,
            AGENT_ERR_CANCELLED,AGENT_OK,AGENT_OK,AGENT_ERR_PROTOCOL,AGENT_ERR_TIMEOUT,AGENT_ERR_CANCELLED};
        if(error!=expected[mode] || delegate!=(mode<2 || mode==5))
            fprintf(stderr,"sentence mode %u error %s expected %s delegate %u\n",mode,agent_err_name(error),agent_err_name(expected[mode]),delegate);
        assert(error==expected[mode] && delegate==(mode<2 || mode==5));
        bool live=mode!=5 && mode!=6;
        assert(sentence_streams==(live?1u:0u));
        assert(native_cached==live && !cache_deferred && !effects && !results && !socket_open);
        assert(sample_count==(mode==5?0u:2u) && creates==1 && closes==1);
        const esp_agent_voice_ack_t *ack=esp_agent_voice_fast_ack();
        if(mode<2) {
            assert(ack->pending && !ack->spoken && cache_sealed && speaker && !ring);
            assert(!strcmp(ack->text,native_reply) && !strcmp(ack->language,mode==1?"yue":"zh"));
            memset(engine.buffer,0xa5,AGENT_ENGINE_BUFFER_SIZE);
            assert(cached_pcm[0]==0x3412 && cached_pcm[1]==0x5678);
            assert(!esp_agent_voice_fast_ack_join(AGENT_OK));
            assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
            assert(!users && !turns && !errors);
        } else assert(!speaker && !ack->pending);
        if(mode==5)assert(!ack->text[0]);
        if(mode==8)assert(ticks-fault_start>=6000 && !timeout_notices);
        assert(!strcmp(input,original));agent_end_turn(&core);
    }
}
static void literal_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(strcmp(stage,"fast_local_intent"))return;
    assert(!socket_open && !effects && !sample_count && !strcmp(text,"device_light_set_rgb"));
    local_literal=true;
}
static void literal_light_checks(void)
{
    static const struct {const char *text;unsigned mask;} cases[]={
        {"请把四颗灯设为蓝色，请简单回答。",1}, {"请把四颗灯设为蓝色。请简单回答。",1},
        {"请把四颗灯设为蓝色.请简单回答.",1}, {"把灯调成红色。",4},
        {"请将所有灯设置为绿色",2}, {"把四个灯改成黄色吧。",6},
        {"把灯设为紫色。",5}, {"把灯设为青色。",3}, {"把灯设为白色。",7},
        {"把灯调成蓝色，不对，改成绿色。",2},
        {"请把灯调成蓝色，不对，不要蓝色，改成绿色。",2},
        {"把灯调成蓝色。不对，换成红色。",4},
        {"不要把灯设为蓝色。",0}, {"如果可以，把灯设为蓝色。",0},
        {"把灯设为蓝色或者红色。",0}, {"把灯设为蓝色，再关掉。",0},
        {"把灯设为蓝色，请检查麦克风。",0}, {"把灯设为蓝色？",0},
        {"把灯设为蓝色，请检",0}, {"把灯设为蓝色，亮度50%。",0},
        {"把灯设为蓝色，",0},
        {"请把灯设为蓝色。现在不要执行。",0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=PLAIN;reset();strcpy(original,cases[i].text);
        early_literal_metadata=i==1; /* Unexecuted call may arrive before final ASR. */
        for(unsigned k=0;k<3;++k)expected_rgb[k]=(cases[i].mask&(4u>>k))?255:0;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        assert(!error && !socket_open && !speaker && local_literal==(cases[i].mask!=0));
        assert(effects==(cases[i].mask?1u:0u) && !results);
        if(cases[i].mask) {
            assert(!delegate && closed_with_unread && sample_count==18033);
            assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
            assert(users==1 && turns==1 && tool_messages==1 && !errors);
        }
        agent_end_turn(&core);
    }
    for(unsigned fault=0;fault<4;++fault) {
        scenario=PLAIN;reset();strcpy(original,"请把灯设为蓝色。");
        if(fault==0)cancel_on_close=true;
        if(fault==1)light_error=AGENT_ERR_BUSY;
        if(fault==2)cancel_after_light=true;
        if(fault==3)local_write_error=AGENT_ERR_NETWORK;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        const agent_err_t expected[]={AGENT_ERR_CANCELLED,AGENT_ERR_BUSY,AGENT_ERR_CANCELLED,AGENT_ERR_NETWORK};
        assert(error==expected[fault] && !delegate && !socket_open && !speaker && !results);
        assert(effects==(fault>=2?1u:0u) && !sample_count);agent_end_turn(&core);
    }
    /* A stale model setter is discarded. The complete correction executes
     * exactly its final colour; standalone negation still delegates intact. */
    const char *corrections[]={"请把灯设为蓝色，不对，改成红色。","不要把灯设为蓝色。",
                              "把灯设为蓝色，不是改成绿色。","唔該，把燈調成藍色，唔係改做綠色。"};
    for(unsigned i=0;i<sizeof(corrections)/sizeof(*corrections);++i) {
        scenario=BLUE;reset();strcpy(original,corrections[i]);
        if(!i) {expected_rgb[0]=255;expected_rgb[1]=expected_rgb[2]=0;early_literal_metadata=true;}
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice));
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
        assert(delegate==(i!=0) && effects==(!i) && !results && local_literal==(!i));
        assert(sample_count==(!i?18033u:0u));
        assert(!strcmp(input,original));agent_end_turn(&core);
    }
}
static void joined_light_checks(void)
{
    for(unsigned mode=0;mode<10;++mode) {
        scenario=BLUE;reset();joined=true;
        esp_agent_voice_prefetch_set(true);
        strcpy(original,mode==1?"唔該，把燈調成藍色，唔係，改做綠色。":
            "请把灯调成蓝色。不对，不要蓝色，改成绿色。\n");
        expected_rgb[0]=expected_rgb[2]=0;expected_rgb[1]=255;
        if(mode==2)light_error=AGENT_ERR_BUSY;
        if(mode==3)light_error=AGENT_ERR_TOOL;
        if(mode==4)cancel_after_light=true;
        if(mode==5)atomic_store(&core.cancelled,true);
        if(mode==6)local_write_error=AGENT_ERR_NETWORK;
        if(mode==7)drain_failure=AGENT_ERR_NETWORK;
        if(mode==8)persist_error=AGENT_ERR_CORRUPT;
        if(mode==9)core.tool_rounds=AGENT_ROUNDS_MAX;
        const agent_err_t expected[]={AGENT_OK,AGENT_OK,AGENT_ERR_BUSY,AGENT_ERR_TOOL,
            AGENT_ERR_CANCELLED,AGENT_ERR_CANCELLED,AGENT_ERR_NETWORK,AGENT_ERR_NETWORK,
            AGENT_ERR_CORRUPT,AGENT_ERR_LIMIT};
        agent_err_t error=esp_agent_voice_fast_light(&engine,original,literal_notice);
        assert(error==expected[mode] && !speaker && !socket_open && !opens && !closes && !creates && !results);
        assert(effects==((mode<2 || mode==4 || mode==6 || mode==7 || mode==8)?1u:0u));
        assert(sample_count==((mode<2 || mode==7 || mode==8)?(mode==1?15424u:18033u):0u));
        assert(stream_opens==(mode==5?0u:1u) && stream_finishes==stream_opens);
        if(!error) {
            assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
            assert(users==1 && turns==1 && tool_messages==1 && !errors);
        }
        agent_end_turn(&core);
    }
    const char *refused[]={"不要把灯调成蓝色。","把灯调成蓝色。不对。",
        "把灯调成蓝色，再播放音乐。","把灯调成蓝色，不是改成绿色。"};
    for(unsigned i=0;i<sizeof(refused)/sizeof(*refused);++i) {
        scenario=BLUE;reset();joined=true;
        assert(esp_agent_voice_fast_light(&engine,refused[i],literal_notice)==AGENT_ERR_ARGUMENT);
        assert(!effects && !sample_count && !stream_opens && !opens);
        agent_end_turn(&core);
    }
    scenario=BLUE;reset();warm_start();
    assert(esp_agent_voice_fast_light(&engine,"把灯调成绿色。",literal_notice)==AGENT_ERR_BUSY);
    assert(socket_open && !effects && !stream_opens);
    esp_agent_voice_fast_discard();assert(!socket_open);agent_end_turn(&core);
}

static void unfinished_checks(void)
{
    const char *texts[]={"请把灯调成蓝色。不对，不。","唔係，改做。"};
    for(unsigned mode=0;mode<2;++mode)for(unsigned i=0;i<2;++i)for(unsigned cancelled=0;cancelled<2;++cancelled) {
        scenario=BLUE;reset();strcpy(original,texts[i]);
        esp_agent_voice_prefetch_set(mode!=0);cancel_on_close=cancelled!=0;
        agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
        assert(!error && joined);
        bool delegate=false;error=finish_join(error,&delegate);
        assert(error==(cancelled?AGENT_ERR_CANCELLED:AGENT_OK) && delegate==!cancelled);
        assert(!effects && !results && !sample_count && !socket_open && !speaker);
        assert(esp_agent_voice_fast_clarify() && !strcmp(input,original));
        const esp_agent_voice_ack_t *ack=esp_agent_voice_fast_ack();
        assert(!ack->text[0] && !ack->spoken && !ack->pending);
        if(!cancelled) {
            assert(drained==AGENT_OK && cued_opens==1); /* Cue drains, guessed PCM stays absent. */
            assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
            assert(!users && !turns && !errors); /* The clarification engine owns persistence. */
        }
        agent_end_turn(&core);
    }
    for(unsigned mode=0;mode<2;++mode)for(unsigned failure=0;failure<2;++failure) {
        scenario=BLUE;reset();strcpy(original,texts[0]);esp_agent_voice_prefetch_set(mode!=0);
        drain_failure=failure?AGENT_ERR_CANCELLED:AGENT_ERR_NETWORK;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;assert(finish_join(AGENT_OK,&delegate)==drain_failure);
        assert(!delegate && !sample_count && !effects && !socket_open && !speaker);agent_end_turn(&core);
    }
    scenario=PLAIN;reset();
    assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
    bool delegate=false;assert(!finish_join(AGENT_OK,&delegate));
    assert(!delegate && !esp_agent_voice_fast_clarify() && sample_count==2);agent_end_turn(&core);
}
static void early_commit_checks(void)
{
    const agent_err_t failures[]={AGENT_OK,AGENT_ERR_CORRUPT,AGENT_ERR_STORAGE,AGENT_ERR_CANCELLED,AGENT_ERR_NETWORK};
    for(unsigned fault=0;fault<5;++fault) {
        scenario=PLAIN;reset();esp_agent_voice_prefetch_set(true);tail_samples=73;
        strcpy(original,"请把灯设为蓝色，不对，改成绿色。");
        manual_preview="请把灯设为蓝色。";native_reply="已设为蓝色。";
        expected_rgb[0]=expected_rgb[2]=0;expected_rgb[1]=255;
        if(fault==4)commit_send_error=failures[fault];
        else late_capture_error=failures[fault];
        agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice);
        assert(error==failures[fault] && early_commits==1 && commits==(fault==4?0u:1u));
        assert(uploaded_samples==(prefetch_chunks-1)*ESP_AGENT_FAST_UPLOAD_SAMPLES+73 && !effects && !sample_count && !speaker);
        bool delegate=false;error=finish_join(error,&delegate);
        assert(error==failures[fault] && !delegate && !socket_open && !speaker && !replay_releases);
        assert(effects==(!fault) && sample_count==(!fault?18033u:0u));
        assert(!fault || (!input[0] && !local_literal));
        agent_end_turn(&core);
    }
    esp_agent_voice_prefetch_set(false);
}
static void prefetch_checks(void)
{
    for(unsigned fault=0;fault<5;++fault) {
        scenario=PLAIN;reset();esp_agent_voice_prefetch_set(true);
        strcpy(original,"请把灯设为蓝色，不对，不要蓝色，改成绿色。");
        expected_rgb[0]=expected_rgb[2]=0;expected_rgb[1]=255;
        if(fault==1)cancel_on_close=true;
        if(fault==2)light_error=AGENT_ERR_BUSY;
        if(fault==3)cancel_after_light=true;
        if(fault==4)local_write_error=AGENT_ERR_NETWORK;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice));
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        const agent_err_t expected[]={AGENT_OK,AGENT_ERR_CANCELLED,AGENT_ERR_BUSY,
                                     AGENT_ERR_CANCELLED,AGENT_ERR_NETWORK};
        assert(error==expected[fault] && !delegate && !socket_open && !speaker);
        assert(opens==1 && closes==1 && commits==1 && !creates && !results);
        assert(effects==((fault==0 || fault>=3)?1u:0u));
        assert(sample_count==(fault==0?18033u:0u));agent_end_turn(&core);
    }
    for(unsigned mode=0;mode<15;++mode) {
        scenario=PLAIN;reset();esp_agent_voice_prefetch_set(true);
        strcpy(original,"请介绍你自己。");native_reply="我是小言。";
        if(mode==1) {manual_preview="请记住";manual_changed=true;}
        if(mode==2)manual_no_preview=true;
        if(mode==3)no_endpoint=true;
        if(mode==4)cancel_on_close=true;
        if(mode==5)plain_final="changed output";
        if(mode==6) {strcpy(original,"请把灯设为蓝色。");local_literal=true;}
        if(mode==7) {strcpy(original,"请记住，小星星。");native_reply="我已经记住了。";}
        if(mode==8) {strcpy(original,"请把灯设为蓝色，不对，改成绿色。");native_reply="已设为蓝色。";}
        if(mode==9 || mode==10)manual_overlap=mode-8; /* Pending or active at full commit. */
        if(mode==11) {strcpy(original,"请把灯设为蓝色，不对，改成绿色。");native_reply="我来把灯改成黑色。";}
        if(mode==12) {manual_preview="请记住";manual_changed=true;drain_failure=AGENT_ERR_NETWORK;}
        if(mode==13) {strcpy(original,"请记住，小星星。");native_reply="我来记一下。";}
        if(mode==14) {strcpy(original,"请把灯设为蓝色，不对，改成绿色。");native_reply="我来调整灯光。";}
        bool correction=mode==8 || mode==11 || mode==14;
        if(correction) {
            manual_preview="请把灯设为蓝色。";
            expected_rgb[0]=expected_rgb[2]=0;expected_rgb[1]=255;
        }
        agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice);
        assert(joined && !sample_count && !effects && commits==(mode==3?0u:1u) && !creates);
        assert(uploaded_samples==prefetch_chunks*ESP_AGENT_FAST_UPLOAD_SAMPLES && appends==prefetch_chunks && radio_restores==1);
        assert(manual_drafts==(mode==2?0u:1u));
        bool delegate=false;error=finish_join(error,&delegate);
        if(mode==3)assert(error==AGENT_ERR_TIMEOUT && !delegate && !sample_count && !input[0]);
        else if(mode==4)assert(error==AGENT_ERR_CANCELLED && !delegate && !sample_count && !effects);
        else if(mode==12)assert(error==AGENT_ERR_NETWORK && !delegate && !sample_count && !effects);
        else {
            if(error)fprintf(stderr,"manual prefetch case%u: %s\n",mode,agent_err_name(error));
            bool miss=mode==1 || mode==5 || mode==7;
            assert(!error && delegate==(miss || mode==13));
            assert(!strcmp(input,original) && !replay_releases && opens==1 && commits==1);
            assert(effects==(mode==6 || correction));
            if(correction)assert(local_literal && sample_count==18033 && !native_cached);
            if(miss)assert(!sample_count && !esp_agent_voice_fast_ack()->text[0]);
            else assert(sample_count>0);
            if(mode==13)assert(native_cached && cache_sealed && esp_agent_voice_fast_ack()->spoken);
            assert(creates==(mode==2?1u:0u)); /* Never regenerate consumed input. */
        }
        assert(!socket_open && !speaker);agent_end_turn(&core);
    }
    esp_agent_voice_prefetch_set(false);
}
static void prefetch_recovery_checks(void)
{
    for(unsigned mode=0;mode<4;++mode) {
        scenario=PLAIN;reset();esp_agent_voice_prefetch_set(true);prefetch_empty=true;
        if(mode==1)replay_error=AGENT_ERR_CORRUPT;
        if(mode==2)cancel_on_close=true;
        if(mode==3)original[0]=0;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        assert(joined && !effects && !sample_count && appends==prefetch_chunks);
        /* Recovery has its own manual commit after the closed failed one. */
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        agent_err_t expected=mode==1?AGENT_ERR_CORRUPT:mode==2?AGENT_ERR_CANCELLED:mode==3?AGENT_ERR_PROTOCOL:AGENT_OK;
        if(error!=expected)fprintf(stderr,"manual recovery %u: %s\n",mode,agent_err_name(error));
        assert(error==expected && !delegate && !effects && !socket_open && !speaker && !replaying);
        assert(replay_releases==(mode==2?0u:1u) && closes==opens && stream_finishes==stream_opens);
        assert(mode || (sample_count && !strcmp(input,original)));
        assert(!mode || !sample_count);agent_end_turn(&core);
    }
    esp_agent_voice_prefetch_set(false);
}
static void prefetch_stream_checks(void)
{
    for(unsigned mode=1;mode<=11;++mode) {
        scenario=PLAIN;reset();esp_agent_voice_prefetch_set(true);
        manual_stream=mode;manual_overlap=3;
        if(mode==8)read_limit=257; /* The same long tail has a bounded faster link. */
        strcpy(original,mode==2?"请记住，小星星。":"请介绍你自己。");
        native_reply=mode==2?"我来记一下。":"我是小言。";
        if(mode==11) {strcpy(original,"請記低我鍾意藍色。");native_reply="我嚟記低先。";}
        if(mode==5)plain_final="different final text";
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        assert(joined && !sample_count && !effects && commits==1);
        bool delegate=false;agent_err_t error=finish_join(AGENT_OK,&delegate);
        agent_err_t expected=mode>=3 && mode<=5?AGENT_ERR_PROTOCOL:
            mode==6?AGENT_ERR_CANCELLED:mode==7?AGENT_ERR_NETWORK:
            mode==9?AGENT_ERR_TIMEOUT:mode==10?AGENT_ERR_SERVER:AGENT_OK;
        if(error!=expected)fprintf(stderr,"stream draft%u: %s\n",mode,agent_err_name(error));
        assert(error==expected && delegate==(mode==2 || mode==11));
        assert(!speaker && !socket_open && opens==1 && closes==1 && !creates && !effects);
        assert(!replay_releases && manual_drafts==1 && commits==1 && sample_count>0);
        /* Packed prefix starts with three zeros, then 15*7/8 and +15*16/8.
         * Leading zeros are omitted once; the later raw tail is untouched. */
        if(mode!=2 && mode!=11)assert(manual_pcm[0]==13 && manual_pcm[1]==43);
        if(mode==1 || mode==5 || mode==8)assert(manual_pcm[2]==0x3412 && manual_pcm[3]==0x5678);
        if(mode==1)assert(sample_count==4);
        if(mode==2 || mode==11)assert(native_cached && cache_sealed && esp_agent_voice_fast_ack()->spoken && sample_count==7);
        if(mode==11)assert(!strcmp(esp_agent_voice_fast_ack()->language,"yue"));
        if(mode==8)assert(sample_count==84004); /* Longer than the entire prefetch cache. */
        if(error)assert(drained==error && !esp_agent_voice_fast_ack()->pending);
        agent_end_turn(&core);
    }
    esp_agent_voice_prefetch_set(false);
}
static void manual_build_checks(void)
{
#if !AGENT_RT_AUTOVAD
    scenario=PLAIN;reset();
    agent_speech_t speech={.scratch=engine.buffer,.capacity=AGENT_ENGINE_BUFFER_SIZE,.now_ms=tool_time};
    agent_realtime_t q;agent_realtime_init(&q,&speech,&esp_agent_realtime_ws,NULL,NULL,NULL);
    assert(agent_realtime_begin(&q,"test","[]",true)==AGENT_ERR_ARGUMENT && !opens);
    q.continuous=true;
    assert(agent_realtime_begin(&q,"test","[]",false)==AGENT_ERR_ARGUMENT && !opens);
    q.continuous=false;q.created=q.ready=true;q.deadline=1000;q.input_samples=16000;
    const char *text="{\"type\":\"input_audio_buffer.speech_started\",\"item_id\":\"unexpected\",\"audio_start_ms\":0}";
    agent_ws_chunk_t packet={strlen(text),1,true,true};
    assert(agent_realtime_receive(&q,text,&packet)==AGENT_ERR_PROTOCOL && !opens);
    agent_end_turn(&core);
#endif
}
static void prefetch_acquisition_checks(void)
{
    for(unsigned mode=0;mode<5;++mode) {
        scenario=PLAIN;reset();esp_agent_voice_prefetch_set(true);prefetch_protocol_fault=mode!=4;
        manual_bad_echo=mode==4;
        prefetch_capture_fault=mode==1;prefetch_cancel_capture=mode==2;no_endpoint=mode==3;
        agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
        agent_err_t expected=mode==1?AGENT_ERR_CORRUPT:mode==2?AGENT_ERR_CANCELLED:mode==3?AGENT_ERR_TIMEOUT:mode==4?AGENT_ERR_PROTOCOL:AGENT_OK;
        assert(error==expected && joined && !sample_count && !effects && !socket_open);
        assert(appends<prefetch_chunks && prefetch_emitted<=prefetch_chunks);
        if(!mode)assert(prefetch_emitted==prefetch_chunks);
        bool delegate=false;error=finish_join(error,&delegate);
        assert(error==expected && !delegate && !socket_open && !speaker && !effects);
        assert(replay_releases==!mode);
        assert(mode?!sample_count:(sample_count && !strcmp(input,original)));
        agent_end_turn(&core);
    }
    esp_agent_voice_prefetch_set(false);
}
static void next_prediction_turn(void)
{
    assert(!agent_begin_turn(&core));++reuse_round;
    joined=ready=false;source_reads=appends=commits=creates=manual_drafts=prefetch_emitted=0;
    sample_count=manual_observed=0;
    size_t size;void *memory=agent_engine_scratch(&engine,&size);memset(memory,0xa5,capture_capacity);
}
static void prediction_reuse_checks(void)
{
    for(unsigned fault=0;fault<9;++fault) {
        scenario=PLAIN;reset();esp_agent_voice_reuse_set(true);esp_agent_voice_prefetch_set(true);
        strcpy(original,"请介绍你自己。");native_reply="我是小言。";reuse_round=1;
        if(fault==1)reuse_no_clear=true;
        if(fault==2)drain_failure=AGENT_ERR_NETWORK;
        bool delegate=false;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice));
        agent_err_t error=finish_join(AGENT_OK,&delegate);
        assert(error==(fault==2?AGENT_ERR_NETWORK:AGENT_OK) && !delegate && sample_count==2);
        assert(!speaker && !effects && opens==1);
        agent_end_turn(&core);
        if(fault==1 || fault==2)assert(!socket_open && closes==1 && !esp_agent_voice_fast_ready());
        else if(fault==3) {
            enqueue("{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"complete1\",\"text\":\"old\",\"stash\":\"\"}");
            assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_PROTOCOL && closes==1);
        } else if(fault==4) {
            ticks+=60000;assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_TIMEOUT && closes==1);
        } else {
            for(unsigned turn=2;turn<=(fault?2u:3u);++turn) {
                assert(socket_open && esp_agent_voice_fast_ready() && !esp_agent_voice_fast_poll_idle());
                next_prediction_turn();
                if(fault==5) {strcpy(original,"请记住，小星星。");native_reply="我来记一下。";}
                if(fault==6) {
                    strcpy(original,"请把灯设为蓝色，不对，改成绿色。");native_reply="已设为蓝色。";
                    manual_preview="请把灯设为蓝色。";
                    expected_rgb[0]=expected_rgb[2]=0;expected_rgb[1]=255;
                }
                if(fault==7)manual_no_preview=true;
                if(fault==8)cancel_next=true;
                error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),literal_notice);
                error=finish_join(error,&delegate);
                assert(error==(fault==8?AGENT_ERR_CANCELLED:AGENT_OK));
                assert(delegate==(fault==5) && effects==(fault==6) && !speaker && opens==1);
                if(fault==6)assert(local_literal && sample_count==18033);
                if(fault==8)assert(!sample_count);
                else assert(sample_count>0);
                assert(socket_open==(!fault && turn<3));
                assert(esp_agent_voice_fast_ready()==socket_open);
                if(!fault)assert(!strcmp(input,original) && updates==1 && !creates && manual_drafts==1);
                if(fault==7)assert(updates==2 && creates==1); /* Normal config never retained as prediction. */
                agent_end_turn(&core);
            }
        }
        assert(!socket_open && closes==1 && effects==(fault==6));
    }
    esp_agent_voice_prefetch_set(false);esp_agent_voice_reuse_set(false);
}
static void reuse_checks(void)
{
    scenario=PLAIN;reset();esp_agent_voice_reuse_set(true);
    for(reuse_round=1;reuse_round<=3;++reuse_round) {
        if(reuse_round>1) {
            assert(!agent_begin_turn(&core));
            joined=ready=false;source_reads=appends=commits=creates=0;
            size_t size;void *memory=agent_engine_scratch(&engine,&size);
            memset(memory,0xa5,capture_capacity);
        }
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate) && !delegate);
        assert(opens==1 && updates==1 && !speaker && !effects && sample_count==2*reuse_round);
        assert(socket_open==(reuse_round<3) && esp_agent_voice_fast_ready()==socket_open);
        assert(!strcmp(input,original));
        if(socket_open)assert(!esp_agent_voice_fast_poll_idle());
        agent_end_turn(&core);
    }
    assert(closes==1 && !esp_agent_voice_fast_ready());
    for(unsigned cancelled=0;cancelled<2;++cancelled) {
        scenario=PLAIN;reset();esp_agent_voice_reuse_set(true);reuse_round=1;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate) && !delegate);
        assert(socket_open && esp_agent_voice_fast_ready());agent_end_turn(&core);
        assert(!agent_begin_turn(&core));reuse_round=2;scenario=DELEGATE;task_alias=true;
        strcpy(original,"请记住，小星星。");joined=ready=false;
        source_reads=appends=commits=creates=0;
        size_t size;void *memory=agent_engine_scratch(&engine,&size);memset(memory,0xa5,capture_capacity);
        cancel_next=cancelled!=0;
        agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
        error=finish_join(error,&delegate);
        assert(error==(cancelled?AGENT_ERR_CANCELLED:AGENT_OK) && delegate==!cancelled);
        assert(!socket_open && !speaker && opens==1 && closes==1 && !effects && sample_count==2);
        if(!cancelled)assert(!strcmp(input,original));
        agent_end_turn(&core);
    }
    for(unsigned fault=0;fault<3;++fault) {
        scenario=PLAIN;reset();esp_agent_voice_reuse_set(true);reuse_round=1;reuse_no_clear=fault==0;
        assert(!esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL));
        bool delegate=false;assert(!finish_join(AGENT_OK,&delegate) && !delegate && sample_count==2);
        if(fault==0)assert(!socket_open && !esp_agent_voice_fast_ready() && closes==1);
        else if(fault==1) {
            ticks+=60000;assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_TIMEOUT && closes==1);
        } else {
            enqueue("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"reuse_i1\",\"transcript\":\"stale\"}");
            assert(esp_agent_voice_fast_poll_idle()==AGENT_ERR_PROTOCOL && closes==1);
        }
        assert(sample_count==2 && !effects && !socket_open);agent_end_turn(&core);
    }
    esp_agent_voice_reuse_set(false);
}
static void capture_fault_notice(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    unsigned *count=!strcmp(stage,"capture_input_error")?&capture_input_errors:
        !strcmp(stage,"asr_input_error")?&asr_input_errors:
        !strcmp(stage,"capture_join_error")?&capture_join_errors:NULL;
    if(count) {assert(text && !strcmp(text,"limit"));++*count;}
}
static void capture_fault_checks(void)
{
    /* The same public error must identify its owner without changing teardown
     * or admitting incomplete input, PCM output, tools or context commits. */
    for(unsigned owner=0;owner<3;++owner) {
        scenario=PLAIN;reset();
        if(owner==0)source_fault=AGENT_ERR_LIMIT;
        if(owner==1)network_input_fault=AGENT_ERR_LIMIT;
        if(owner==2)join_fault=AGENT_ERR_LIMIT;
        agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),capture_fault_notice);
        assert(error==AGENT_ERR_LIMIT && joined && !radio_active && radio_restores==1);
        assert(capture_input_errors==(owner==0) && asr_input_errors==(owner==1) && capture_join_errors==(owner==2));
        bool delegated=false;
        assert(finish_join(error,&delegated)==AGENT_ERR_LIMIT && !delegated);
        assert(!effects && !sample_count && !commits && !stream_opens && !socket_open && !speaker);
        assert(opens==1 && closes==1);agent_end_turn(&core);
    }
}
int main(int argc,char **argv)
{
    if(argc==2) {
        scenario=PLAIN;reset();wire_fixture=argv[1];read_limit=512;
        strcpy(original,"我刚给这盏灯取的名字是什么？");
        agent_err_t result=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
        bool delegate=false;result=finish_join(result,&delegate);
        assert(!speaker && !socket_open && !effects);
        printf("wire: error=%s delegate=%u audible_samples=%zu ack=%s\n",
            agent_err_name(result),delegate,sample_count,esp_agent_voice_fast_ack()->text);
        agent_end_turn(&core);return result?1:0;
    }
    const agent_err_t expected[]={AGENT_OK,AGENT_OK,AGENT_OK,AGENT_ERR_ARGUMENT,AGENT_ERR_NETWORK,AGENT_ERR_CANCELLED,AGENT_ERR_PROTOCOL,AGENT_OK,AGENT_OK,AGENT_ERR_SERVER,AGENT_ERR_PROTOCOL,AGENT_OK,AGENT_ERR_PROTOCOL,AGENT_ERR_PROTOCOL};
    for(scenario=0;scenario<CASES;++scenario) {
        reset();agent_err_t error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
        assert(error==(scenario==EARLY_CAPTURE?AGENT_ERR_NETWORK:AGENT_OK));assert(joined);
        assert(!radio_active && radio_begins==1 && radio_restores==1);
        bool delegate=false;error=finish_join(error,&delegate);
        if(error!=expected[scenario])fprintf(stderr,"fast case %u expected %d got %d\n",scenario,expected[scenario],error);
        assert(error==expected[scenario] && delegate==(scenario==DELEGATE));
        assert(!socket_open && !speaker && opens==1 && closes==1 && source_reads>=1);
        assert(effects==(scenario==BLUE?1u:0u) && results==(scenario==UNCLEAR_COLOR?1u:0u));
        assert(stream_opens==(scenario==EARLY_CAPTURE?0u:1u) && stream_finishes==stream_opens);
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect_event,NULL));
        if(scenario==DELEGATE)assert(!users && !turns && !errors && !strcmp(input,original) && drained==AGENT_ERR_CANCELLED);
        else if(scenario==EARLY_CAPTURE)assert(!users && !turns && !errors);
        else if(error)assert(users==1 && !turns && errors==1);
        else assert(users==1 && turns==1 && !errors && (scenario==BLUE?sample_count==18033:sample_count==2));
        assert(tool_messages==((scenario==BLUE || scenario==UNCLEAR_COLOR)?1u:0u));
        if(scenario==MIXED_AUDIO_TOOL)assert(sample_count==2 && creates==1 && !effects && !results && drained==AGENT_ERR_PROTOCOL);
        agent_end_turn(&core);
    }
    draft_checks();warm_checks();reuse_checks();prediction_reuse_checks();
    capture_fault_checks();
    scenario=PLAIN;reset();radio_begin_error=AGENT_ERR_CONFIG;
    agent_err_t capture_error=esp_agent_voice_fast_capture(&engine,input,sizeof(input),NULL);
    assert(capture_error==AGENT_ERR_CONFIG && joined && !radio_active && !opens && !appends);
    bool delegated=false;
    assert(finish_join(capture_error,&delegated)==AGENT_ERR_CONFIG && !delegated);
    assert(!commits && !stream_opens && radio_restores==1);agent_end_turn(&core);
    diagnostic_checks();
    provider_failure_checks();
    policy_checks();
    asr_activity_checks();
    acknowledgement_checks();
    native_ack_checks();
    receipt_metadata_checks();
    detached_ack_checks();
    plain_ack_checks();
    startup_deadline_checks();
    history_checks();
    local_confirmation_checks();
    literal_light_checks();
    joined_light_checks();
    unfinished_checks();
    sentence_stream_checks();
    progress_checks();empty_route_checks();
    early_commit_checks();prefetch_checks();prefetch_stream_checks();manual_build_checks();
    prefetch_recovery_checks();
    prefetch_acquisition_checks();
    for(;head<tail;++head)free(queue[head]);
    puts("production voice_fast: 14 turn scenarios, warm ownership, diagnostics, no replay after speech/effects and one-shot progress ownership OK");return 0;
}
