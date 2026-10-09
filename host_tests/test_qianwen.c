#include "qianwen.h"
#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char packets[8][800],id[129];size_t lengths[8],offset,head,tail,split,audio_sent,wait_head;
    unsigned opcode[8],opens,closes,starts,finishes,busy,binary_ms,read_ms,binary_attempts;
    bool asr,fail,wrong_id,never_start,cancel_audio,block_binary,block_tts;
    unsigned continues;
    char pending[2048];size_t pending_length;
    int16_t pcm[1024];size_t pcm_count;unsigned pcm_opens;
} mock_t;
static uint64_t ticks;
static atomic_bool stopped;
static agent_err_t read_ws(void *,char *,size_t,agent_ws_chunk_t *,unsigned,const atomic_bool *);
static agent_err_t pcm_open(void *,unsigned);
static agent_err_t pcm_write(void *,const int16_t *,size_t);
static uint64_t now(void *ctx) { (void)ctx;return ticks; }
static void wait_ms(unsigned ms) { ticks+=ms; }
static void packet(mock_t *m,const char *event,const char *payload)
{
    size_t i=m->tail++%8;
    int n=snprintf(m->packets[i],sizeof(m->packets[i]),"{\"header\":{\"task_id\":\"%s\",\"event\":\"%s\"},\"payload\":%s}",m->wrong_id?"wrong":m->id,event,payload);
    assert(n>0 && (size_t)n<sizeof(m->packets[i]));m->lengths[i]=(size_t)n;m->opcode[i]=1;
}
static agent_err_t open_ws(void *ctx,const atomic_bool *cancelled)
{ mock_t *m=ctx;assert(!atomic_load(cancelled));++m->opens;return m->fail?AGENT_ERR_AUTH:AGENT_OK; }
static void close_ws(void *ctx) { ++((mock_t *)ctx)->closes; }
static agent_err_t send_ws(void *ctx,bool binary,char *data,size_t length,const atomic_bool *cancelled)
{
    mock_t *m=ctx;assert(!atomic_load(cancelled));
    if(m->pending_length)assert(!binary && length==m->pending_length && !memcmp(data,m->pending,length));
    if(m->busy) {--m->busy;return AGENT_ERR_BUSY;}
    if(m->wait_head>m->head)return AGENT_ERR_BUSY;
    if(binary) {
        ++m->binary_attempts;ticks+=m->binary_ms;
        if(m->block_binary) {
            m->block_binary=false;
            packet(m,"result-generated","{\"output\":{\"sentence\":{\"sentence_id\":1,\"text\":\"请开\",\"sentence_end\":false}}}");
            m->wait_head=m->tail;return AGENT_ERR_BUSY;
        }
        assert(m->asr && m->starts==1 && length<=2048);m->audio_sent+=length;
        if(m->cancel_audio)atomic_store(&stopped,true);
        return AGENT_OK;
    }
    cJSON *root=agent_json_parse(data,length);assert(root);
    const cJSON *header=cJSON_GetObjectItemCaseSensitive(root,"header");
    const char *action=agent_json_string(header,"action"),*id=agent_json_string(header,"task_id");
    if(!strcmp(action,"run-task")) {
        ++m->starts;assert(m->starts==1);strcpy(m->id,id);
        const cJSON *payload=cJSON_GetObjectItemCaseSensitive(root,"payload");
        m->asr=!strcmp(agent_json_string(payload,"task"),"asr");
        assert(!strcmp(agent_json_string(payload,"model"),m->asr?AGENT_QWEN_ASR_MODEL:AGENT_QWEN_TTS_MODEL));
        if(!m->never_start)packet(m,"task-started","{}");
    } else {
        assert(!strcmp(id,m->id));
        if(!strcmp(action,"continue-task")) {
            assert(!m->asr);
            if(m->block_tts && m->continues==1) {
                assert(length<sizeof(m->pending));memcpy(m->pending,data,length);m->pending_length=length;
                m->block_tts=false;m->wait_head=m->tail;cJSON_Delete(root);return AGENT_ERR_BUSY;
            }
            m->pending_length=0;++m->continues;
            size_t i=m->tail++%8;m->opcode[i]=2;m->lengths[i]=600;
            for(size_t j=0;j<300;++j) {int16_t value=(int16_t)(j*123-17000);m->packets[i][2*j]=(char)value;m->packets[i][2*j+1]=(char)((uint16_t)value>>8);}
        } else {
            assert(!strcmp(action,"finish-task"));++m->finishes;
            if(m->asr) {
                packet(m,"result-generated","{\"output\":{\"sentence\":{\"sentence_id\":1,\"text\":\"蓝色\",\"sentence_end\":false}}}");
                for(unsigned i=0;i<2;++i)packet(m,"result-generated","{\"output\":{\"sentence\":{\"sentence_id\":1,\"text\":\"请开蓝灯。\",\"sentence_end\":true}}}");
            }
            packet(m,"task-finished","{}");
        }
    }
    cJSON_Delete(root);return AGENT_OK;
}
static void rx_loan_checks(void)
{
    unsigned char *raw=malloc(8192+32);assert(raw);memset(raw,0x5a,8192+32);
    mock_t m={.split=17,.block_tts=true};agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&m};
    agent_speech_t s={.scratch=(char *)raw+16,.capacity=8192,.now_ms=now,.cancelled=&stopped};
    agent_qwen_t q;agent_qwen_init(&q,&s,&ws);
    assert(!agent_qwen_use_rx_pcm(&q));assert(agent_qwen_use_rx_pcm(&q)==AGENT_ERR_BUSY);
    agent_pcm_sink_t sink={pcm_open,pcm_write,&m};
    char text[513];memset(text,'a',256);memset(text+256,'b',256);text[512]=0;
    ticks=0;atomic_store(&stopped,false);
    assert(!agent_qianwen_tts.speak(&s,AGENT_QWEN_VOICE,text,&sink));
    assert(q.rx_pcm && m.continues==2 && !m.block_tts && !m.pending_length && m.pcm_count==600 && m.closes==1);
    for(unsigned i=0;i<600;++i)assert(m.pcm[i]==(int16_t)((i%300)*123-17000));
    for(unsigned i=0;i<16;++i)assert(raw[i]==0x5a && raw[8192+16+i]==0x5a);
    /* Decoder slot never changes pending TX, including byte-split extrema. */
    m=(mock_t){0};agent_qwen_init(&q,&s,&ws);assert(!agent_qwen_use_rx_pcm(&q));
    q.started=true;q.deadline=UINT64_MAX;q.sink=sink;
    memset(s.scratch+6144,0x3c,2048);
    const int16_t values[]={INT16_MIN,INT16_MAX,-1,0,1,0x1234};
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i)
    for(unsigned byte=0;byte<2;++byte) {
        char data=(char)((uint16_t)values[i]>>(byte*8));agent_ws_chunk_t part={1,2,true,true};
        assert(!agent_qwen_receive(&q,&data,&part));
    }
    assert(m.pcm_count==sizeof(values)/sizeof(*values) && !memcmp(m.pcm,values,sizeof(values)) && !q.odd);
    for(unsigned i=0;i<2048;++i)assert((unsigned char)s.scratch[6144+i]==0x3c);
    size_t before=q.pcm_bytes;agent_ws_chunk_t part={2,2,true,true};
    assert(agent_qwen_receive(&q,s.scratch,&part)==AGENT_ERR_ARGUMENT && q.pcm_bytes==before);
    agent_qwen_init(&q,&s,&ws);assert(!q.rx_pcm);s.scratch=(char *)raw+17;
    assert(agent_qwen_use_rx_pcm(&q)==AGENT_ERR_ARGUMENT);
    s.scratch=(char *)raw+16;s.capacity=8191;assert(agent_qwen_use_rx_pcm(&q)==AGENT_ERR_ARGUMENT);
    s.capacity=8192;q.opened=true;assert(agent_qwen_use_rx_pcm(&q)==AGENT_ERR_BUSY);
    q.opened=false;agent_qwen_t *aliased=(agent_qwen_t *)(raw+16);agent_qwen_init(aliased,&s,&ws);
    assert(agent_qwen_use_rx_pcm(aliased)==AGENT_ERR_ARGUMENT);
    free(raw);
}
static agent_err_t read_ws(void *ctx,char *data,size_t cap,agent_ws_chunk_t *p,unsigned ms,const atomic_bool *cancelled)
{
    mock_t *m=ctx;assert(!atomic_load(cancelled));*p=(agent_ws_chunk_t){0};ticks+=m->read_ms;
    if(m->head==m->tail) {ticks+=ms;return AGENT_OK;}
    ++ticks;size_t i=m->head%8,n=m->lengths[i]-m->offset;
    if(n>m->split)n=m->split;if(n>cap)n=cap;
    *p=(agent_ws_chunk_t){n,m->opcode[i],!m->offset,m->offset+n==m->lengths[i]};
    memcpy(data,m->packets[i]+m->offset,n);m->offset+=n;
    if(p->final) {++m->head;m->offset=0;}
    return AGENT_OK;
}
static agent_err_t clip_read(void *ctx,size_t offset,int16_t *data,size_t n)
{ (void)ctx;for(size_t i=0;i<n;++i)data[i]=(int16_t)(offset+i);return AGENT_OK; }
static agent_err_t pcm_open(void *ctx,unsigned rate)
{ assert(rate==24000);++((mock_t *)ctx)->pcm_opens;return AGENT_OK; }
static agent_err_t pcm_write(void *ctx,const int16_t *data,size_t n)
{
    mock_t *m=ctx;if(m->fail)return AGENT_ERR_CANCELLED;
    assert(m->pcm_count+n<=1024);memcpy(m->pcm+m->pcm_count,data,n*2);m->pcm_count+=n;return AGENT_OK;
}
typedef struct { uint32_t id;unsigned end;bool final,nonempty,timed; } notice_t;
typedef struct { notice_t notices[64];size_t count;unsigned partials,finals; } observer_t;
static void sentence_notice(void *ctx,uint32_t id,unsigned end,bool final,bool nonempty,bool timed)
{
    observer_t *o=ctx;assert(o->count<64);
    o->notices[o->count++]=(notice_t){id,end,final,nonempty,timed};
}
static void observe_event(void *ctx,const char *stage,const char *text)
{
    observer_t *o=ctx;(void)text;
    if(!strcmp(stage,"asr_partial"))++o->partials;
    if(!strcmp(stage,"asr_final"))++o->finals;
}
static agent_err_t sentence_message(agent_qwen_t *q,const char *sentence,size_t split)
{
    char json[1024];int n=snprintf(json,sizeof(json),"{\"header\":{\"event\":\"result-generated\",\"task_id\":\"one\"},\"payload\":{\"output\":{\"sentence\":%s}}}",sentence);
    assert(n>0 && (size_t)n<sizeof(json));observer_t *o=q->speech->asr_sentence_ctx;
    size_t before=o->count;
    for(size_t offset=0;offset<(size_t)n;) {
        size_t length=(size_t)n-offset;if(length>split)length=split;
        agent_ws_chunk_t p={length,offset?0:1,true,offset+length==(size_t)n};
        agent_err_t e=agent_qwen_receive(q,json+offset,&p);if(e)return e;
        if(!p.final)assert(o->count==before); /* Never publish incomplete JSON/UTF-8. */
        offset+=length;
    }
    return AGENT_OK;
}
static void check_notice(const observer_t *o,size_t index,uint32_t id,unsigned end,bool final,bool nonempty,bool timed)
{
    assert(index<o->count);const notice_t *n=&o->notices[index];
    assert(n->id==id && n->end==end && n->final==final && n->nonempty==nonempty && n->timed==timed);
}
static void sentence_checks(void)
{
    for(size_t split=1;split<=31;split+=5) {
        observer_t o={0};char scratch[8192],out[128]={0};
        agent_speech_t s={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&stopped,
            .event=observe_event,.event_ctx=&o,.asr_sentence=sentence_notice,.asr_sentence_ctx=&o};
        agent_qwen_t q;agent_qwen_init(&q,&s,NULL);q.deadline=100000;q.started=true;q.asr=true;
        q.transcript=out;q.transcript_cap=sizeof(out);strcpy(s.task_id,"one");
        const char *partial="{\"sentence_id\":1,\"text\":\"你\",\"sentence_end\":false,\"begin_time\":0,\"end_time\":400}";
        assert(!sentence_message(&q,partial,split));assert(!sentence_message(&q,partial,split));
        check_notice(&o,0,1,400,false,true,true);check_notice(&o,1,1,400,false,true,true);
        assert(o.partials==1 && q.sentence==0 && !out[0]);
        /* The provider announces a new sentence before it has any text. */
        assert(!sentence_message(&q,"{\"sentence_id\":2,\"text\":\"\",\"sentence_end\":false,\"begin_time\":500,\"end_time\":null}",split));
        check_notice(&o,2,2,0,false,false,false);
        assert(o.count==3 && o.partials==1 && q.sentence==0 && !out[0]);
        const char *final="{\"sentence_id\":1,\"text\":\"你好\",\"sentence_end\":true,\"begin_time\":0,\"end_time\":700}";
        assert(!sentence_message(&q,final,split));assert(!sentence_message(&q,final,split));
        check_notice(&o,3,1,700,true,true,true);assert(o.count==4 && o.finals==1);
        /* Newer partials do not discard a still-uncommitted earlier final. */
        assert(!sentence_message(&q,"{\"sentence_id\":3,\"text\":\"小\",\"sentence_end\":false,\"begin_time\":1000,\"end_time\":null}",split));
        assert(!sentence_message(&q,"{\"sentence_id\":2,\"text\":\"，\",\"sentence_end\":true,\"begin_time\":701,\"end_time\":1000}",split));
        check_notice(&o,4,3,0,false,true,false);check_notice(&o,5,2,1000,true,true,true);
        /* Preserve IDs even for late partials so the consumer can order them. */
        assert(!sentence_message(&q,partial,split));check_notice(&o,6,1,400,false,true,true);
        const char *empty="{\"sentence_id\":3,\"text\":\"\",\"sentence_end\":true,\"begin_time\":1000,\"end_time\":1200}";
        assert(!sentence_message(&q,empty,split));assert(!sentence_message(&q,empty,split));
        assert(!sentence_message(&q,final,split));check_notice(&o,7,3,1200,true,false,true);assert(o.count==8);
        assert(!sentence_message(&q,"{\"sentence_id\":4,\"text\":\"小言。\",\"sentence_end\":true,\"begin_time\":1200,\"end_time\":2000}",split));
        assert(!strcmp(out,"你好，小言。") && o.finals==4 && o.partials==1);
        assert(!sentence_message(&q,"{\"heartbeat\":true,\"sentence_id\":0}",split));
        assert(!sentence_message(&q,"{\"heartbeat\":true,\"sentence_id\":9,\"text\":\"忽略\",\"sentence_end\":true}",split));
        assert(o.count==9 && q.sentence==4);
        /* Optional timing never invalidates otherwise legal transcription. */
        const char *bad_times[]={"",",\"begin_time\":0",",\"end_time\":500",
            ",\"begin_time\":0,\"end_time\":0",",\"begin_time\":-1,\"end_time\":500",
            ",\"begin_time\":0,\"end_time\":-1",",\"begin_time\":0,\"end_time\":10001",
            ",\"begin_time\":501,\"end_time\":500",",\"begin_time\":0.5,\"end_time\":500",
            ",\"begin_time\":0,\"end_time\":500.5",",\"begin_time\":0,\"end_time\":\"500\"",
            ",\"begin_time\":null,\"end_time\":500",",\"begin_time\":0,\"end_time\":null"};
        for(size_t i=0;i<sizeof(bad_times)/sizeof(*bad_times);++i) {
            char sentence[256];uint32_t id=(uint32_t)i+5;size_t before=o.count,used=strlen(out);
            snprintf(sentence,sizeof(sentence),"{\"sentence_id\":%u,\"text\":\"a\",\"sentence_end\":true%s}",(unsigned)id,bad_times[i]);
            assert(!sentence_message(&q,sentence,split));assert(o.count==before+1 && strlen(out)==used+1);
            check_notice(&o,before,id,0,true,true,false);
        }
        assert(!sentence_message(&q,"{\"sentence_id\":30,\"text\":\"好\",\"sentence_end\":true,\"begin_time\":10000,\"end_time\":10000}",split));
        check_notice(&o,o.count-1,30,10000,true,true,true);
        size_t before=o.count;unsigned finals=o.finals;
        assert(sentence_message(&q,"{\"sentence_id\":0,\"text\":\"bad\"}",split)==AGENT_ERR_PROTOCOL);
        assert(sentence_message(&q,"{\"text\":\"bad\"}",split)==AGENT_ERR_PROTOCOL);
        assert(sentence_message(&q,"{\"sentence_id\":31,\"text\":null}",split)==AGENT_ERR_PROTOCOL);
        q.transcript_cap=q.transcript_used+1;
        assert(sentence_message(&q,"{\"sentence_id\":31,\"text\":\"full\",\"sentence_end\":true}",split)==AGENT_ERR_LIMIT);
        assert(o.count==before && o.finals==finals && q.sentence==30);
        /* Cancellation midway through a message cannot publish its final. */
        agent_ws_chunk_t p={1,1,true,false};assert(!agent_qwen_receive(&q,"{",&p));
        atomic_store(&stopped,true);p=(agent_ws_chunk_t){1,0,true,true};
        assert(agent_qwen_receive(&q,"}",&p)==AGENT_ERR_CANCELLED);
        atomic_store(&stopped,false);assert(o.count==before && o.finals==finals);
    }
}
static void event_checks(void)
{
    char scratch[8192],out[128];agent_speech_t s={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=now,.cancelled=&stopped};
    agent_qwen_t q;agent_qwen_init(&q,&s,NULL);q.deadline=100000;q.started=true;q.asr=true;q.transcript=out;q.transcript_cap=sizeof(out);out[0]=0;
    strcpy(s.task_id,"one");
    const char *message="{\"header\":{\"event\":\"result-generated\",\"task_id\":\"one\"},\"payload\":{\"output\":{\"sentence\":{\"sentence_id\":1,\"text\":\"你好，小言\",\"sentence_end\":true}}}}";
    size_t length=strlen(message);
    for(size_t i=0;i<length;++i) {
        /* A fragmented UTF-8 text message with a continuation frame per byte. */
        agent_ws_chunk_t p={1,i?0:1,true,i+1==length};assert(!agent_qwen_receive(&q,message+i,&p));
    }
    assert(!strcmp(out,"你好，小言"));
    agent_ws_chunk_t p={length,1,true,true};assert(!agent_qwen_receive(&q,message,&p));assert(!strcmp(out,"你好，小言"));
    strcpy(s.task_id,"other");assert(agent_qwen_receive(&q,message,&p)==AGENT_ERR_PROTOCOL);
    strcpy(s.task_id,"one");p.length=2;assert(agent_qwen_receive(&q,"{x",&p)==AGENT_ERR_JSON);
    p=(agent_ws_chunk_t){0,0,true,true};assert(agent_qwen_receive(&q,"",&p)==AGENT_ERR_PROTOCOL);
    memset(scratch,'x',sizeof(scratch));p=(agent_ws_chunk_t){6144,1,true,true};assert(agent_qwen_receive(&q,scratch,&p)==AGENT_ERR_LIMIT);
    atomic_store(&stopped,true);assert(agent_qwen_receive(&q,"",&p)==AGENT_ERR_CANCELLED);atomic_store(&stopped,false);
    /* Odd PCM across frames is reconstructed; odd EOF must fail. */
    mock_t sink={0};agent_qwen_init(&q,&s,NULL);q.deadline=100000;q.started=true;q.ending=true;
    q.sink=(agent_pcm_sink_t){pcm_open,pcm_write,&sink};
    p=(agent_ws_chunk_t){1,2,true,true};assert(!agent_qwen_receive(&q,"\x12",&p));
    assert(!agent_qwen_receive(&q,"\x34",&p));assert(sink.pcm[0]==0x3412);
    assert(!agent_qwen_receive(&q,"\x55",&p));
    const char *done="{\"header\":{\"event\":\"task-finished\",\"task_id\":\"one\"},\"payload\":{}}";
    p=(agent_ws_chunk_t){strlen(done),1,true,true};assert(agent_qwen_receive(&q,done,&p)==AGENT_ERR_PROTOCOL);
}
typedef struct {
    unsigned count,finishes;
    uint64_t samples,chunks,source_ms,feed_ms,wait_ms;
    bool complete;
} upload_t;
static void upload_event(void *ctx,const char *stage,const char *text)
{
    upload_t *u=ctx;
    if(!strcmp(stage,"asr_finish")) {assert(u->count==1);++u->finishes;}
    if(strcmp(stage,"asr_upload_stats"))return;
    assert(++u->count==1 && text && strlen(text)<192);
    cJSON *json=agent_json_parse(text,strlen(text));assert(cJSON_IsObject(json));
    assert(cJSON_GetArraySize(json)==6);
    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(json,"samples"),UINT64_MAX,&u->samples));
    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(json,"chunks"),UINT64_MAX,&u->chunks));
    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(json,"source_ms"),UINT64_MAX,&u->source_ms));
    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(json,"feed_ms"),UINT64_MAX,&u->feed_ms));
    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(json,"wait_ms"),UINT64_MAX,&u->wait_ms));
    const cJSON *complete=cJSON_GetObjectItemCaseSensitive(json,"complete");
    assert(cJSON_IsBool(complete));u->complete=cJSON_IsTrue(complete);cJSON_Delete(json);
}
typedef struct { mock_t *mock;unsigned calls,scenario; } timed_source_t;
static agent_err_t timed_next(void *ctx,int16_t *out,size_t cap,size_t *count,bool *end)
{
    timed_source_t *s=ctx;unsigned step=s->calls++;assert(step<4 && cap>=512);
    const unsigned cost[]={7,11,13,17};ticks+=cost[step];*count=0;*end=false;
    if(step==2 && s->scenario==2)return AGENT_ERR_ARGUMENT;
    if(step==1 || step==2) {
        *count=step==1?512:384;for(size_t i=0;i<*count;++i)out[i]=(int16_t)(step*100+i);
        if(step==2 && s->scenario==1)s->mock->cancel_audio=true;
    } else if(step==3) {
        *end=true;
        if(s->scenario==3)s->mock->wrong_id=true; /* Server failure after successful local upload. */
    }
    return AGENT_OK;
}
static void upload_checks(void)
{
    for(unsigned scenario=0;scenario<5;++scenario) {
        ticks=0;atomic_store(&stopped,false);upload_t stats={0};
        mock_t m={.split=800,.binary_ms=3,.read_ms=5,.block_binary=true,.fail=scenario==4};
        agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&m};
        char scratch[8192],out[128];agent_speech_t speech={.scratch=scratch,.capacity=sizeof(scratch),
            .now_ms=now,.cancelled=&stopped,.event=upload_event,.event_ctx=&stats};
        agent_qwen_t q;agent_qwen_init(&q,&speech,&ws);
        timed_source_t source={.mock=&m,.scenario=scenario};agent_speech_live_t input={timed_next,&source};
        const agent_err_t expected[]={AGENT_OK,AGENT_ERR_CANCELLED,AGENT_ERR_ARGUMENT,AGENT_ERR_PROTOCOL,AGENT_ERR_AUTH};
        assert(agent_qwen_live(&speech,&input,out,sizeof(out))==expected[scenario]);
        if(scenario==4)assert(!stats.count && !source.calls && !m.binary_attempts);
        else {
            assert(stats.count==1 && stats.wait_ms==25 && m.closes==1);
            assert(stats.samples==(scenario==1 || scenario==2?512:896));
            assert(stats.chunks==(scenario==1 || scenario==2?1:2));
            assert(stats.source_ms==(scenario==1 || scenario==2?31:48));
            assert(stats.feed_ms==(scenario==1?20:scenario==2?17:25));
            assert(stats.complete==(scenario==0 || scenario==3));
            assert(stats.finishes==(scenario==0 || scenario==3));
            /* BUSY never writes PCM; the cancelled second feed did write but
             * is intentionally excluded from successful diagnostic samples. */
            assert(m.audio_sent==(scenario==2?1024:1792));
            assert(m.binary_attempts==(scenario==2?2:3));
        }
        atomic_store(&stopped,false);m=(mock_t){.split=800};agent_qwen_init(&q,&speech,&ws);
        unsigned count=stats.count;agent_pcm_sink_t sink={pcm_open,pcm_write,&m};
        assert(!agent_qianwen_tts.speak(&speech,AGENT_QWEN_VOICE,"正常。",&sink));
        assert(stats.count==count); /* ASR upload telemetry never leaks into TTS. */
    }
}
int main(void)
{
    rx_loan_checks();
    for(unsigned shared=0;shared<2;++shared)
    for(size_t split=1;split<=137;split+=17) {
        ticks=0;mock_t m={.split=split,.busy=3,.block_binary=true};agent_ws_ops_t ws={open_ws,send_ws,read_ws,close_ws,&m};
        char scratch[8192],out[128];void *raw=shared?malloc(8192):NULL;assert(!shared || raw);
        agent_speech_t s={.scratch=shared?raw:scratch,.capacity=sizeof(scratch),.now_ms=now,.wait_ms=wait_ms,.cancelled=&stopped};
        agent_qwen_t q;agent_qwen_init(&q,&s,&ws);agent_speech_input_t input={1600,clip_read,NULL};
        assert(!agent_qianwen_asr.transcribe(&s,&input,out,sizeof(out)));assert(!strcmp(out,"请开蓝灯。"));
        assert(m.audio_sent==3200 && m.starts==1 && m.finishes==1 && m.closes==1);
        m=(mock_t){.split=split};agent_qwen_init(&q,&s,&ws);agent_pcm_sink_t sink={pcm_open,pcm_write,&m};
        if(shared)assert(!agent_qwen_use_rx_pcm(&q));
        assert(!agent_qianwen_tts.speak(&s,AGENT_QWEN_VOICE,"你好，小言。",&sink));
        assert(m.pcm_count==300 && m.pcm_opens==1 && m.starts==1 && m.closes==1);
        for(unsigned i=0;i<300;++i)assert(m.pcm[i]==(int16_t)(i*123-17000));
        m=(mock_t){.split=split,.wrong_id=true};agent_qwen_init(&q,&s,&ws);
        assert(agent_qianwen_asr.transcribe(&s,&input,out,sizeof(out))==AGENT_ERR_PROTOCOL);assert(m.starts==1 && m.closes==1);
        m=(mock_t){.split=split,.never_start=true};agent_qwen_init(&q,&s,&ws);
        assert(agent_qianwen_asr.transcribe(&s,&input,out,sizeof(out))==AGENT_ERR_TIMEOUT);assert(m.starts==1 && m.closes==1);
        m=(mock_t){.split=split,.fail=true};agent_qwen_init(&q,&s,&ws);
        assert(agent_qianwen_asr.transcribe(&s,&input,out,sizeof(out))==AGENT_ERR_AUTH);assert(m.opens==1 && !m.starts);
        m=(mock_t){.split=split,.cancel_audio=true};agent_qwen_init(&q,&s,&ws);
        assert(agent_qianwen_asr.transcribe(&s,&input,out,sizeof(out))==AGENT_ERR_CANCELLED);
        assert(m.audio_sent==2048 && m.starts==1 && !m.finishes && m.closes==1 && !out[0]);
        atomic_store(&stopped,false);
        atomic_store(&stopped,true);m=(mock_t){.split=split};agent_qwen_init(&q,&s,&ws);
        if(shared)assert(!agent_qwen_use_rx_pcm(&q));
        assert(agent_qianwen_tts.speak(&s,AGENT_QWEN_VOICE,"test",&sink)==AGENT_ERR_CANCELLED);assert(!m.opens);atomic_store(&stopped,false);
        free(raw);
    }
    ticks=0;event_checks();sentence_checks();upload_checks();puts("qianwen streaming: fragments, UTF-8, sentence timing/order, upload timing, finals, PCM, deadlines, cancellation, no replay OK");return 0;
}
