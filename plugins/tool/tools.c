#include "tools.h"
#include "json.h"
#include "context.h"
#include <stdio.h>
#include <string.h>

enum {
    TOOL_STATUS, TOOL_LIGHT_GET, TOOL_LIGHT_SET, TOOL_CONTEXT,
#if AGENT_ENABLE_AUDIO
    TOOL_AUDIO, TOOL_SCORE, TOOL_STOP, TOOL_VOLUME, TOOL_MIC, TOOL_CAPTURE, TOOL_REPLAY, TOOL_SONG,
#endif
    TOOL_SEARCH, TOOL_SUMMARY_GET, TOOL_SUMMARY_SET,
    TOOL_CAPABILITIES, TOOL_PLAN_VALIDATE, TOOL_PLAN_RUN, TOOL_PLAN_STATUS, TOOL_PLAN_CANCEL,
    TOOL_GPIO_GET, TOOL_GPIO_SET, TOOL_HARDWARE, TOOL_DISPLAY_GET, TOOL_DISPLAY_SET
};

const agent_tool_description_t agent_tool_descriptions[AGENT_TOOL_COUNT] = {
    {"device.status.get", "device_status_get"},
    {"device.light.get", "device_light_get"},
    {"device.light.set_rgb", "device_light_set_rgb"},
    {"agent.context.stats", "agent_context_stats"},
#if AGENT_ENABLE_AUDIO
    {"device.audio.get", "device_audio_get"},
    {"device.audio.play_score", "device_audio_play_score"},
    {"device.audio.stop", "device_audio_stop"},
    {"device.audio.volume", "device_audio_volume"},
    {"device.mic.set", "device_mic_set"},
    {"device.audio.capture", "device_audio_capture"},
    {"device.audio.replay", "device_audio_replay"},
    {"device.audio.play_song", "device_audio_play_song"},
#endif
    {"agent.context.search", "agent_context_search"},
    {"agent.context.summary.get", "agent_context_summary_get"},
    {"agent.context.summary.set", "agent_context_summary_set"},
    {"device.control.capabilities", "device_control_capabilities"},
    {"device.control.validate", "device_control_validate"},
    {"device.control.run", "device_control_run"},
    {"device.control.status", "device_control_status"},
    {"device.control.cancel", "device_control_cancel"},
    {"device.gpio.get", "device_gpio_get"},
    {"device.gpio.set", "device_gpio_set"},
    {"device.hardware.get", "device_hardware_get"},
    {"device.display.get", "device_display_get"},
    {"device.display.set", "device_display_set"},
};

#define EMPTY_SCHEMA "\"parameters\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
#define PLAN_SCHEMA "\"parameters\":{\"type\":\"object\",\"properties\":{" \
    "\"steps\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":32,\"items\":{\"type\":\"object\",\"properties\":{" \
    "\"op\":{\"type\":\"string\",\"enum\":[\"light\",\"wait\",\"play_score\",\"mic\",\"capture\",\"replay\",\"await\",\"wait_level\",\"gpio_read\",\"gpio_write\",\"pwm\",\"wait_gpio\"]}," \
    "\"r\":{\"type\":\"integer\"},\"g\":{\"type\":\"integer\"},\"b\":{\"type\":\"integer\"}," \
    "\"ms\":{\"type\":\"integer\"},\"score\":{\"type\":\"integer\"},\"wait\":{\"type\":\"boolean\"}," \
    "\"enabled\":{\"type\":\"boolean\"},\"resource\":{\"enum\":[\"speaker\",\"capture\"]},\"above\":{\"type\":\"integer\"}," \
    "\"pin\":{\"type\":\"integer\"},\"value\":{\"type\":\"integer\"},\"hz\":{\"type\":\"integer\"},\"duty\":{\"type\":\"integer\"}," \
    "\"timeout_ms\":{\"type\":\"integer\"},\"on_timeout\":{\"enum\":[\"abort\",\"continue\"]}},\"required\":[\"op\"],\"additionalProperties\":false}}," \
    "\"scores\":{\"type\":\"array\",\"maxItems\":2,\"items\":{\"type\":\"object\",\"properties\":{" \
    "\"bpm\":{\"type\":\"integer\",\"minimum\":40,\"maximum\":240},\"wave\":{\"enum\":[\"sine\",\"triangle\"]}," \
    "\"notes\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":64,\"items\":{\"type\":\"array\",\"minItems\":2,\"maxItems\":2,\"items\":{\"type\":\"integer\"}}}}," \
    "\"required\":[\"bpm\",\"wave\",\"notes\"],\"additionalProperties\":false}}," \
    "\"repeat\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000}}," \
    "\"required\":[\"steps\"],\"additionalProperties\":false}"
#define PLAN_DESCRIPTION "Bounded local plan. Steps use only these fields: light(r,g,b=0..255); wait(ms); play_score(score=0..1,wait=true); mic(enabled); capture(ms=100..10000); replay(wait=true); await(resource,timeout_ms,on_timeout); wait_level(above=1..32767,timeout_ms,on_timeout); gpio_read(pin); gpio_write(pin,value=0|1); pwm(pin,hz=10..5000,duty=0..1000); wait_gpio(pin,value,timeout_ms,on_timeout). Omit fields irrelevant to the op. GPIO requires capabilities. Scores use [MIDI pitch,ticks] notes, pitch 0 or 36..96, ticks 1..16. wait=false permits following light/mic steps in parallel; join speaker with await before another sound or plan end. Sound triggers need a preceding mic enabled step and fresh levels. Capture reserves a silent speaker and includes up to 10s preparation. Max 32 steps, repeat 1..8, total timeout 60s; all owned outputs/mic are restored at completion/cancel."
static const char *const schemas[AGENT_TOOL_COUNT] = {
    "{\"type\":\"function\",\"function\":{\"name\":\"device_status_get\","
    "\"description\":\"Get firmware, network and memory status.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_light_get\","
    "\"description\":\"Get the RGB light color.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_light_set_rgb\","
    "\"description\":\"Set all four head LEDs to an RGB color; 0,0,0 turns them off.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"r\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"g\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"b\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255}},"
    "\"required\":[\"r\",\"g\",\"b\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"agent_context_stats\","
    "\"description\":\"Get context storage and synchronization statistics.\"," EMPTY_SCHEMA "}}",
#if AGENT_ENABLE_AUDIO
    "{\"type\":\"function\",\"function\":{\"name\":\"device_audio_get\","
    "\"description\":\"Read speaker playback progress and local microphone levels; levels are not speech recognition.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_audio_play_score\","
    "\"description\":\"Compose and queue a short original melody on the speaker. Each notes pair is [MIDI pitch, sixteenth-note ticks]; 4 ticks is a quarter note. Pitch 0 is a rest, other pitches are 36..96. At most 64 notes and 30 seconds. Returns immediately; inspect audio status for completion. One score at a time.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"bpm\":{\"type\":\"integer\",\"minimum\":40,\"maximum\":240},"
    "\"wave\":{\"type\":\"string\",\"enum\":[\"sine\",\"triangle\"]},"
    "\"notes\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":64,\"items\":{\"type\":\"array\",\"minItems\":2,\"maxItems\":2,"
    "\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":96},\"description\":\"[pitch,ticks]; ticks must be 1..16.\"}}},"
    "\"required\":[\"bpm\",\"wave\",\"notes\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_audio_stop\","
    "\"description\":\"Stop current playback or capture; cancels an action plan owning the speaker.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_audio_volume\","
    "\"description\":\"Set speaker master volume, 0 mutes; default is 80.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"percent\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100}},"
    "\"required\":[\"percent\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_mic_set\","
    "\"description\":\"Enable or disable local microphone level measurement when the user requests it. Audio is not uploaded, recorded or transcribed.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"enabled\":{\"type\":\"boolean\"}},"
    "\"required\":[\"enabled\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_audio_capture\","
    "\"description\":\"When requested, record a local mono clip, replacing the previous clip; no upload or transcription. Returns immediately; poll audio status until recording=false and clip_ready=true before replay. Speaker must be idle. Preparation may take up to 10 seconds.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"ms\":{\"type\":\"integer\",\"minimum\":100,\"maximum\":10000}},\"required\":[\"ms\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_audio_replay\","
    "\"description\":\"Replay the verified local microphone clip asynchronously at the current volume.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_audio_play_song\","
    "\"description\":\"Compose expressive instrumental music, up to 75s, on four simultaneous voices: electric keys lead, bass, plucked arpeggio, drums. v=1. Each pattern is two 4/4 bars (32 sixteenth ticks), with trailing silence. Notes are [MIDI pitch,ticks,velocity,gate_percent]; pitch 0 or 36..96, ticks 1..32, velocity 1..127, gate 1..100. Sum ticks <=32 per pattern. Max 16 patterns and 128 notes combined. sequence rows are [lead_pattern,bass_pattern,arp_pattern,drum_pattern,gain_1_to_127]; -1 mutes a part, other references are zero-based. Drums use ONLY pitches 36=kick,38=snare,42=hat,0=rest. Max 32 sequence rows, total <=75s. At 128 BPM, 16 sequence rows give exactly 32 bars/60s. Reuse patterns, vary arrangement and dynamics, end on the tonic. Keep JSON compact, under 4096 bytes. Returns immediately; audio playing=false means finished. Use this tool for full music, not a timed control plan.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"v\":{\"type\":\"integer\",\"enum\":[1]},"
    "\"bpm\":{\"type\":\"integer\",\"minimum\":60,\"maximum\":180},"
    "\"patterns\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":16,\"items\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":32,"
    "\"items\":{\"type\":\"array\",\"minItems\":4,\"maxItems\":4,\"items\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":127}}}},"
    "\"sequence\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":32,\"items\":{\"type\":\"array\",\"minItems\":5,\"maxItems\":5,"
    "\"items\":{\"type\":\"integer\",\"minimum\":-1,\"maximum\":127}}}},\"required\":[\"v\",\"bpm\",\"patterns\",\"sequence\"],\"additionalProperties\":false}}}",
#endif
    "{\"type\":\"function\",\"function\":{\"name\":\"agent_context_search\","
    "\"description\":\"Find retained earlier dialogue by a UTF-8 phrase (ASCII case-insensitive), or fetch an exact event_id. Newest matches first with source IDs. Use next_before for older matches.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":128},"
    "\"before\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":9007199254740991},\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":3}},"
    "\"required\":[\"query\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"agent_context_summary_get\","
    "\"description\":\"Read the durable historical summary and its source boundary. A summary can omit details; search source events to verify.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"agent_context_summary_set\","
    "\"description\":\"Save durable user facts/decisions/work; preserve other user facts, omit hardware status/capabilities. Do not rewrite unchanged facts. Aim <256 characters; max1536 UTF-8 bytes. No invented facts. through_seq: omit/0 for latest completed turn.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":1536},"
    "\"through_seq\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":9007199254740991}},\"required\":[\"text\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_control_capabilities\","
    "\"description\":\"Discover GPIO modes and resource owners before a plan. Owner 0=free,1=system,2=direct tool,3=plan,4=storage,5=LCD. Never infer spare GPIO.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_control_validate\",\"description\":\"Check a plan without effects. " PLAN_DESCRIPTION "\"," PLAN_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_control_run\",\"description\":\"Validate and start a plan asynchronously. Returns accepted job, not completion; poll control status. " PLAN_DESCRIPTION "\"," PLAN_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_control_status\",\"description\":\"Read job state, step, completed repeats and error. Waits locally up to 15000ms by default for an active job; use wait_ms=0 for an immediate check. active=true still means unfinished, including cleanup. Do not rapidly repeat status calls.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"wait_ms\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":15000}},\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_control_cancel\",\"description\":\"Request cancellation and restoration of the active local action plan. Check status for completion.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_gpio_get\",\"description\":\"Read an exposed pin level, mode and PWM configuration. Query capabilities first. Busy during a control plan.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":21}},\"required\":[\"pin\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_gpio_set\",\"description\":\"Set an exposed GPIO. Query capabilities first; LCD/body pins may be connected. input releases output/PWM; output requires value; pwm requires hz and duty, at most two simultaneous channels. Omit irrelevant fields. Change persists until changed or reboot; use control.run for timed sequences with restoration. Busy during any plan.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"pin\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":21},\"mode\":{\"type\":\"string\",\"enum\":[\"input\",\"output\",\"pwm\"]},"
    "\"value\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1},\"hz\":{\"type\":\"integer\",\"minimum\":10,\"maximum\":5000},"
    "\"duty\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1000}},\"required\":[\"pin\",\"mode\"],\"additionalProperties\":false}}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_hardware_get\",\"description\":\"Read this firmware's board wiring, managed peripherals, reserved IO and hardware uncertainties.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_display_get\",\"description\":\"Read LCD mode, NTP validity, last rendered time, frame count and driver errors. Successful writes do not prove visible pixels.\"," EMPTY_SCHEMA "}}",
    "{\"type\":\"function\",\"function\":{\"name\":\"device_display_set\","
    "\"description\":\"Control the onboard 160x80 LCD. clock starts persistent local once-per-second refresh until off/cancel/reboot, without repeated LLM calls. Default Beijing UTC+480 minutes, white on black. Use NTP time, never invent it. fill paints foreground; off stops and releases GPIO4/5/10 as inputs. Other GPIO access to them is busy while LCD owns them. Asynchronous: check pending=false,ready=true,rendered_job=job,frames increasing. RGB565 colors: white65535,black0,red63488,green2016,blue31. Omit all other fields for off, and offset/background for fill.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"mode\":{\"type\":\"string\",\"enum\":[\"clock\",\"fill\",\"off\"]},"
    "\"utc_offset_minutes\":{\"type\":\"integer\",\"minimum\":-720,\"maximum\":840},"
    "\"foreground\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535},"
    "\"background\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535}},\"required\":[\"mode\"],\"additionalProperties\":false}}}"
};

static agent_err_t write_tools(bool voice,agent_write_fn write,void *write_ctx)
{
    if(!write) return AGENT_ERR_ARGUMENT;
    agent_err_t error=write(write_ctx,"[",1);
    for(unsigned i=0;!error && i<AGENT_TOOL_COUNT;++i) {
        if(i) error=write(write_ctx,",",1);
        if(!error && voice) {
            /* These immutable schemas all start parameters with properties.
             * Insert one optional control field without a DOM/heap allocation. */
            static const char key[]="\"properties\":{";
            const char *at=strstr(schemas[i],key);
            if(!at)return AGENT_ERR_CONFIG;
            at+=sizeof(key)-1;
            error=write(write_ctx,schemas[i],(size_t)(at-schemas[i]));
            static const char hint[]="\"final_batch\":{\"type\":\"boolean\"}";
            if(!error)error=write(write_ctx,hint,sizeof(hint)-1);
            if(!error && *at!='}')error=write(write_ctx,",",1);
            if(!error)error=write(write_ctx,at,strlen(at));
        } else if(!error) error=write(write_ctx,schemas[i],strlen(schemas[i]));
    }
    return error?error:write(write_ctx,"]",1);
}
agent_err_t agent_tools_write(void *ctx,agent_write_fn write,void *write_ctx)
{ (void)ctx;return write_tools(false,write,write_ctx); }
agent_err_t agent_tools_voice_write(void *ctx,agent_write_fn write,void *write_ctx)
{ (void)ctx;return write_tools(true,write,write_ctx); }

agent_err_t agent_tools_voice_hint(agent_llm_reply_t *reply,char *scratch,size_t capacity,bool *last)
{
    if(!reply || !scratch || !last || !reply->done || !reply->call_count ||
       reply->call_count>AGENT_TOOLS_MAX || capacity<sizeof(reply->arguments)+5)return AGENT_ERR_ARGUMENT;
    if(reply->args_used>sizeof(reply->arguments))return AGENT_ERR_PROTOCOL;
    *last=false;bool hinted=false;size_t used=0;
    unsigned offsets[AGENT_TOOLS_MAX];
    for(unsigned i=0;i<reply->call_count;++i) {
        if(reply->calls[i].offset>=reply->args_used)return AGENT_ERR_PROTOCOL;
        const char *original=agent_call_arguments(reply,i);
        if(!memchr(original,0,reply->args_used-reply->calls[i].offset))return AGENT_ERR_PROTOCOL;
        cJSON *root=agent_json_parse(original,strlen(original));
        if(!root)return AGENT_ERR_JSON;
        if(!cJSON_IsObject(root)) {cJSON_Delete(root);return AGENT_ERR_ARGUMENT;}
        const cJSON *value=cJSON_GetObjectItemCaseSensitive(root,"final_batch");
        if(value && (!cJSON_IsBool(value) || (cJSON_IsTrue(value) && i+1!=reply->call_count))) {
            cJSON_Delete(root);return AGENT_ERR_ARGUMENT;
        }
        if(value) {
            hinted=cJSON_IsTrue(value);
            cJSON_DeleteItemFromObjectCaseSensitive(root,"final_batch");
            if(!cJSON_PrintPreallocated(root,scratch+used,(int)(capacity-used),false)) {
                cJSON_Delete(root);return AGENT_ERR_LIMIT;
            }
        } else {
            size_t n=strlen(original)+1;
            if(n>capacity-used) {cJSON_Delete(root);return AGENT_ERR_LIMIT;}
            memcpy(scratch+used,original,n);
        }
        cJSON_Delete(root);
        size_t bytes=strlen(scratch+used)+1;
        if(bytes>sizeof(reply->arguments)-used)return AGENT_ERR_LIMIT;
        offsets[i]=(unsigned)used;used+=bytes;
    }
    /* No mutation of the original call arguments until the whole batch is valid. */
    memcpy(reply->arguments,scratch,used);reply->args_used=(unsigned)used;
    for(unsigned i=0;i<reply->call_count;++i) {
        reply->calls[i].offset=offsets[i];
        reply->calls[i].length=(i+1<reply->call_count?offsets[i+1]:used)-offsets[i]-1;
    }
    *last=hinted;return AGENT_OK;
}

static int find_tool(const char *name)
{
    if (!name) return -1;
    for (unsigned i = 0; i < AGENT_TOOL_COUNT; ++i)
        if (!strcmp(name, agent_tool_descriptions[i].name) || !strcmp(name, agent_tool_descriptions[i].wire_name)) return (int)i;
    return -1;
}

const agent_tool_description_t *agent_tool_find(const char *name)
{
    int index=find_tool(name); return index<0 ? NULL : &agent_tool_descriptions[index];
}

typedef union {
    uint8_t rgb[3];
    unsigned wait_ms;
    agent_display_config_t display;
    struct { unsigned pin; agent_pin_state_t state; } gpio;
    struct { char query[129]; uint64_t before; unsigned limit; } search;
    struct { char text[AGENT_SUMMARY_MAX+1]; uint64_t through; } summary;
    agent_plan_t plan;
#if AGENT_ENABLE_AUDIO
    unsigned value;
    agent_score_t score;
    agent_song_t song;
#endif
} tool_arguments_t;

static agent_err_t validate(int index, const char *arguments, tool_arguments_t *args,char *detail,size_t capacity)
{
    if (index < 0) return AGENT_ERR_TOOL;
    if (!arguments || strlen(arguments) > AGENT_ARGS_MAX) return AGENT_ERR_LIMIT;
    if(index==TOOL_PLAN_VALIDATE || index==TOOL_PLAN_RUN) return agent_plan_parse(arguments,&args->plan);
    if(index==TOOL_DISPLAY_SET) return agent_display_parse(arguments,&args->display);
#if AGENT_ENABLE_AUDIO
    if (index == TOOL_SCORE) return agent_score_parse(arguments, &args->score);
    if (index == TOOL_SONG) return agent_song_parse(arguments, &args->song);
#endif
    cJSON *root = agent_json_parse(arguments, strlen(arguments));
    if (!root) return AGENT_ERR_JSON;
    agent_err_t error = AGENT_ERR_ARGUMENT;
    if (!cJSON_IsObject(root)) goto done;
    unsigned count = 0;
    for (const cJSON *item = root->child; item; item = item->next) ++count;
    if(index==TOOL_PLAN_STATUS) {
        const cJSON *wait=cJSON_GetObjectItemCaseSensitive(root,"wait_ms");
        uint64_t value=15000;
        if(count!=(unsigned)(wait!=NULL) || (wait && !agent_json_uint(wait,15000,&value))) goto done;
        args->wait_ms=(unsigned)value;error=AGENT_OK;goto done;
    }
    if(index==TOOL_GPIO_GET || index==TOOL_GPIO_SET) {
        uint64_t pin,value,hz;
        if(!agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"pin"),21,&pin)) goto done;
        args->gpio.pin=(unsigned)pin; args->gpio.state=(agent_pin_state_t){.mode=AGENT_GPIO_READ};
        if(index==TOOL_GPIO_GET) { if(count==1) error=AGENT_OK; goto done; }
        const char *mode=agent_json_string(root,"mode"); if(!mode) goto done;
        if(!strcmp(mode,"input") && count==2) error=AGENT_OK;
        else if(!strcmp(mode,"output") && count==3 && agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"value"),1,&value)) {
            args->gpio.state=(agent_pin_state_t){.mode=AGENT_GPIO_WRITE,.value=(unsigned)value}; error=AGENT_OK;
        } else if(!strcmp(mode,"pwm") && count==4 && agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"hz"),5000,&hz) && hz>=10 &&
            agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"duty"),1000,&value)) {
            args->gpio.state=(agent_pin_state_t){.mode=AGENT_GPIO_PWM,.hz=(unsigned)hz,.duty=(unsigned)value}; error=AGENT_OK;
        }
        goto done;
    }
#if AGENT_ENABLE_AUDIO
    if(index==TOOL_CAPTURE) {
        uint64_t value;
        if(count==1 && agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"ms"),AGENT_CAPTURE_MS,&value) && value>=100) {
            args->value=(unsigned)value; error=AGENT_OK;
        }
        goto done;
    }
#endif
    if(index==TOOL_SEARCH) {
        const char *query=agent_json_string(root,"query");
        const cJSON *before=cJSON_GetObjectItemCaseSensitive(root,"before"),*limit=cJSON_GetObjectItemCaseSensitive(root,"limit");
        uint64_t n=3;
        if(!query || !*query || strlen(query)>128 || count!=1u+(before!=NULL)+(limit!=NULL)) goto done;
        args->search.before=0;
        if(before && !agent_json_uint(before,AGENT_SEQ_MAX,&args->search.before)) goto done;
        if(limit && (!agent_json_uint(limit,3,&n) || !n)) goto done;
        strcpy(args->search.query,query); args->search.limit=(unsigned)n; error=AGENT_OK; goto done;
    }
    if(index==TOOL_SUMMARY_SET) {
        const char *text=agent_json_string(root,"text");
        const cJSON *through=cJSON_GetObjectItemCaseSensitive(root,"through_seq");
        if(detail && capacity)snprintf(detail,capacity,"Use nonempty text (<=1536 UTF-8 bytes); only optional through_seq, an integer 0..9007199254740991. Omit it or use 0 for latest completed turn.");
        if(!text || !*text || count!=1u+(through!=NULL)) goto done;
        if(strlen(text)>AGENT_SUMMARY_MAX) {
            if(detail && capacity)snprintf(detail,capacity,"text is %u UTF-8 bytes; limit %u. Shorten durable user facts; omit hardware status/capabilities.",(unsigned)strlen(text),AGENT_SUMMARY_MAX);
            goto done;
        }
        args->summary.through=0;
        if(through && !agent_json_uint(through,AGENT_SEQ_MAX,&args->summary.through)) goto done;
        strcpy(args->summary.text,text); error=AGENT_OK; goto done;
    }
#if AGENT_ENABLE_AUDIO
    if (index == TOOL_VOLUME || index == TOOL_MIC) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, index == TOOL_VOLUME ? "percent" : "enabled");
        uint64_t percent;
        if (count != 1) goto done;
        if (index == TOOL_VOLUME && agent_json_uint(value, 100, &percent)) { args->value = (unsigned)percent; error = AGENT_OK; }
        if (index == TOOL_MIC && cJSON_IsBool(value)) { args->value = cJSON_IsTrue(value); error = AGENT_OK; }
        goto done;
    }
#endif
    if (index != TOOL_LIGHT_SET) { if (!count) error = AGENT_OK; goto done; }
    if (count != 3) goto done;
    const char *keys[] = {"r", "g", "b"};
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t value;
        if (!agent_json_uint(cJSON_GetObjectItemCaseSensitive(root, keys[i]), 255, &value)) goto done;
        args->rgb[i] = (uint8_t)value;
    }
    error = AGENT_OK;
done:
    cJSON_Delete(root);
    return error;
}

agent_err_t agent_tools_validate(const agent_llm_reply_t *reply)
{
    if (!reply->done || reply->call_count > AGENT_TOOLS_MAX) return AGENT_ERR_PROTOCOL;
    for (unsigned i = 0; i < reply->call_count; ++i) {
        if (!reply->calls[i].id[0]) return AGENT_ERR_PROTOCOL;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(reply->calls[i].id, reply->calls[j].id)) return AGENT_ERR_DUPLICATE;
    }
    for (unsigned i = 0; i < reply->call_count; ++i) {
        tool_arguments_t args;
        agent_err_t error = validate(find_tool(reply->calls[i].name), agent_call_arguments(reply, i), &args,NULL,0);
        if (error) return error;
    }
    return AGENT_OK;
}

agent_err_t agent_tool_check(const char *name,const char *arguments,char *detail,size_t capacity)
{
    int index=find_tool(name);tool_arguments_t args;
    if(detail && capacity) snprintf(detail,capacity,"Arguments must follow this tool's schema, ranges and resource limits.");
#if AGENT_ENABLE_AUDIO
    if(index==TOOL_SONG) return agent_song_parse_detail(arguments,&args.song,detail,capacity);
#endif
    return validate(index,arguments,&args,detail,capacity);
}

agent_err_t agent_tool_invoke(const agent_tool_ops_t *ops, const char *name, const char *arguments,
                             char *output, size_t capacity)
{
    if(!ops || !output || !capacity) return AGENT_ERR_ARGUMENT;
    int index = find_tool(name);
    tool_arguments_t args = {0};
    agent_err_t error = validate(index, arguments, &args,NULL,0);
    if (error) return error;
    uint64_t start = ops->now_ms ? ops->now_ms(ops->ctx) : 0;
    switch (index) {
    case TOOL_HARDWARE: error=ops->hardware?ops->hardware(ops->ctx,output,capacity):AGENT_ERR_CONFIG;break;
    case TOOL_DISPLAY_GET: case TOOL_DISPLAY_SET:
        if(!ops->display || !ops->display->status) return AGENT_ERR_CONFIG;
        if(index==TOOL_DISPLAY_SET) error=ops->display->set?ops->display->set(ops->display->ctx,&args.display):AGENT_ERR_CONFIG;
        if(!error) error=ops->display->status(ops->display->ctx,output,capacity);
        break;
    case TOOL_GPIO_GET: case TOOL_GPIO_SET: {
        if(!ops->control) return AGENT_ERR_CONFIG;
        agent_pin_state_t pin;
        error=agent_control_gpio(ops->control,args.gpio.pin,index==TOOL_GPIO_SET?&args.gpio.state:NULL,&pin);
        if(!error) {
            agent_json_writer_t w; agent_json_writer_init(&w,output,capacity);
            agent_json_printf(&w,"{\"pin\":%u,\"mode\":\"%s\",\"value\":%u,\"hz\":%u,\"duty\":%u}",
                args.gpio.pin,pin.mode==AGENT_GPIO_PWM?"pwm":pin.mode==AGENT_GPIO_WRITE?"output":"input",pin.value,pin.hz,pin.duty);
            error=w.error;
        }
        break;
    }
    case TOOL_STATUS: error = ops->status ? ops->status(ops->ctx, output, capacity) : AGENT_ERR_CONFIG; break;
    case TOOL_CONTEXT: error = ops->context_stats ? ops->context_stats(ops->ctx, output, capacity) : AGENT_ERR_CONFIG; break;
    case TOOL_LIGHT_GET: error = ops->light_get ? ops->light_get(ops->ctx, args.rgb) : AGENT_ERR_CONFIG; break;
    case TOOL_LIGHT_SET: error = ops->light_set ? ops->light_set(ops->ctx, args.rgb) : AGENT_ERR_CONFIG; break;
    case TOOL_SEARCH: error = ops->context_search ? ops->context_search(ops->ctx,args.search.query,args.search.before,args.search.limit,output,capacity) : AGENT_ERR_CONFIG; break;
    case TOOL_SUMMARY_GET: error = ops->summary_get ? ops->summary_get(ops->ctx,output,capacity) : AGENT_ERR_CONFIG; break;
    case TOOL_SUMMARY_SET:
        if(!ops->summary_set || !ops->summary_get) return AGENT_ERR_CONFIG;
        error = ops->summary_set(ops->ctx,args.summary.text,args.summary.through);
        if(!error) error=ops->summary_get(ops->ctx,output,capacity);
        break;
    case TOOL_CAPABILITIES: case TOOL_PLAN_VALIDATE: case TOOL_PLAN_RUN: case TOOL_PLAN_STATUS: case TOOL_PLAN_CANCEL:
        if(!ops->control) return AGENT_ERR_CONFIG;
        if(index==TOOL_CAPABILITIES) error=agent_control_capabilities(ops->control,output,capacity);
        else if(index==TOOL_PLAN_VALIDATE) {
            uint32_t mask; unsigned worst;
            error=agent_plan_validate(&args.plan,&ops->control->backend,&mask,&worst);
            if(!error) {
                agent_json_writer_t w; agent_json_writer_init(&w,output,capacity);
                agent_json_printf(&w,"{\"valid\":true,\"worst_ms\":%u,\"resources\":%u,\"available\":%s}",worst,mask,
                    (atomic_load(&ops->control->backend.resources->occupied)&mask)?"false":"true");
                error=w.error;
            }
        } else {
            if(index==TOOL_PLAN_RUN) error=agent_control_submit(ops->control,&args.plan);
            if(index==TOOL_PLAN_CANCEL) agent_control_cancel(ops->control);
            if(index==TOOL_PLAN_STATUS && ops->wait_ms) {
                unsigned job=atomic_load(&ops->control->job);
                for(unsigned waited=0;;) {
                    if(ops->cancelled && ops->cancelled(ops->ctx)) {error=AGENT_ERR_CANCELLED;break;}
                    if(!atomic_load(&ops->control->active) || atomic_load(&ops->control->job)!=job ||
                       waited>=args.wait_ms || (ops->now_ms && ops->now_ms(ops->ctx)-start>=args.wait_ms)) break;
                    unsigned delay=args.wait_ms-waited;
                    if(delay>25)delay=25;
                    ops->wait_ms(delay);waited+=delay;
                }
            }
            if(!error) error=agent_control_status(ops->control,output,capacity);
        }
        break;
#if AGENT_ENABLE_AUDIO
    case TOOL_AUDIO: case TOOL_SCORE: case TOOL_STOP: case TOOL_VOLUME: case TOOL_MIC: case TOOL_CAPTURE: case TOOL_REPLAY: case TOOL_SONG: {
        const agent_audio_ops_t *a = ops->audio;
        if (!a || !a->status) return AGENT_ERR_CONFIG;
        if (index == TOOL_SCORE) error = a->play ? a->play(a->ctx, &args.score) : AGENT_ERR_CONFIG;
        if (index == TOOL_SONG) error = a->play_song ? a->play_song(a->ctx, &args.song) : AGENT_ERR_CONFIG;
        if (index == TOOL_STOP) error = a->stop ? a->stop(a->ctx) : AGENT_ERR_CONFIG;
        if (index == TOOL_VOLUME) error = a->volume ? a->volume(a->ctx, args.value) : AGENT_ERR_CONFIG;
        if (index == TOOL_MIC) error = a->microphone ? a->microphone(a->ctx, args.value != 0) : AGENT_ERR_CONFIG;
        if (index == TOOL_CAPTURE) error = a->capture ? a->capture(a->ctx,args.value) : AGENT_ERR_CONFIG;
        if (index == TOOL_REPLAY) error = a->replay ? a->replay(a->ctx) : AGENT_ERR_CONFIG;
        if (!error) error = a->status(a->ctx, output, capacity);
        break;
    }
#endif
    default: return AGENT_ERR_TOOL;
    }
    unsigned budget=(index==TOOL_SEARCH || index==TOOL_SUMMARY_SET)?AGENT_CONTEXT_SCAN_MS:2000;
    if(index==TOOL_PLAN_STATUS)budget+=args.wait_ms;
    if (!error && ops->now_ms && ops->now_ms(ops->ctx) - start > budget) return AGENT_ERR_TIMEOUT;
    if (!error && (index == TOOL_LIGHT_GET || index == TOOL_LIGHT_SET)) {
        int size = snprintf(output, capacity, "{\"ok\":true,\"r\":%u,\"g\":%u,\"b\":%u}", args.rgb[0], args.rgb[1], args.rgb[2]);
        if (size < 0 || (size_t)size >= capacity) return AGENT_ERR_LIMIT;
    }
    return error;
}
