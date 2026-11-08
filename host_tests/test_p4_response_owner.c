#include "p4_response_owner.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Cold restored photo history must not own the new spoken story. A late
     * history ACK, a duplicate ACK, or an ACK for a superseded audio turn
     * cannot trigger inference for this binding. */
    p4_input_binding_t binding={0};
    assert(p4_input_binding_begin(&binding,"live-story","audio-story",7,100));
    assert(!p4_input_binding_ack(&binding,"old-photo","audio-story",7));
    assert(!p4_input_binding_ack(&binding,"live-story","audio-next",7));
    assert(!p4_input_binding_ack(&binding,"live-story","audio-story",8));
    assert(binding.pending && !p4_input_binding_timeout(&binding,3099));
    assert(p4_input_binding_timeout(&binding,3100));
    assert(!p4_input_binding_begin(&binding,"next","audio-next",8,200));
    assert(p4_input_binding_ack(&binding,"live-story","audio-story",7));
    assert(!p4_input_binding_ack(&binding,"live-story","audio-story",7));
    assert(!p4_input_binding_timeout(&binding,9000));
    assert(!p4_input_binding_begin(&binding,"","audio-next",8,200));
    assert(p4_input_binding_begin(&binding,"next","audio-next",8,200));
    /* Captured refine result: two committed but empty microphone turns
     * arrived while the job ran. Its result still belongs to the last real
     * request; a later nonempty request really does supersede that identity. */
    p4_input_identity_t input={0};
    assert(p4_input_identity_accept(&input,"select-third","请选择第三道题"));
    assert(!p4_input_identity_accept(&input,"noise-1",""));
    assert(!p4_input_identity_accept(&input,"noise-2"," \r\n\t"));
    assert(p4_input_identity_matches(&input,"select-third"));
    assert(!p4_input_identity_matches(&input,"noise-2"));
    assert(!p4_input_identity_accept(&input,"","有效文字但没有条目"));
    assert(p4_input_identity_matches(&input,"select-third"));
    assert(p4_input_identity_accept(&input,"new-photo","重新拍照"));
    assert(!p4_input_identity_matches(&input,"select-third"));
    assert(p4_input_identity_matches(&input,"new-photo"));
    p4_response_owner_t s={0};
    /* Captured tutor read at the 120 s renewal boundary: the speaker is idle
     * between output ACK and the 250 ms fallback, but the answer is owed. */
    assert(p4_response_settled(&s));
    unsigned renewal_ticket=p4_response_begin(&s,false);
    p4_response_ack(&s,renewal_ticket,120000);
    assert(!p4_response_manual_due(&s,120249));
    assert(!p4_response_settled(&s));
    assert(p4_response_manual_due(&s,120250));
    p4_response_manual(&s);assert(!p4_response_settled(&s));
    assert(p4_response_created(&s));assert(p4_response_settled(&s));
    /* A field hidden from the voice schema is also rejected at execution,
     * regardless of its value (URL, empty string or null). Ordinary tutor
     * fields and unrelated tools retain their existing contracts. */
    assert(!p4_voice_tool_field_allowed("device_tutor","test_image_url"));
    assert(p4_voice_tool_field_allowed("device_tutor","action"));
    assert(p4_voice_tool_field_allowed("device_tutor","question_id"));
    assert(p4_voice_tool_field_allowed("device_tutor","revision"));
    assert(p4_voice_tool_field_allowed("device_camera_test","test_image_url"));
    assert(p4_voice_tool_field_allowed("device_display_image","url"));
    /* Either provider event ordering permits exactly one audible response. */
    for(unsigned auto_first=0;auto_first<2;++auto_first) {
        unsigned ticket=p4_response_begin(&s,false);
        if(auto_first)assert(p4_response_created(&s));
        p4_response_ack(&s,ticket,1000);
        if(!auto_first)assert(p4_response_created(&s));
        assert(!p4_response_manual_due(&s,5000));
        assert(!p4_response_created(&s));
    }
    /* An absent automatic continuation gets one bounded manual fallback. */
    unsigned ticket=p4_response_begin(&s,false);p4_response_ack(&s,ticket,1000);
    assert(!p4_response_manual_due(&s,1249));
    assert(p4_response_manual_due(&s,1250));
    ticket=p4_response_manual(&s);
    assert(p4_response_send(&s,ticket));
    assert(!p4_response_manual_due(&s,5000));
    assert(p4_response_created(&s));
    assert(!p4_response_send(&s,ticket));
    assert(!p4_response_created(&s));
    /* New input wins over a queued old response; it does not cancel a job. */
    ticket=p4_response_begin(&s,false);p4_response_ack(&s,ticket,6000);
    ticket=p4_response_manual(&s);p4_response_obsolete(&s);
    assert(!p4_response_send(&s,ticket));
    p4_response_begin(&s,false);
    assert(!p4_response_send(&s,ticket));
    p4_response_ack(&s,ticket,6500); /* old item acknowledgement after new input */
    assert(!p4_response_manual_due(&s,9000));
    assert(p4_response_created(&s));
    /* Accepted is protocol data, not another acknowledgement to speak. */
    ticket=p4_response_begin(&s,true);
    assert(p4_response_silent_wait(&s,7000));
    p4_response_ack(&s,ticket,7000);
    assert(p4_response_silent_wait(&s,7249));
    assert(!p4_response_silent_wait(&s,7250));
    assert(!p4_response_manual_due(&s,9000));
    assert(!p4_response_created(&s));
    assert(!p4_response_created(&s));
    /* A solving choice receipt stays silent; a later real user and the solve
     * completion still own separate audible responses. Completed explain,
     * status, unrelated tools and malformed/stale choices must not be muted. */
    assert(p4_tutor_pending_choice("device_tutor","read","running","solving","reading",true));
    bool pending=p4_tutor_pending_choice("device_tutor","explain","running","solving","explaining",true);
    assert(pending);
    ticket=p4_response_begin(&s,pending);p4_response_ack(&s,ticket,9100);
    assert(!p4_response_manual_due(&s,10000));assert(!p4_response_created(&s));
    p4_response_user(&s);assert(p4_response_created(&s));
    p4_response_notice(&s);assert(p4_response_created(&s));
    assert(!p4_tutor_pending_choice("device_tutor","explain","succeeded","complete","explaining",true));
    assert(!p4_tutor_pending_choice("device_tutor","explain","failed","solving","explaining",true));
    assert(!p4_tutor_pending_choice("device_tutor","status","running","solving","explaining",true));
    assert(!p4_tutor_pending_choice("device_tutor","confirm","running","solving","explaining",true));
    assert(!p4_tutor_pending_choice("other_tool","explain","running","solving","explaining",true));
    assert(!p4_tutor_pending_choice("device_tutor","explain","running","solving","",true));
    assert(!p4_tutor_pending_choice("device_tutor","explain","running","solving","reading",true));
    assert(!p4_tutor_pending_choice("device_tutor","explain","running","solving","explaining",false));
    assert(!p4_tutor_pending_choice("device_tutor","explain","running","solving",NULL,true));
    /* The reproduced refine -> result -> automatic status/refine sequence
     * must not authorize either tool. Only a subsequent human turn may. */
    p4_response_user(&s);
    assert(p4_response_tools(&s));
    ticket=p4_response_notice(&s);
    p4_response_ack(&s,ticket,10000);
    assert(p4_response_created(&s));
    unsigned effects=0;
    for(unsigned status_then_refine=0;status_then_refine<2;++status_then_refine)
        if(p4_response_tools(&s))++effects;
    assert(effects==0);
    ticket=p4_response_begin(&s,false); /* even a tool rejection receipt */
    p4_response_ack(&s,ticket,11000);
    assert(p4_response_manual_due(&s,11250));
    p4_response_manual(&s);
    assert(p4_response_created(&s));
    assert(!p4_response_tools(&s));
    p4_response_obsolete(&s);
    assert(!p4_response_tools(&s));
    p4_response_user(&s);
    assert(p4_response_tools(&s));
    /* Consuming at dispatch also covers interruption/no-audio completion. */
    bool consumed=false;
    assert(p4_notice_claim(&consumed,true));
    assert(!p4_notice_claim(&consumed,true));
    consumed=false;
    assert(!p4_notice_claim(&consumed,false));
    assert(!p4_notice_claim(&consumed,true));
    /* Trial: explain(step 1) exceeds the receipt deadline. Its action already
     * consumed completion_pending; accepted must still lead to one reply. */
    p4_response_user(&s);
    ticket=p4_response_begin(&s,true);p4_response_ack(&s,ticket,12000);
    assert(!p4_response_created(&s));
    assert(p4_tutor_notice_kind("explain",true,true,"complete",false,false,false)==P4_TUTOR_NOTICE_RESULT);
    consumed=false;assert(p4_notice_claim(&consumed,true));
    ticket=p4_response_notice(&s);p4_response_ack(&s,ticket,13000);
    assert(p4_response_created(&s));
    assert(!p4_response_tools(&s));
    assert(!p4_notice_claim(&consumed,true));
    const char *actions[]={"read","explain","resume","status","context","done","scroll"};
    for(unsigned i=0;i<sizeof(actions)/sizeof(actions[0]);++i) {
        assert(p4_tutor_notice_kind(actions[i],true,true,"complete",false,false,true)==P4_TUTOR_NOTICE_RESULT);
        assert(p4_tutor_notice_kind(actions[i],false,true,"complete",false,false,false)==P4_TUTOR_NOTICE_SKIP);
        assert(p4_tutor_notice_kind(actions[i],true,false,"complete",false,false,false)==P4_TUTOR_NOTICE_SKIP);
    }
    assert(p4_tutor_notice_kind("confirm",true,true,"complete",false,false,false)==P4_TUTOR_NOTICE_SKIP);
    assert(p4_tutor_notice_kind("recheck",true,true,"complete",false,false,false)==P4_TUTOR_NOTICE_SKIP);
    assert(p4_tutor_notice_kind("confirm",false,true,"complete",false,true,false)==P4_TUTOR_NOTICE_COMPLETION);
    assert(p4_tutor_notice_kind("recheck",true,true,"complete",false,true,false)==P4_TUTOR_NOTICE_COMPLETION);
    assert(p4_tutor_notice_kind("confirm",true,true,"complete",false,true,true)==P4_TUTOR_NOTICE_SKIP);
    assert(p4_tutor_notice_kind("refine",true,true,"awaiting_confirmation",false,false,false)==P4_TUTOR_NOTICE_RESULT);
    /* Selected-question notices cannot inherit an old solution, even when
     * that solution is present beside the current summary in a snapshot. */
    const struct {const char *summary,*excerpt,*overview;} snapshot={
        "CURRENT_QUESTION_ONLY", "OLD_SOLUTION_EXCERPT", "OLD_SOLUTION_OVERVIEW"};
    char phase_message[2048];
    p4_tutor_stage_t stage=p4_tutor_stage("refine","awaiting_confirmation",true,true,true);
    assert(stage==P4_TUTOR_STAGE_CONFIRM);
    assert(p4_tutor_stage_message(stage,snapshot.summary,phase_message,sizeof(phase_message)));
    assert(strstr(phase_message,snapshot.summary) && strstr(phase_message,"是这道题吗？"));
    assert(!strstr(phase_message,snapshot.excerpt) && !strstr(phase_message,snapshot.overview));
    stage=p4_tutor_stage("start","needs_selection",true,true,true);
    assert(stage==P4_TUTOR_STAGE_SELECT);
    assert(p4_tutor_stage_message(stage,"CURRENT_CANDIDATE_A; CURRENT_CANDIDATE_B",phase_message,sizeof(phase_message)));
    assert(strstr(phase_message,"CURRENT_CANDIDATE_A") && strstr(phase_message,"CURRENT_CANDIDATE_B"));
    assert(strstr(phase_message,"请告诉我题号") && !strstr(phase_message,"是这道题吗？"));
    assert(!strstr(phase_message,snapshot.excerpt) && !strstr(phase_message,snapshot.overview));
    assert(p4_tutor_stage("explain","complete",true,true,true)==P4_TUTOR_STAGE_NONE);
    assert(p4_tutor_stage("status","awaiting_confirmation",true,true,true)==P4_TUTOR_STAGE_NONE);
    assert(p4_tutor_stage("refine","awaiting_confirmation",false,true,true)==P4_TUTOR_STAGE_NONE);
    assert(p4_tutor_stage("refine","awaiting_confirmation",true,false,true)==P4_TUTOR_STAGE_NONE);
    assert(p4_tutor_stage("refine","awaiting_confirmation",true,true,false)==P4_TUTOR_STAGE_NONE);
    assert(p4_tutor_stage("confirm","solving",true,true,true)==P4_TUTOR_STAGE_NONE);
    assert(!p4_tutor_stage_message(P4_TUTOR_STAGE_CONFIRM,"",phase_message,sizeof(phase_message)) && !*phase_message);
    assert(!p4_tutor_stage_message(P4_TUTOR_STAGE_CONFIRM,snapshot.summary,phase_message,8) && !*phase_message);
    /* A later "explain first" must not swallow this question's solve failure.
     * Cancellation, supersession and a different question remain silent. */
    const char *solves[]={"confirm","recheck"};
    const char *failures[]={"failed","incomplete"};
    for(unsigned i=0;i<2;++i) {
        for(unsigned f=0;f<2;++f) {
            assert(p4_tutor_notice_kind(solves[i],false,true,failures[f],true,false,false)==P4_TUTOR_NOTICE_FAILURE);
            assert(p4_tutor_notice_kind(solves[i],true,true,failures[f],true,false,false)==P4_TUTOR_NOTICE_FAILURE);
            assert(p4_tutor_notice_kind(solves[i],false,false,failures[f],true,false,false)==P4_TUTOR_NOTICE_SKIP);
        }
        assert(p4_tutor_notice_kind(solves[i],true,true,"cancelled",true,false,false)==P4_TUTOR_NOTICE_SKIP);
        assert(p4_tutor_notice_kind(solves[i],false,true,"superseded",true,false,false)==P4_TUTOR_NOTICE_SKIP);
        assert(p4_tutor_notice_kind(solves[i],false,true,"solving",false,false,false)==P4_TUTOR_NOTICE_SKIP);
    }
    assert(p4_tutor_notice_kind("recheck",false,true,"complete",true,false,false)==P4_TUTOR_NOTICE_FAILURE);
    assert(p4_tutor_notice_kind("recheck",false,false,"complete",true,false,false)==P4_TUTOR_NOTICE_SKIP);
    consumed=false;assert(p4_notice_claim(&consumed,true));
    ticket=p4_response_notice(&s);p4_response_ack(&s,ticket,14000);
    assert(p4_response_created(&s));
    assert(!p4_response_tools(&s)); /* failure cannot authorize an automatic retry */
    assert(!p4_notice_claim(&consumed,true));
    /* User fallback needs transcription, speech-stop and the tool-restoring
     * session acknowledgement. Its grace is 1 s, ordinary receipts remain 250 ms. */
    unsigned user=p4_response_user(&s);
    p4_response_user_ready(&s,user,false,true,true,15000);
    assert(!p4_response_user_due(&s,user,17000));
    p4_response_user_ready(&s,user,true,false,true,17000);
    assert(!p4_response_user_due(&s,user,19000));
    p4_response_user_ready(&s,user,true,true,false,19000);
    assert(!p4_response_user_due(&s,user,21000));
    p4_response_user_ready(&s,user,true,true,true,21000);
    assert(!p4_response_user_due(&s,user,21999));
    assert(p4_response_user_due(&s,user,22000));
    unsigned manual_count=0;
    for(unsigned now=22000;now<=24000;now+=100) {
        if(p4_response_user_due(&s,user,now)){++manual_count;ticket=p4_response_manual(&s);}
        if(now==22500){assert(p4_response_send(&s,ticket));assert(p4_response_created(&s));}
    }
    assert(manual_count==1 && !p4_response_send(&s,ticket));
    assert(!p4_response_created(&s));
    user=p4_response_user(&s);p4_response_user_ready(&s,user,true,true,true,24000);
    assert(p4_response_user_due(&s,user,25000));ticket=p4_response_manual(&s);
    assert(p4_response_created(&s)); /* automatic response before queued create is sent */
    assert(!p4_response_send(&s,ticket) && !p4_response_user_due(&s,user,27000));
    /* An automatic response before ASR consumes the user ticket; a later
     * receipt must not be acknowledged by that old user's delayed transcript. */
    user=p4_response_user(&s);assert(p4_response_created(&s));
    p4_response_user_ready(&s,user,true,true,true,25000);
    assert(!p4_response_user_due(&s,user,27000));
    ticket=p4_response_begin(&s,false);
    p4_response_user_ready(&s,user,true,true,true,27000);
    assert(!p4_response_manual_due(&s,29000));
    assert(!p4_response_user_pending(&s,ticket));
    /* Automatic response after ASR but before the grace expires also wins. */
    user=p4_response_user(&s);p4_response_user_ready(&s,user,true,true,true,30000);
    assert(!p4_response_user_due(&s,user,30500));assert(p4_response_created(&s));
    assert(!p4_response_user_due(&s,user,32000));
    /* A new human turn rejects both old transcription and an old queued create. */
    unsigned old_user=p4_response_user(&s);
    p4_response_user_ready(&s,old_user,true,true,true,33000);
    assert(p4_response_user_due(&s,old_user,34000));ticket=p4_response_manual(&s);
    user=p4_response_user(&s);
    p4_response_user_ready(&s,old_user,true,true,true,34000);
    assert(!p4_response_user_due(&s,user,36000) && !p4_response_send(&s,ticket));
    p4_response_user_ready(&s,user,true,true,true,36000);
    assert(p4_response_user_due(&s,user,37000));
    /* A completed notification disappears from provider input history while
     * the real user and spoken assistant IDs can never satisfy its deletion. */
    p4_notice_item_t temporary={0};
    assert(p4_notice_item_begin(&temporary,"app_1_1",41));
    assert(!p4_notice_item_begin(&temporary,"app_1_2",42));
    p4_notice_item_created(&temporary,"real_user");
    p4_notice_item_response(&temporary,42,"real_response");
    p4_notice_item_done(&temporary,"real_response");
    assert(!p4_notice_item_deleting(&temporary));
    p4_notice_item_created(&temporary,"app_1_1");
    p4_notice_item_response(&temporary,41,"notice_response");
    p4_notice_item_done(&temporary,"notice_response");
    assert(!p4_notice_item_delete_due(&temporary,true));
    assert(p4_notice_item_delete_due(&temporary,false));
    p4_notice_item_delete_sent(&temporary,38000);
    assert(!p4_notice_item_delete_due(&temporary,false));
    assert(!p4_notice_item_deleted(&temporary,"real_user"));
    assert(!p4_notice_item_deleted(&temporary,"spoken_assistant"));
    assert(p4_notice_item_deleted(&temporary,"app_1_1"));
    assert(!p4_notice_item_deleting(&temporary));
    /* New speech may supersede a notice before creation ACK. Do not delete
     * before ACK, while generation is active, or twice after a late ACK. */
    assert(p4_notice_item_begin(&temporary,"app_1_2",43));
    p4_notice_item_retire(&temporary);
    assert(!p4_notice_item_delete_due(&temporary,false));
    p4_notice_item_created(&temporary,"app_1_1");
    assert(!p4_notice_item_delete_due(&temporary,false));
    p4_notice_item_created(&temporary,"app_1_2");
    assert(!p4_notice_item_delete_due(&temporary,true));
    assert(p4_notice_item_delete_due(&temporary,false));
    p4_notice_item_delete_sent(&temporary,39000);
    assert(!p4_notice_item_deleted(&temporary,"interrupted_assistant_repair"));
    assert(p4_notice_item_deleting(&temporary));
    assert(p4_notice_item_deleted(&temporary,"app_1_2"));
    assert(!p4_notice_item_deleted(&temporary,"app_1_2"));
    /* Automatic notice response can finish before the item's creation ACK. */
    assert(p4_notice_item_begin(&temporary,"app_1_3",44));
    p4_notice_item_response(&temporary,44,"notice_early");
    p4_notice_item_done(&temporary,"notice_early");
    assert(!p4_notice_item_delete_due(&temporary,false));
    p4_notice_item_created(&temporary,"app_1_3");
    assert(p4_notice_item_delete_due(&temporary,false));
    puts("P4 response ownership: auto/manual ordering, stale receipts, silent acceptance, once-only notice and notice tool isolation PASS");
    return 0;
}
