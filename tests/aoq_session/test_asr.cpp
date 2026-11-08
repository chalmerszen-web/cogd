#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_asr_trial.cpp"

int64_t test_now=1000;
enum class Scenario{Pass,WrongWord,Heartbeat,ZeroFinal,Interim,Duplicate,Conflict,OutOfOrder,InvalidUtf8,Nul,Long,Many,Cancelled,Failed,WrongTask,Audio,Busy,NoSource,TooLong,EightSeconds};
static Scenario scenario=Scenario::Pass;static unsigned opens=0,cases=0;
extern "C" int p4_aoq_tts_trial_copy_pcm(uint64_t gen,void* p,size_t cap,size_t* size){
 *size=0;if(gen!=123||scenario==Scenario::NoSource)return 23;
 const size_t samples=scenario==Scenario::TooLong?192001:scenario==Scenario::EightSeconds?192000:4800;
 assert(cap>=samples*2);auto* pcm=static_cast<int16_t*>(p);for(size_t i=0;i<samples;++i)pcm[i]=int16_t((i%71)*100-3500);*size=samples*2;return 0;
}
namespace aoq {
struct Session::Impl {
 Options options;Callbacks callbacks;Diagnostics diag;std::string task,last_busy;
 unsigned phase=0,frames=0;bool started=false,finished=false,delivered=false,refused=false,pending=false;
 int event(const char* name,const std::string& sentence="",bool other=false){
  std::string json="{\"header\":{\"event\":\""+std::string(name)+"\",\"task_id\":\""+(other?"other-task":task)+"\",\"error_message\":\"PRIVATE_ERROR_MUST_NOT_PRINT\"}";
  if(!sentence.empty())json+=",\"payload\":{\"output\":{\"sentence\":"+sentence+"}}";
  json+="}";Object object;object.payload={reinterpret_cast<const uint8_t*>(json.data()),json.size()};
  int e=callbacks.data(callbacks.userdata,object);if(e)diag.error=e;return e;
 }
 int sentence(unsigned id,const std::string& text,bool final=true,bool heartbeat=false){
  return event("result-generated","{\"sentence_id\":"+std::to_string(id)+",\"sentence_end\":"+(final?"true":"false")+",\"heartbeat\":"+(heartbeat?"true":"false")+",\"text\":\""+text+"\"}");
 }
};
Session::Session()=default;Session::~Session()=default;
int Session::open(p4_aoq_kind_t kind,const Options& o,const Callbacks& c){
 assert(kind==P4_AOQ_ASR);++opens;impl_.reset(new Impl);impl_->options=o;impl_->callbacks=c;impl_->diag.closed=false;
 impl_->diag.setup_accepted=true;impl_->diag.stage="mock_setup_accepted";return 0;
}
int Session::send_json(const char* bytes,size_t n){
 auto& p=*impl_;std::string message(bytes,n);
 if(scenario==Scenario::Busy&&!p.refused){p.refused=true;p.last_busy=message;return 3;}
 if(scenario==Scenario::Busy){assert(p.last_busy==message);p.refused=false;}
 assert(!p.pending);auto* json=cJSON_ParseWithLength(bytes,n);assert(json);
 auto* header=cJSON_GetObjectItemCaseSensitive(json,"header");
 if(header){
  const char* action=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(header,"action"));const char* task=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(header,"task_id"));assert(task&&std::strlen(task)==36);
  if(!std::strcmp(action,"run-task")){assert(p.phase==0);p.task=task;p.phase=1;}
  else {assert(!std::strcmp(action,"finish-task")&&p.started&&p.frames>0&&p.diag.queue_bytes==0&&p.diag.sent_objects==p.frames+1);assert(p.task==task);p.finished=true;}
 }else{
  assert(p.started&&!p.finished);assert(!std::strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"type")),"input_audio_buffer.append"));
  const char* id=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"event_id"));assert(id==p.task+"-audio-"+std::to_string(p.frames));
  const char* audio=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json,"audio"));assert(audio&&std::strlen(audio)<=1708&&std::strlen(audio)%4==0);++p.frames;
 }
 cJSON_Delete(json);p.pending=true;p.diag.queue_bytes=n;return 0;
}
int Session::poll(unsigned ms){
 auto& p=*impl_;test_now+=ms;if(p.pending){p.pending=false;p.diag.queue_bytes=0;++p.diag.sent_objects;}
 if(p.options.cancelled&&p.options.cancelled(p.options.cancel_userdata)){p.diag.error=4;return 4;}
 if(p.phase&&!p.started){
  if(scenario==Scenario::WrongTask){test_now+=5000;return p.event("task-started","",true);}
  if(scenario==Scenario::Cancelled){p4_aoq_asr_trial_cancel();return 0;}
  if(scenario==Scenario::Failed)return p.event("task-failed");
  p.started=true;return p.event("task-started");
 }
 if(p.finished&&!p.delivered){
  p.delivered=true;int e=0;
  switch(scenario){
   case Scenario::WrongWord:e=p.sentence(1,"原声语音测试成功。");break;
   case Scenario::Heartbeat:e=p.sentence(0,"原生语音测试成功。",true,true);break;
   case Scenario::ZeroFinal:e=p.sentence(0,"原生语音测试成功。");break;
   case Scenario::Interim:e=p.sentence(1,"原生语音测试成功。",false);break;
   case Scenario::Duplicate:e=p.sentence(1,"原生语音测试成功。");if(!e)e=p.sentence(1,"原生语音测试成功。");break;
   case Scenario::Conflict:e=p.sentence(1,"原生语音测试成功。");if(!e)e=p.sentence(1,"原声语音测试成功。");break;
   case Scenario::OutOfOrder:e=p.sentence(2,"测试成功。");if(!e)e=p.sentence(1,"原生语音");break;
   case Scenario::InvalidUtf8:e=p.sentence(1,std::string("\xc0\x80",2));break;
   case Scenario::Nul:e=p.sentence(1,"原生语音测试成功。\\u0000错");break;
   case Scenario::Long:e=p.sentence(1,std::string(257,'x'));break;
   case Scenario::Many:for(unsigned i=1;i<=9&&!e;++i)e=p.sentence(i,"原");break;
   case Scenario::Audio:{Object o;e=p.callbacks.audio(p.callbacks.userdata,o);break;}
   default:e=p.sentence(1,"  原生 语音测试成功。 \\n");break;
  }
  if(e){p.diag.error=e;return e;}
  return p.event("task-finished");
 }
 if(p.delivered)test_now+=5000;
 return 0;
}
void Session::close(){if(impl_)impl_->diag.closed=true;}
void Session::cancel(){p4_aoq_asr_trial_cancel();}
Diagnostics Session::diagnostics()const{return impl_?impl_->diag:Diagnostics{};}
}
static std::string run(Scenario next,int expected,bool network=true){
 scenario=next;char report[2048];const auto before=opens;const int e=p4_aoq_asr_trial_command("asr probe 123",report,sizeof(report));assert(e==expected);assert(opens==before+(network?1:0));
 auto* json=cJSON_Parse(report);assert(json);assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"pass"))==(expected==0));
 assert(!std::strstr(report,"PRIVATE_ERROR")&&!std::strstr(report,"task_id"));cJSON_Delete(json);++cases;return report;
}
int main(){
 run(Scenario::Pass,0);auto wrong=run(Scenario::WrongWord,18);assert(wrong.find("\"transport_complete\":true")!=wrong.npos&&wrong.find("\"expected_text_match\":false")!=wrong.npos);
 run(Scenario::Heartbeat,15);run(Scenario::ZeroFinal,7);run(Scenario::Interim,15);auto duplicate=run(Scenario::Duplicate,0);assert(duplicate.find("\"duplicate_finals\":1")!=duplicate.npos);
 run(Scenario::Conflict,7);run(Scenario::OutOfOrder,0);run(Scenario::InvalidUtf8,7);run(Scenario::Nul,7);run(Scenario::Long,7);run(Scenario::Many,5);
 run(Scenario::Cancelled,4);run(Scenario::Failed,14);run(Scenario::WrongTask,15);run(Scenario::Audio,7);run(Scenario::Busy,0);
 run(Scenario::NoSource,23,false);run(Scenario::TooLong,5,false);auto maximum=run(Scenario::EightSeconds,0);assert(maximum.find("\"frames_queued\":200")!=maximum.npos);
 for(const char* cmd:{"asr probe 0","asr probe -1","asr probe 123 ","asr probe 18446744073709551616"}){char out[2048];auto before=opens;assert(p4_aoq_asr_trial_command(cmd,out,sizeof(out))==1&&opens==before);++cases;}
 char norm[64];assert(normalize("原生，语音测试成功！",std::strlen("原生，语音测试成功！"),norm,sizeof(norm))&&!std::strcmp(norm,Expected));++cases;
 assert(!normalize("\xed\xa0\x80",3,norm,sizeof(norm)));++cases;
 std::cout<<"PASS "<<cases<<" fixed ASR trial mock cases; real PCM converter, no allocation/network/device\n";
}
