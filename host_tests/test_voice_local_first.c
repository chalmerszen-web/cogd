/* Reuse the deterministic ownership/transport/speaker stubs; do not run its
 * baseline main under the different nomination policy. */
#define main candidate_baseline_main
#include "test_voice_candidate.c"
#undef main

static void predicted_topic(void)
{
    const char *partial[]={"请记住，我给这盏灯取","唔該記低，呢盞燈個名叫小"};
    const char *final[]={"请记住，我给这盏灯取名叫小星星。","唔該記低，呢盞燈個名叫小言。"};
    const char *answer[]={"嗯，灯取名，我想想。","嗯，呢盞燈叫小言，我諗諗先。"};
    for(unsigned language=0;language<2;++language)for(unsigned timing=0;timing<3;++timing) {
        init();strcpy(reply,answer[language]);
        char *settled=malloc(strlen(final[language])+1);assert(settled);strcpy(settled,final[language]);
        if(timing) {response_delay=70;tail_delay=650;tail_chunks=8;}
        begin(partial[language]);
        if(!timing)worker_stopped();
        assert(!streams && !persists && !local_calls && !esp_agent_voice_candidate_ack()->pending);
        bool handled=false;uint64_t began=esp_agent_now();
        if(timing==2) {
            assert(!esp_agent_voice_candidate_stage(&engine,settled,AGENT_OK,
                capture_arena+CAPTURE_BYTES-8192,&handled));
            assert(!handled && esp_agent_voice_candidate_deferred() && !engine.workspace);
            assert(atomic_load(&worker_live) && streams==1 && !seals && !persists && !local_calls);
            /* The final input lives outside the captured workspace and remains
             * unchanged until joined resume. No ASR callback can revise it. */
        }
        assert(!finish(&engine,settled,AGENT_OK,&handled));
        assert(!handled && streams==1 && seals==1 && creates==1 && closes==1 && !persists && !local_calls);
        assert(!atomic_load(&worker_live) && !atomic_load(&cache_count) && !esp_agent_voice_candidate_deferred());
        assert(first_write_at>=began && first_write_at-began<500);
        assert(esp_agent_voice_candidate_ack()->pending && spool_cancel==&core.cancelled);
        assert(!strcmp(esp_agent_voice_candidate_ack()->text,answer[language]));
        memset(settled,0xa6,strlen(settled));free(settled); /* ASan: playback cannot retain this pointer. */
        assert(!esp_agent_voice_candidate_ack_join(AGENT_OK) && finishes==1);
        assert(!esp_agent_voice_candidate_ack()->pending);preserved();
    }
    const char *changed[]={"请记住，我给这盏灯取下来收好。","请记住，我给另一盏灯取名。",
        "请记住，我给这盏灯取名，取消。","请记住，我给这盏灯取名，不对，改成",
        "请记住，我给这盏灯取名，用粤语回答。"};
    for(unsigned i=0;i<sizeof(changed)/sizeof(*changed);++i)for(unsigned timing=0;timing<2;++timing) {
        init();strcpy(reply,answer[0]);
        if(timing) {response_delay=70;tail_delay=50;tail_chunks=8;}
        begin(partial[0]);
        if(!timing) {worker_stopped();assert(atomic_load(&cache_count));}
        assert(!streams && !persists && !local_calls);
        bool handled=false;assert(!finish(&engine,changed[i],AGENT_OK,&handled));
        assert(!handled && !streams && !persists && !local_calls && !esp_agent_voice_candidate_ack()->pending);
        assert(!atomic_load(&worker_live) && !atomic_load(&cache_count) && !atomic_load(&opened));
        assert(opens==closes && opens<=1);preserved(); /* Cancellation can precede open. */
    }
    for(unsigned error=0;error<2;++error) {
        init();strcpy(reply,answer[0]);begin(partial[0]);worker_stopped();
        if(!error)atomic_store(&core.cancelled,true);
        bool handled=false;assert(finish(&engine,final[0],error?AGENT_ERR_TIMEOUT:AGENT_OK,&handled)==
            (error?AGENT_ERR_TIMEOUT:AGENT_ERR_CANCELLED));
        assert(!handled && !streams && !persists && !local_calls && !atomic_load(&cache_count));preserved();
    }
    /* A late response tool or changed transcript aborts the already admitted
     * turn; it cannot replay the request or record a completed answer. */
    const unsigned late_failure[]={19,8}; /* Tool item; changed transcript. */
    for(unsigned i=0;i<sizeof(late_failure)/sizeof(*late_failure);++i) {
        init();fault=late_failure[i];strcpy(reply,answer[0]);tail_delay=30;begin(partial[0]);
        bool handled=false;assert(finish(&engine,final[0],AGENT_OK,&handled)==AGENT_ERR_PROTOCOL);
        assert(handled && streams==1 && finishes==1 && !seals && !assistant_turns && !local_calls);
        assert(!atomic_load(&worker_live) && !atomic_load(&cache_count));preserved();
    }
    puts("Predicted topics: silent prefinal cache, correct/wrong completion, active/completed/deferred joins, no escaped final pointer, cancel and late protocol failures OK");
}

int main(void)
{
    bool handled;
    const char *finals[]={"请把灯设为蓝色。","请把灯调成蓝色，不对，不要蓝色，改成绿色。",
                         "唔該將燈改做藍色，唔係，唔好藍色，改做綠色。"};
    for(unsigned i=0;i<sizeof(finals)/sizeof(*finals);++i) {
        init();begin("请把灯");vTaskDelay(20);
        esp_agent_voice_candidate_revision(2);esp_agent_voice_candidate_preview(finals[i]);vTaskDelay(20);
        assert(!creates && !atomic_load(&cache_count) && !local_calls && !streams);
        assert(!finish(&engine,finals[i],AGENT_OK,&handled));
        assert(handled && local_calls==1 && !creates && !streams && !persists);
        assert(!atomic_load(&worker_live) && !atomic_load(&opened));preserved();
    }
    init();begin("请把灯");vTaskDelay(20);
    assert(!finish(&engine,"请把灯调成。",AGENT_OK,&handled));
    assert(!handled && engine.clarify_only && !creates && !local_calls && !streams);preserved();
    init();begin("请把灯");vTaskDelay(20);
    atomic_store(&core.cancelled,true);
    assert(finish(&engine,finals[0],AGENT_OK,&handled)==AGENT_ERR_CANCELLED);
    assert(!local_calls && !creates && !streams && !atomic_load(&cache_count));preserved();
    init();begin("请把灯");vTaskDelay(20);
    assert(finish(&engine,finals[0],AGENT_ERR_TIMEOUT,&handled)==AGENT_ERR_TIMEOUT);
    assert(!handled && !local_calls && !creates && !streams);preserved();
    /* Unknown richer lamp instructions still prepare grounded process speech;
     * neither preview nor final acceptance executes a tool in this adapter. */
    init();strcpy(reply,"嗯，显示时间，我想想。");begin("请把灯调成蓝色，再显示时间。");
    worker_stopped();assert(creates==1 && !local_calls && !streams);
    assert(!finish(&engine,"请把灯调成蓝色，再显示时间。",AGENT_OK,&handled));
    assert(!handled && streams==1 && seals==1 && !local_calls);preserved();
    assert(esp_agent_voice_candidate_ack()->pending);
    assert(!esp_agent_voice_candidate_ack_join(AGENT_OK));
    assert(!esp_agent_voice_candidate_ack()->pending && finishes==1);
    /* Shared fast answers, source-grounded receipts and streaming ownership. */
    short_answers();contextual_receipt();predicted_topic();free(capture_arena);capture_arena=NULL;
    puts("Local-first adapter: no local response/cache, complete-final tool, cancellation and complex receipt preserved");
}
