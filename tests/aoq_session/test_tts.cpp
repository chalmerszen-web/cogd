#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_tts_trial.cpp"

int64_t test_now=1000;
enum class Scenario{Pass,FinOnly,WrongTask,NoPcm,Failed,Overflow,Multiple,CursorJump,CursorReset,ObjectGap,ObjectRepeat,Cancelled,EndTrack,Busy,OtherStreamGroup,OtherStreamTrack,EarlyEnd,GoAway,Buffered,NoFinish};
static Scenario scenario=Scenario::Pass;
static unsigned opens=0,accepted_sends=0,cases=0;
static const char* expected_phrase="原生语音测试成功。";
namespace aoq {
struct Session::Impl {
 Options options;Callbacks callbacks;Diagnostics diag;
 std::string task;uint64_t pcm_index=0;unsigned actions=0;bool started=false,delivered=false;bool refused[3]{};
 int event(const char* name,bool other=false){
  std::string json="{\"header\":{\"event\":\""+std::string(name)+"\",\"task_id\":\""+(other?"unrelated-task":task)+"\",\"error_message\":\"PRIVATE_PROVIDER_MESSAGE_DO_NOT_EXPOSE\"}}";
  Object o;o.payload={reinterpret_cast<const uint8_t*>(json.data()),json.size()};
  int e=callbacks.data(callbacks.userdata,o);if(e)diag.error=e;return e;
 }
 int pcm(uint64_t group,uint64_t stream,uint64_t cursor,size_t size,uint64_t explicit_object=UINT64_MAX){
  std::vector<uint8_t> properties,payload(size,0x42);aoq_wire::AudioProperties p;p.timestamp_us=1000000+pcm_index*20000;p.sample_cursor=cursor;p.duration_samples=size/2;
  assert(aoq_wire::encode_pcm_properties(p,properties));Object o;o.stream=stream;o.group=group;o.id=explicit_object==UINT64_MAX?pcm_index:explicit_object;++pcm_index;
  o.properties={properties.data(),properties.size()};o.payload={payload.data(),payload.size()};
  int e=callbacks.audio(callbacks.userdata,o);if(e)diag.error=e;return e;
 }
};
Session::Session()=default;Session::~Session()=default;
int Session::open(p4_aoq_kind_t kind,const Options& o,const Callbacks& c){
 assert(kind==P4_AOQ_TTS);++opens;impl_.reset(new Impl);impl_->options=o;impl_->callbacks=c;
 impl_->diag.closed=false;impl_->diag.setup_accepted=true;impl_->diag.pcm24k_selected=true;impl_->diag.stage="mock_setup_accepted";
 impl_->diag.receive_drained=scenario!=Scenario::Buffered;impl_->diag.rtt_ms=50;
 if(scenario==Scenario::GoAway){
  auto& d=impl_->diag;d.pcm24k_selected=false;d.error=7;d.stage="server_goaway";d.closed=true;d.goaway_received=true;
  d.last_control_type=16;d.last_control_length=109;d.goaway_reason_length=106;d.notifications=1;
  std::strcpy(d.goaway_reason,"synthetic refusal [redacted]");std::strcpy(d.last_notify_tag,"status");return 7;
 }
 return 0;
}
int Session::send_json(const char* data,size_t size){
 auto& p=*impl_;assert(p.actions<3);assert(p.actions==0||p.started);
 if(scenario==Scenario::Busy&&!p.refused[p.actions]){p.refused[p.actions]=true;return 3;}
 auto* json=cJSON_ParseWithLength(data,size);assert(json);auto* h=cJSON_GetObjectItemCaseSensitive(json,"header");
 const char* action=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(h,"action"));
 const char* task=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(h,"task_id"));assert(task&&std::strlen(task)==36);
 static const char* expected[]={"run-task","continue-task","finish-task"};assert(action&&!std::strcmp(action,expected[p.actions]));
 if(p.actions==0)p.task=task;else assert(p.task==task);
 if(p.actions==1){auto* text=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json,"payload"),"input"),"text");assert(!std::strcmp(cJSON_GetStringValue(text),expected_phrase));}
 cJSON_Delete(json);++p.actions;++p.diag.sent_objects;++accepted_sends;return 0;
}
int Session::poll(unsigned ms){
 auto& p=*impl_;test_now+=ms;
 if(p.options.cancelled&&p.options.cancelled(p.options.cancel_userdata)){p.diag.error=4;return 4;}
 if(p.actions==1&&!p.started){
  if(scenario==Scenario::Cancelled){p4_aoq_tts_trial_cancel();return 0;}
  if(scenario==Scenario::Failed)return p.event("task-failed");
  if(scenario==Scenario::WrongTask){test_now+=5000;return p.event("task-started",true);}
  p.started=true;return p.event("task-started");
 }
 if(p.actions==3&&!p.delivered){
  p.delivered=true;
  if(scenario==Scenario::EarlyEnd){End early;early.alias=202;early.group=9;early.stream=3;early.end_group=true;assert(p.callbacks.end(p.callbacks.userdata,early)==0);}
  if(scenario==Scenario::Overflow){for(unsigned i=0;i<9;++i){int e=p.pcm(9,3,i*32768,65536);if(e)return e;}}
  else if(scenario!=Scenario::NoPcm){int e=p.pcm(9,3,0,4);if(e)return e;}
  if(scenario==Scenario::Multiple){int e=p.pcm(10,7,0,4);if(e)return e;}
  if(scenario==Scenario::CursorJump){int e=p.pcm(9,3,99,4);if(e)return e;}
  if(scenario==Scenario::CursorReset){int e=p.pcm(9,3,0,4);if(e)return e;}
  if(scenario==Scenario::ObjectGap){int e=p.pcm(9,3,2,4,2);if(e)return e;}
  if(scenario==Scenario::ObjectRepeat){int e=p.pcm(9,3,2,4,0);if(e)return e;}
  End terminal;terminal.alias=202;terminal.group=9;terminal.stream=3;terminal.stream_fin=true;
  terminal.end_group=scenario!=Scenario::FinOnly&&scenario!=Scenario::EndTrack&&scenario!=Scenario::OtherStreamTrack&&scenario!=Scenario::Buffered&&scenario!=Scenario::NoFinish;
  terminal.end_track=scenario==Scenario::EndTrack||scenario==Scenario::OtherStreamTrack;
  if(scenario==Scenario::OtherStreamGroup||scenario==Scenario::OtherStreamTrack)terminal.stream=7;
  if(scenario!=Scenario::EarlyEnd){int e=p.callbacks.end(p.callbacks.userdata,terminal);if(e){p.diag.error=e;return e;}}
  return scenario==Scenario::NoFinish?0:p.event("task-finished");
 }
 if(p.delivered)test_now+=5000;
 return 0;
}
void Session::close(){if(impl_)impl_->diag.closed=true;}
void Session::cancel(){p4_aoq_tts_trial_cancel();}
Diagnostics Session::diagnostics()const{return impl_?impl_->diag:Diagnostics{};}
}
static uint64_t report_generation(const std::string& json){auto at=json.find("\"generation\":");assert(at!=json.npos);return std::stoull(json.substr(at+13));}
static std::string run(Scenario next,int expected){
 scenario=next;char report[2048];const auto previous=opens;int e=p4_aoq_tts_trial_command("tts probe",report,sizeof(report));
 assert(e==expected&&opens==previous+1);auto* json=cJSON_Parse(report);assert(json);
 assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"pass"))==(expected==0));
 assert(!std::strstr(report,"PRIVATE_PROVIDER_MESSAGE")&&!std::strstr(report,"task_id"));
 cJSON_Delete(json);++cases;return report;
}
int main(){
 {scenario=Scenario::Pass;expected_phrase="请直接回答，一加一等于多少？";char report[2048];
  assert(!p4_aoq_tts_trial_command("tts question",report,sizeof(report)));expected_phrase="原生语音测试成功。";++cases;}
 auto success=run(Scenario::Pass,0);const auto first_generation=report_generation(success);uint8_t out[8]{};size_t size=999;
 assert(p4_aoq_tts_trial_copy_pcm(first_generation,out,sizeof(out),&size)==0&&size==4&&out[0]==0x42);++cases;
 size=999;assert(p4_aoq_tts_trial_copy_pcm(first_generation,out,3,&size)==5&&size==0);++cases;
 size=999;assert(p4_aoq_tts_trial_copy_pcm(first_generation+1,out,sizeof(out),&size)==23&&size==0);++cases;
 auto drained=run(Scenario::FinOnly,0);assert(drained.find("\"audio_end_group\":false")!=drained.npos&&drained.find("\"model_receive_drained\":true")!=drained.npos);
 size=999;assert(p4_aoq_tts_trial_copy_pcm(first_generation,out,sizeof(out),&size)==23&&size==0);++cases;
 run(Scenario::WrongTask,15);run(Scenario::NoPcm,15);run(Scenario::Failed,14);
 run(Scenario::Overflow,5);auto multiple=run(Scenario::Multiple,7);assert(multiple.find("unsupported_audio_layout")!=multiple.npos&&multiple.find("\"pcm_groups\":2")!=multiple.npos);
 for(auto c:{Scenario::CursorJump,Scenario::CursorReset}){
  auto kept=run(c,0);assert(kept.find("\"internal_cursor_discontinuities\":1")!=kept.npos);
  assert(kept.find("\"pcm_bytes\":8")!=kept.npos);
 }
 for(auto c:{Scenario::ObjectGap,Scenario::ObjectRepeat}){
  auto rejected=run(c,7);assert(rejected.find("pcm_object_gap_or_reorder")!=rejected.npos);
 }
 run(Scenario::Cancelled,4);run(Scenario::EndTrack,0);
 for(auto mismatch:{Scenario::OtherStreamGroup,Scenario::OtherStreamTrack,Scenario::EarlyEnd}){
  const auto report=run(mismatch,0);assert(report.find("\"audio_end_group\":false")!=report.npos&&report.find("\"audio_end_track\":false")!=report.npos&&report.find("\"model_receive_drained\":true")!=report.npos);
 }
 run(Scenario::Buffered,15);run(Scenario::NoFinish,15);
 {
  scenario=Scenario::Pass;Trial t;aoq::Options o;aoq::Callbacks c;assert(t.session.open(P4_AOQ_TTS,o,c)==0);
  t.task_finished=true;t.bytes=4;t.task_finished_at=t.last_pcm_at=test_now;
  t.drain_after_model_end();assert(!t.model_receive_drained&&t.drain_guard_ms==300);
  test_now+=299;t.drain_after_model_end();assert(!t.model_receive_drained);
  // A new audio block moves the anchor; no immediate completion after the
  // earlier task receipt, and no invented group/track terminal.
  t.last_pcm_at=test_now;test_now+=299;t.drain_after_model_end();assert(!t.model_receive_drained);
  test_now+=1;t.drain_after_model_end();assert(t.model_receive_drained&&!t.end_group&&!t.end_track);++cases;
 }
 unsigned before=accepted_sends;auto busy=run(Scenario::Busy,0);assert(accepted_sends==before+3&&busy.find("\"busy_retries\":3")!=busy.npos);
 assert(report_generation(busy)!=first_generation);++cases;
 {
  const auto sends=accepted_sends;const auto rejection=run(Scenario::GoAway,7);assert(sends==accepted_sends);
  auto* json=cJSON_Parse(rejection.c_str());assert(json);
  assert(!std::strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"session_stage")),"server_goaway"));
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"goaway_received")));
  for(const char* key:{"pcm24k_selected","selected_seen","run_queued","task_started","pcm_retained"})assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json,key)));
  cJSON_Delete(json);
  size=999;assert(p4_aoq_tts_trial_copy_pcm(report_generation(busy),out,sizeof(out),&size)==23&&size==0);++cases;
 }
 {
  Trial t;t.id=UINT64_MAX;t.stage="unsupported_audio_layout";t.error=7;t.has_tuple=t.has_conflict=true;
  t.first={UINT64_MAX,UINT64_MAX,UINT64_MAX};t.conflict=t.first;
  t.bytes=512*1024;t.pcm_objects=4096;t.terminal_events=4096;t.groups=8;t.streams=8;t.busy_retries=UINT32_MAX;
  t.unmatched_events=4096;t.ignored_events=4096;
  t.cursor_expected=t.cursor_received=t.first_timestamp=t.received_timestamp=t.received_object=UINT64_MAX;t.received_duration=UINT32_MAX;
  aoq::Diagnostics d;d.error=7;d.stage="unsupported_media_selection";d.setup_accepted=true;d.tls_verified=true;
  d.last_control_type=0xff0e30;d.last_control_length=16640;d.control_frames=256;d.media_setups=256;
  d.tx_packets=d.rx_packets=8*1024*1024;d.goaway_received=true;d.goaway_reason_length=256;
  std::memset(d.goaway_reason,'r',sizeof(d.goaway_reason)-1);std::memset(d.last_notify_tag,'n',sizeof(d.last_notify_tag)-1);
  char full[4096];assert(report(t,d,false,full,sizeof(full))==0);auto* parsed=cJSON_Parse(full);assert(parsed);cJSON_Delete(parsed);
  const size_t needed=std::strlen(full)+1;char minimum[1536],recommended[2048];
  const int minimum_result=report(t,d,false,minimum,sizeof(minimum));assert(minimum_result==(needed>sizeof(minimum)?5:0));
  assert(needed<=sizeof(recommended)&&report(t,d,false,recommended,sizeof(recommended))==0);
  parsed=cJSON_Parse(recommended);assert(parsed);cJSON_Delete(parsed);
  std::cout<<"TTS diagnostic stress report requires "<<needed<<" bytes including NUL; cap1536 result="<<minimum_result<<"\n";++cases;
 }
 std::cout<<"PASS "<<cases<<" fixed TTS trial mock cases; no allocation/network/device/playback\n";
}
