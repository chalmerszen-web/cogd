#include "asr_realtime.h"
#include "asr_end.h"
#include "json.h"
#include "pcm_json.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char configuration[]="\"session\":{\"id\":\"sess_test\",\"model\":\"" AGENT_ASR_RT_MODEL
    "\",\"modalities\":[\"text\"],\"input_audio_format\":\"pcm\",\"sample_rate\":16000}";
static const char final_text[]="请把灯调成蓝色，不对，不要蓝色，改成绿色。";
enum { NORMAL,WRONG_SESSION,AUTO_VAD,NO_FINAL,NO_FINISHED,EARLY_FINAL,DUPLICATE_FINAL,
       DUPLICATE_COMMIT,MISMATCH_ID,BAD_JSON,EMPTY_FINAL,PREMATURE_ACK,AMBIGUOUS_SEND,
       BINARY_INPUT,CANCEL_AFTER_SEND,PROVIDER_ERROR,EMPTY_VAD_PREFIX,EMPTY_VAD_TAIL,EMPTY_VAD_ALL };
enum { PACKETS=32 };
typedef struct {
    char packets[PACKETS][1024];size_t lengths[PACKETS],head,tail,offset,split,samples;
    unsigned fault,opens,closes,appends,commits,finishes,previews,finals,sequence,tries;
    bool block_second,blocked,vad;
    unsigned revisions,settled,last_revision;
    agent_asr_end_t endpoint;
    char transcript[2049];
} mock_t;
static mock_t mock;
static atomic_bool cancelled;
static uint64_t ticks;
static uint64_t now(void *ctx) { (void)ctx;return ticks; }
static int16_t sample(size_t i) { return (int16_t)(i*997u+32768u); }
static void packet(mock_t *m,const char *type,const char *fields)
{
    assert(m->tail-m->head<PACKETS);
    size_t i=m->tail++%PACKETS;
    int n=snprintf(m->packets[i],sizeof(m->packets[i]),"{\"event_id\":\"e%u\",\"type\":\"%s\"%s%s}",
                   ++m->sequence,type,fields[0]?",":"",fields);
    assert(n>0 && (size_t)n<sizeof(m->packets[i]));m->lengths[i]=(size_t)n;
}
static void partial(mock_t *m)
{ packet(m,"conversation.item.input_audio_transcription.text","\"content_index\":0,\"text\":\"请把灯\",\"stash\":\"调成蓝色。\""); }
static void final(mock_t *m)
{
    char fields[256];
    snprintf(fields,sizeof(fields),"\"content_index\":0,%s\"transcript\":\"%s\"",
        m->fault==MISMATCH_ID?"\"item_id\":\"foreign\",":"",m->fault==EMPTY_FINAL?"":final_text);
    packet(m,"conversation.item.input_audio_transcription.completed",fields);
}
static void vad_segment(mock_t *m)
{
    static const char *const words[]={"请把灯调成蓝色，不对。","不要蓝色。","改成绿色。"};
    unsigned index=m->appends-1;assert(index<3);
    char fields[256];
    snprintf(fields,sizeof(fields),"\"item_id\":\"part%u\",\"audio_start_ms\":%u",index,index*8);
    packet(m,"input_audio_buffer.speech_started",fields);
    snprintf(fields,sizeof(fields),"\"item_id\":\"part%u\",\"audio_end_ms\":%u",index,(index+1)*8);
    packet(m,"input_audio_buffer.speech_stopped",fields);
    snprintf(fields,sizeof(fields),"\"item_id\":\"part%u\"",index);
    packet(m,"input_audio_buffer.committed",fields);
    if(m->fault!=NO_FINAL) {
        snprintf(fields,sizeof(fields),"\"item_id\":\"part%u\",\"content_index\":0,\"transcript\":\"%s\"",
            m->fault==MISMATCH_ID?9:index,
            m->fault==EMPTY_VAD_ALL || (m->fault==EMPTY_VAD_PREFIX && !index) ||
            (m->fault==EMPTY_VAD_TAIL && index==2)?"":words[index]);
        packet(m,"conversation.item.input_audio_transcription.completed",fields);
    }
}
static agent_err_t open_ws(void *ctx,const atomic_bool *flag)
{
    mock_t *m=ctx;assert(!atomic_load(flag));++m->opens;
    packet(m,"session.created",configuration);return AGENT_OK;
}
static void close_ws(void *ctx) { ++((mock_t *)ctx)->closes; }
static unsigned digit(char c)
{
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *p=strchr(alphabet,c);
    assert(p && c);return (unsigned)(p-alphabet);
}
static void audio(mock_t *m,const char *base64)
{
    size_t length=strlen(base64),bytes=0;assert(length%4==0);
    for(size_t i=0;i<length;i+=4) {
        unsigned value=digit(base64[i])<<18|digit(base64[i+1])<<12;
        if(base64[i+2]!='=')value|=digit(base64[i+2])<<6;
        if(base64[i+3]!='=')value|=digit(base64[i+3]);
        unsigned n=base64[i+2]=='='?1:base64[i+3]=='='?2:3;
        assert(n==3 || i+4==length);
        for(unsigned j=0;j<n;++j) {
            unsigned want=(uint16_t)sample(m->samples+bytes/2)>>(8*(bytes%2))&255u;
            assert((value>>(16-8*j)&255u)==want);++bytes;
        }
    }
    assert(!(bytes%2) && bytes && bytes<=AGENT_ASR_RT_CHUNK*2);
    m->samples+=bytes/2;
}
static agent_err_t send_ws(void *ctx,bool binary,char *data,size_t length,const atomic_bool *flag)
{
    mock_t *m=ctx;assert(!binary && !atomic_load(flag) && length<2048);++m->tries;
    cJSON *root=agent_json_parse(data,length);assert(root);
    const char *type=agent_json_string(root,"type");assert(type);
    agent_err_t error=AGENT_OK;
    if(!strcmp(type,"session.update")) {
        const cJSON *s=cJSON_GetObjectItemCaseSensitive(root,"session");
        if(m->vad) {
            const cJSON *vad=cJSON_GetObjectItemCaseSensitive(s,"turn_detection");
            assert(!strcmp(agent_json_string(vad,"type"),"server_vad"));
            char fields[512];snprintf(fields,sizeof(fields),"%.*s,\"turn_detection\":{\"type\":\"server_vad\",\"threshold\":0.2,\"silence_duration_ms\":700,\"create_response\":true,\"interrupt_response\":true}}",
                (int)strlen(configuration)-1,configuration);
            packet(m,"session.updated",fields);
        } else {
            assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(s,"turn_detection")));
            packet(m,"session.updated",m->fault==WRONG_SESSION?
            "\"session\":{\"id\":\"other\"}":m->fault==AUTO_VAD?
            "\"session\":{\"id\":\"sess_test\",\"model\":\"" AGENT_ASR_RT_MODEL "\",\"modalities\":[\"text\"],\"input_audio_format\":\"pcm\",\"sample_rate\":16000,\"turn_detection\":{\"type\":\"server_vad\"}}":configuration);
        }
    } else if(!strcmp(type,"input_audio_buffer.append")) {
        if(m->fault==AMBIGUOUS_SEND)error=AGENT_ERR_NETWORK;
        else if(m->appends==1 && m->block_second && !m->blocked) {
            m->blocked=true;partial(m);error=AGENT_ERR_BUSY;
        } else {
            audio(m,agent_json_string(root,"audio"));++m->appends;
            if(m->vad) {
                vad_segment(m);
                if(m->fault==CANCEL_AFTER_SEND)atomic_store(&cancelled,true);
            } else if(m->appends==1) {
                packet(m,"conversation.item.created","\"item\":{\"type\":\"message\",\"status\":\"in_progress\",\"role\":\"assistant\",\"content\":[{\"type\":\"input_audio\"}]}");
                partial(m);
                if(m->fault==EARLY_FINAL)final(m);
                if(m->fault==CANCEL_AFTER_SEND)atomic_store(&cancelled,true);
                if(m->fault==BAD_JSON)m->packets[(m->tail-1)%PACKETS][0]='!';
                if(m->fault==PROVIDER_ERROR)packet(m,"error","\"error\":{\"code\":\"server_error\"}");
            }
        }
    } else if(!strcmp(type,"input_audio_buffer.commit")) {
        if(m->fault==PREMATURE_ACK && m->commits) {cJSON_Delete(root);return AGENT_ERR_BUSY;}
        ++m->commits;
        packet(m,"input_audio_buffer.committed",m->fault==MISMATCH_ID?"\"item_id\":\"input_one\"":"");
        if(m->fault==PREMATURE_ACK)error=AGENT_ERR_BUSY;
        else {
            if(m->fault==DUPLICATE_COMMIT)packet(m,"input_audio_buffer.committed","");
            if(m->fault!=NO_FINAL)final(m);
            if(m->fault==DUPLICATE_FINAL)final(m);
        }
    } else {
        assert(!strcmp(type,"session.finish"));++m->finishes;
        if(m->fault!=NO_FINISHED)packet(m,"session.finished","");
    }
    cJSON_Delete(root);return error;
}
static agent_err_t read_ws(void *ctx,char *out,size_t capacity,agent_ws_chunk_t *part,unsigned ms,const atomic_bool *flag)
{
    mock_t *m=ctx;assert(!atomic_load(flag));*part=(agent_ws_chunk_t){0};
    if(m->head==m->tail) {ticks+=ms?ms:1;return AGENT_OK;}
    ++ticks;
    size_t i=m->head%PACKETS,n=m->lengths[i]-m->offset;
    size_t split=m->split?m->split:1+(m->offset*17+m->head*5)%61;
    if(n>split)n=split;
    if(n>capacity)n=capacity;
    *part=(agent_ws_chunk_t){n,m->offset?0:1,!m->offset,m->offset+n==m->lengths[i]};
    if(m->fault==BINARY_INPUT && m->appends && part->first)part->opcode=2;
    memcpy(out,m->packets[i]+m->offset,n);m->offset+=n;
    if(part->final) {++m->head;m->offset=0;}
    return AGENT_OK;
}
static void observe(void *ctx,const char *stage,const char *text)
{
    mock_t *m=ctx;
    if(!strcmp(stage,"asr_partial")) {assert(text && *text && !m->closes);++m->previews;}
    if(!strcmp(stage,"asr_text")) {assert(m->closes==1 && m->finishes==1);++m->finals;}
}
static void sentence(void *ctx,uint32_t revision,unsigned end,bool final,bool nonempty,bool timed)
{
    mock_t *m=ctx;
    agent_asr_end_sentence(&m->endpoint,revision,end,final,nonempty,timed,(unsigned)ticks);
    if(revision!=m->last_revision) {
        assert(revision==m->last_revision+1 && !final && !end && !timed);
        assert(!m->endpoint.candidate_id); /* Uses the actual board endpoint guard. */
        ++m->revisions;m->last_revision=revision;
    }
    if(final) {
        if(timed)assert(nonempty && end==revision*8 && m->endpoint.candidate_id==revision);
        else assert(!end && !m->endpoint.candidate_id);
        ++m->settled;
    }
}
static void vad_session(unsigned fault,size_t split)
{
    mock=(mock_t){.vad=true,.fault=fault,.split=split};ticks=0;atomic_store(&cancelled,false);
    agent_asr_end_reset(&mock.endpoint);
    char scratch[8192];
    agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&mock};
    agent_speech_t speech={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&cancelled,
        .event=observe,.event_ctx=&mock,.asr_sentence=sentence,.asr_sentence_ctx=&mock};
    agent_asr_realtime_t q;agent_asr_segments_t segments;
    agent_asr_realtime_init(&q,&speech,&ws,mock.transcript,sizeof(mock.transcript));
    assert(!agent_asr_realtime_use_vad(&q,&segments));
    assert(agent_asr_realtime_use_vad(&q,&segments)==AGENT_ERR_BUSY);
    agent_err_t error=agent_asr_realtime_begin(&q);
    int16_t pcm[1001];for(size_t i=0;i<1001;++i)pcm[i]=sample(i);
    if(!error)error=agent_asr_realtime_feed_pcm(&q,pcm,1001);
    assert(!q.done && !mock.finals); /* Segment finals alone never complete input. */
    if(!error)error=agent_asr_realtime_finish(&q);
    assert(!mock.commits && mock.closes==1 && !q.opened);
    if(fault && fault!=EMPTY_VAD_PREFIX && fault!=EMPTY_VAD_TAIL)
        assert(error && !mock.transcript[0] && !mock.finals && !agent_asr_segments_settled(&segments));
    else {
        assert(!error && mock.samples==1001 && mock.finishes==1 && mock.finals==1 && q.done);
        assert(mock.revisions==3 && mock.settled==3);
        const char *expected=fault==EMPTY_VAD_PREFIX?"不要蓝色。\n改成绿色。\n":
            fault==EMPTY_VAD_TAIL?"请把灯调成蓝色，不对。\n不要蓝色。\n":
            "请把灯调成蓝色，不对。\n不要蓝色。\n改成绿色。\n";
        assert(!strcmp(mock.transcript,expected));
    }
}
static agent_err_t exercise(unsigned fault,size_t split,bool busy)
{
    mock=(mock_t){.fault=fault,.split=split,.block_second=busy};ticks=0;atomic_store(&cancelled,false);
    char scratch[8192];
    agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&mock};
    agent_speech_t speech={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&cancelled,.event=observe,.event_ctx=&mock};
    agent_asr_realtime_t q;agent_asr_realtime_init(&q,&speech,&ws,mock.transcript,sizeof(mock.transcript));
    agent_err_t error=agent_asr_realtime_begin(&q);
    int16_t pcm[1001];for(size_t i=0;i<1001;++i)pcm[i]=sample(i);
    if(!error)error=agent_asr_realtime_feed_pcm(&q,pcm,1001);
    if(!error)error=agent_asr_realtime_finish(&q);
    assert(mock.opens==1 && mock.closes==1 && !q.opened);
    if(error)assert(!mock.transcript[0] && !mock.finals);
    else {
        assert(!strcmp(mock.transcript,final_text) && mock.finals==1 && q.done);
        assert(mock.samples==1001 && q.input_samples==1001 && mock.appends==3 && mock.commits==1 && mock.finishes==1);
        assert(mock.previews==1+(unsigned)busy);
        assert(agent_asr_realtime_begin(&q)==AGENT_ERR_BUSY); /* Never reuse. */
    }
    if(fault==AMBIGUOUS_SEND)assert(mock.tries==2 && !mock.appends && !q.input_samples);
    return error;
}
static agent_err_t inject(agent_asr_realtime_t *q,const char *data,size_t n,size_t split)
{
    for(size_t at=0;at<n;) {
        size_t bytes=n-at;if(bytes>split)bytes=split;
        agent_ws_chunk_t part={bytes,at?0:1,!at,at+bytes==n};
        agent_err_t error=agent_asr_realtime_receive(q,data+at,&part);if(error)return error;
        at+=bytes;
    }
    return AGENT_OK;
}
static void receive_limits(void)
{
    mock=(mock_t){0};ticks=0;atomic_store(&cancelled,false);
    char scratch[8192],text[16],large[6144];memset(large,' ',sizeof(large));
    agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&mock};
    agent_speech_t s={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&cancelled};
    agent_asr_realtime_t q;agent_asr_realtime_init(&q,&s,&ws,text,sizeof(text));
    q.opened=q.created=q.ready=true;q.input_samples=1;q.deadline=1000;
    assert(inject(&q,large,sizeof(large),17)==AGENT_ERR_LIMIT && mock.closes==1);
    agent_asr_realtime_init(&q,&s,&ws,text,sizeof(text));
    q.opened=q.created=q.ready=true;q.input_samples=1;q.deadline=1000;
    const char *overflow="{\"event_id\":\"e\",\"type\":\"conversation.item.input_audio_transcription.text\",\"content_index\":0,\"text\":\"请把灯调成蓝色\",\"stash\":\"。\"}";
    assert(inject(&q,overflow,strlen(overflow),1)==AGENT_ERR_LIMIT && !text[0]);
    agent_asr_realtime_init(&q,&s,&ws,text,sizeof(text));
    q.opened=q.created=q.ready=true;q.deadline=1000;
    agent_ws_chunk_t part={1,1,true,false};
    assert(!agent_asr_realtime_receive(&q,"{",&part) && q.message);
    ticks=1001;
    assert(agent_asr_realtime_poll(&q,0)==AGENT_ERR_TIMEOUT && !text[0]);
    agent_asr_realtime_init(&q,&s,&ws,text,sizeof(text));
    q.opened=q.ready=true;q.input_samples=AGENT_ASR_RT_INPUT_MAX;q.deadline=2000;
    int16_t pcm=0;
    assert(agent_asr_realtime_feed_pcm(&q,&pcm,1)==AGENT_ERR_LIMIT);
}
static void raw_prefix(const char *path)
{
    FILE *file=fopen(path,"rb");assert(file);mock=(mock_t){0};ticks=0;atomic_store(&cancelled,false);
    char scratch[8192],text[2049],line[6144];unsigned frames=0;bool rejected=false;
    agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&mock};
    agent_speech_t s={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&cancelled};
    agent_asr_realtime_t q;agent_asr_realtime_init(&q,&s,&ws,text,sizeof(text));
    q.opened=true;q.deadline=100000;
    while(fgets(line,sizeof(line),file)) {
        /* This offline replay supplies known send barriers from the paired
         * traffic log; it is not a new connection or a successful finish. */
        if(strstr(line,"\"type\":\"session.updated\""))q.updating=true;
        if(strstr(line,"\"type\":\"conversation.item.created\""))q.input_samples=47040;
        if(strstr(line,"\"type\":\"input_audio_buffer.committed\""))q.committing=true;
        bool had_final=q.final;
        agent_err_t error=inject(&q,line,strlen(line),7);++frames;
        if(error) {assert(had_final && error==AGENT_ERR_PROTOCOL && !text[0]);rejected=true;break;}
        if(q.final)assert(!strcmp(text,"请用一句话介绍你自己。"));
    }
    fclose(file);assert(rejected && mock.closes==1);
    printf("raw_prefix_frames=%u second_input_rejected=true completed_session=false\n",frames);
}
static void raw_vad(const char *path,const char *report_path)
{
    cJSON *report=NULL;unsigned count=3;
    if(report_path) {
        FILE *file=fopen(report_path,"rb");assert(file && !fseek(file,0,SEEK_END));
        long bytes=ftell(file);assert(bytes>0 && bytes<=262144 && !fseek(file,0,SEEK_SET));
        char *data=malloc((size_t)bytes);assert(data);
        assert(fread(data,1,(size_t)bytes,file)==(size_t)bytes && !fclose(file));
        /* Host-only evidence manifest exceeds the device request budget.
         * Individual replayed wire events still use agent_json_parse. */
        report=cJSON_ParseWithLength(data,(size_t)bytes);free(data);assert(report);
        const cJSON *fixtures=cJSON_GetObjectItemCaseSensitive(report,"fixtures");
        count=(unsigned)cJSON_GetArraySize(fixtures);assert(count && count<=6);
    }
    for(unsigned round=1;round<=count;++round) {
        FILE *file=fopen(path,"rb");assert(file);
        mock=(mock_t){0};ticks=0;atomic_store(&cancelled,false);
        char scratch[8192],line[8192];
        agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&mock};
        agent_speech_t s={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&cancelled};
        agent_asr_realtime_t q;agent_asr_segments_t segments;
        agent_asr_realtime_init(&q,&s,&ws,mock.transcript,sizeof(mock.transcript));
        assert(!agent_asr_realtime_use_vad(&q,&segments));q.opened=true;q.deadline=100000;
        unsigned received=0;
        while(fgets(line,sizeof(line),file)) {
            assert(strchr(line,'\n'));
            cJSON *root=agent_json_parse(line,strlen(line));assert(root);uint64_t number;
            assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"round"),count,&number));
            if(number!=round) {cJSON_Delete(root);continue;}
            const cJSON *e=cJSON_GetObjectItemCaseSensitive(root,"event");
            const char *direction=agent_json_string(root,"direction"),*type=agent_json_string(e,"type");
            assert(direction && type);
            if(!strcmp(direction,"send")) {
                /* Paired audited send receipts supply the receive parser's
                 * actual byte/finish barriers; no network is opened here. */
                if(!strcmp(type,"session.update")) {assert(q.created && !q.updating);q.updating=true;}
                else if(!strcmp(type,"session.finish")) {assert(q.ready && !q.finishing);q.finishing=true;}
                else {
                    uint64_t count;assert(!strcmp(type,"input_audio_buffer.append") && q.ready && !q.finishing);
                    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(e,"samples"),464,&count) && count);
                    q.input_samples+=(size_t)count;assert(q.input_samples<=AGENT_ASR_RT_INPUT_MAX);
                }
            } else {
                assert(!strcmp(direction,"receive"));
                char *event=cJSON_PrintUnformatted(e);assert(event);
                assert(!inject(&q,event,strlen(event),1+(received*17)%43));
                cJSON_free(event);++received;
            }
            cJSON_Delete(root);
        }
        assert(!ferror(file) && !fclose(file));
        assert(q.done && q.final && agent_asr_segments_settled(&segments));
        char aggregate[2049]={0};
        const char *expected=round==2?"请把灯调成蓝色，不对。\n不要蓝色。\n改成绿色。\n":"请用一句话介绍你自己。\n";
        size_t samples=round==2?123120u:58240u;
        if(report) {
            const cJSON *fixture=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(report,"fixtures"),(int)round-1);
            uint64_t n;assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(fixture,"samples"),160000,&n));samples=(size_t)n;
            const cJSON *trial=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(report,"trials"),(int)round-1);
            const cJSON *finals=cJSON_GetObjectItemCaseSensitive(trial,"finals");
            assert(cJSON_IsArray(finals));
            for(const cJSON *item=finals->child;item;item=item->next) {
                const char *words=agent_json_string(item,"text");assert(words);
                if(*words) {assert(strlen(aggregate)+strlen(words)+1<sizeof(aggregate));strcat(aggregate,words);strcat(aggregate,"\n");}
            }
            expected=aggregate;
        }
        assert(!strcmp(mock.transcript,expected));
        assert(q.input_samples==samples);
        printf("vad_raw_round=%u received=%u segments=%u samples=%zu receipt_text_retained=true receive_parser_only=true\n",
               round,received,segments.count,q.input_samples);
        agent_asr_realtime_cancel(&q);assert(mock.closes==1 && !mock.transcript[0]);
    }
    cJSON_Delete(report);
}
typedef struct { size_t offset,total;unsigned reads,fault; } source_t;
static agent_err_t source_next(void *ctx,int16_t *pcm,size_t cap,size_t *count,bool *end)
{
    source_t *s=ctx;++s->reads;*count=0;*end=false;
    if(s->fault==4)atomic_store(&cancelled,true);
    if(s->reads<=2)return AGENT_OK; /* Idle is not EOF. */
    if(s->fault==1 && s->offset)return AGENT_ERR_CORRUPT;
    if(s->fault==2) {*count=cap+1;return AGENT_OK;}
    if(s->fault==3) {*end=true;return AGENT_OK;}
    size_t total=s->total?s->total:1001,n=total-s->offset;if(n>cap)n=cap;
    for(size_t i=0;i<n;++i)pcm[i]=sample(s->offset+i);
    s->offset+=n;*count=n;*end=s->offset==total;return AGENT_OK;
}
static agent_err_t recorded_read(void *ctx,size_t offset,int16_t *out,size_t count)
{
    (void)ctx;for(size_t i=0;i<count;++i)out[i]=sample(offset+i);return AGENT_OK;
}
static void source_checks(void)
{
    for(unsigned shared=0;shared<2;++shared)
    for(unsigned vad=0;vad<2;++vad)
    for(unsigned which=0;which<6;++which) {
        mock=(mock_t){.split=31,.vad=vad!=0};ticks=0;atomic_store(&cancelled,false);
        char scratch[8192];unsigned char *raw=shared?malloc(8192+32):NULL;assert(!shared || raw);
        if(raw)memset(raw,0x5a,8192+32);
        agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&mock};
        agent_speech_t s={.scratch=shared?(char *)raw+16:scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&cancelled,
                          .event=observe,.event_ctx=&mock};
        agent_asr_realtime_t q;agent_asr_realtime_init(&q,&s,&ws,mock.transcript,sizeof(mock.transcript));
        agent_asr_segments_t segments;
        if(vad)assert(!agent_asr_realtime_use_vad(&q,&segments));
        source_t source={.fault=which};agent_speech_live_t live={source_next,&source};
        agent_speech_input_t recorded={1001,recorded_read,NULL};
        agent_err_t error=which==5?agent_asr_realtime_transcribe(&q,&recorded):shared?
            agent_asr_realtime_live_shared(&q,&live):agent_asr_realtime_live(&q,&live);
        static const agent_err_t expected[]={AGENT_OK,AGENT_ERR_CORRUPT,AGENT_ERR_LIMIT,AGENT_ERR_PROTOCOL,AGENT_ERR_CANCELLED,AGENT_OK};
        assert(error==expected[which] && mock.closes==1 && mock.opens==1);
        if(error)assert(!mock.commits && !mock.finishes && !mock.finals && !mock.transcript[0]);
        else {
            assert(mock.samples==1001 && mock.appends==3 && mock.commits==!vad && mock.finishes==1 && mock.finals==1);
            if(vad)assert(agent_asr_segments_settled(&segments) && segments.count==3 &&
                !strcmp(mock.transcript,"请把灯调成蓝色，不对。\n不要蓝色。\n改成绿色。\n"));
        }
        if(raw)for(unsigned i=0;i<16;++i)assert(raw[i]==0x5a && raw[8192+16+i]==0x5a);
        free(raw);
    }
}
static void shared_checks(void)
{
    unsigned char *raw=malloc(8192+32);assert(raw);
    agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&mock};
    agent_speech_t s={.scratch=(char *)raw+16,.capacity=8192,.now_ms=now,.cancelled=&cancelled};
    agent_asr_realtime_t q;source_t source={0};agent_speech_live_t live={source_next,&source};
    /* All tail sizes and base64 padding cases, including maximum nonce and
     * a BUSY send which receives fragmented text before retrying the frame. */
    for(unsigned total=1;total<=AGENT_ASR_RT_CHUNK;++total) {
        mock=(mock_t){.split=31,.block_second=true};ticks=0;atomic_store(&cancelled,false);
        memset(raw,0x5a,8192+32);s.nonce=UINT32_MAX-1;source=(source_t){.total=total};
        agent_asr_realtime_init(&q,&s,&ws,mock.transcript,sizeof(mock.transcript));
        assert(!agent_asr_realtime_live_shared(&q,&live));
        assert(mock.samples==total && mock.appends==1 && mock.commits==1 && mock.finishes==1 && mock.closes==1);
        for(unsigned i=0;i<16;++i)assert(raw[i]==0x5a && raw[8192+16+i]==0x5a);
    }
    mock=(mock_t){.split=31,.block_second=true};ticks=0;atomic_store(&cancelled,false);source=(source_t){0};
    agent_asr_realtime_init(&q,&s,&ws,mock.transcript,sizeof(mock.transcript));
    assert(!agent_asr_realtime_live_shared(&q,&live) && mock.blocked && mock.samples==1001);
    /* Invalid loans fail before opening or touching the capture source. */
    mock=(mock_t){0};ticks=0;source=(source_t){0};
    s.scratch=(char *)raw+17;agent_asr_realtime_init(&q,&s,&ws,mock.transcript,sizeof(mock.transcript));
    assert(agent_asr_realtime_live_shared(&q,&live)==AGENT_ERR_ARGUMENT && !mock.opens && !source.reads);
    s.scratch=(char *)raw+16;s.capacity=8191;
    assert(agent_asr_realtime_live_shared(&q,&live)==AGENT_ERR_ARGUMENT && !mock.opens);
    s.capacity=8192;q.text=s.scratch;
    assert(agent_asr_realtime_live_shared(&q,&live)==AGENT_ERR_ARGUMENT && !mock.opens);
    q.text=mock.transcript;agent_asr_realtime_t *aliased=(agent_asr_realtime_t *)(raw+16);
    agent_asr_realtime_init(aliased,&s,&ws,mock.transcript,sizeof(mock.transcript));
    assert(agent_asr_realtime_live_shared(aliased,&live)==AGENT_ERR_ARGUMENT && !mock.opens);
    free(raw);
}
static void encoding_checks(void)
{
    int16_t pcm[512];for(unsigned i=0;i<512;++i)pcm[i]=sample(i);
    for(unsigned count=0;count<=512;++count) {
        char out[1370];agent_json_writer_t w;agent_json_writer_init(&w,out,sizeof(out));
        agent_pcm_json(&w,pcm,count);assert(!w.error && w.used==4*((count*2+2)/3));
        unsigned at=0;
        for(size_t i=0;i<w.used;i+=4) {
            unsigned v=digit(out[i])<<18|digit(out[i+1])<<12;
            if(out[i+2]!='=')v|=digit(out[i+2])<<6;
            if(out[i+3]!='=')v|=digit(out[i+3]);
            unsigned bytes=out[i+2]=='='?1:out[i+3]=='='?2:3;
            for(unsigned j=0;j<bytes;++j,++at)
                assert((v>>(16-8*j)&255u)==((uint16_t)pcm[at/2]>>(8*(at%2))&255u));
        }
        assert(at==count*2);
    }
    char out[16];agent_json_writer_t w;int16_t known=0x1234;
    agent_json_writer_init(&w,out,sizeof(out));agent_pcm_json(&w,&known,1);
    assert(!w.error && !strcmp(out,"NBI="));
    agent_json_writer_init(&w,out,4);agent_pcm_json(&w,&known,1);
    assert(w.error==AGENT_ERR_LIMIT && !w.used);
    agent_json_writer_init(&w,out,sizeof(out));agent_pcm_json(&w,NULL,0);assert(!w.error && !w.used);
    agent_pcm_json(&w,NULL,1);assert(w.error==AGENT_ERR_ARGUMENT);
    agent_json_writer_init(&w,out,sizeof(out));agent_pcm_json(&w,pcm,513);assert(w.error==AGENT_ERR_ARGUMENT);
}
int main(int argc,char **argv)
{
    shared_checks();
    if(argc==3 && !strcmp(argv[1],"--wire")) {raw_prefix(argv[2]);return 0;}
    if(argc==3 && !strcmp(argv[1],"--vad-traffic")) {raw_vad(argv[2],NULL);return 0;}
    if(argc==4 && !strcmp(argv[1],"--vad-study")) {raw_vad(argv[2],argv[3]);return 0;}
    for(unsigned split=1;split<=127;split+=21)assert(exercise(NORMAL,split,false)==AGENT_OK);
    assert(exercise(NORMAL,0,true)==AGENT_OK);
    const agent_err_t errors[]={AGENT_OK,AGENT_ERR_PROTOCOL,AGENT_ERR_PROTOCOL,AGENT_ERR_TIMEOUT,AGENT_ERR_TIMEOUT,
        AGENT_ERR_PROTOCOL,AGENT_ERR_PROTOCOL,AGENT_ERR_PROTOCOL,AGENT_ERR_PROTOCOL,AGENT_ERR_JSON,
        AGENT_ERR_PROTOCOL,AGENT_ERR_PROTOCOL,AGENT_ERR_NETWORK,AGENT_ERR_PROTOCOL,AGENT_ERR_CANCELLED,AGENT_ERR_SERVER};
    for(unsigned fault=1;fault<=PROVIDER_ERROR;++fault)assert(exercise(fault,17,false)==errors[fault]);
    receive_limits();
    source_checks();
    encoding_checks();
    for(unsigned split=1;split<=127;split+=21)vad_session(NORMAL,split);
    vad_session(NO_FINAL,11);vad_session(NO_FINISHED,11);
    vad_session(MISMATCH_ID,11);vad_session(CANCEL_AFTER_SEND,11);
    vad_session(EMPTY_VAD_PREFIX,11);vad_session(EMPTY_VAD_TAIL,11);vad_session(EMPTY_VAD_ALL,11);
    printf("isolated_asr state_bytes=%zu all_checks_passed\n",sizeof(agent_asr_realtime_t));return 0;
}
