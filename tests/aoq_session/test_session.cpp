#include <memory>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include "lwip/sockets.h"
#define private public
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_session.h"
#undef private
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_session.cpp"
#include "../../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_aoq/aoq_video_fixture.inc"

int64_t test_now=1000;
// The actual idle receiver runs against the inert socket substitute. Join
// every host thread; worker delay must not advance the synthetic owner clock.
static unsigned receiver_create_calls=0;
static bool receiver_create_failure=true;
static std::vector<std::thread> receiver_threads;
int xTaskCreatePinnedToCore(void (*entry)(void*),const char*,unsigned,void* ctx,unsigned,void*,int){
 ++receiver_create_calls;if(receiver_create_failure)return 0;
 receiver_threads.emplace_back([=]{test_receiver_thread=true;entry(ctx);});return pdPASS;
}
void vTaskDelete(void*){assert(test_receiver_thread);}
static void join_receivers(){for(auto& t:receiver_threads)t.join();receiver_threads.clear();}
static bool scripted_allocation=false;
static bool prefetched_allocation=false;
static unsigned allocation_calls=0;
static void fill_allocation(p4_aoq_kind_t kind,p4_aoq_allocation_t* out){
 *out={};out->kind=kind;out->received_at_ms=test_now;out->expires_in_seconds=60;out->endpoint_count=1;
 std::strcpy(out->endpoints[0].host,"192.0.2.1");out->endpoints[0].port=8443;
 std::strcpy(out->sid,"abcdef0123456789abcdef0123456789ab:1234567890:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
 std::strcpy(out->token,"SYNTHETIC_TOKEN_NOT_REAL_0123456789");
}
extern "C" bool p4_aoq_prefetch_take(p4_aoq_kind_t kind,p4_aoq_allocation_t* out,unsigned budget){
 if(!prefetched_allocation)return false;
 assert(budget==12000);prefetched_allocation=false;fill_allocation(kind,out);return true;
}
extern "C" int p4_aoq_allocate(p4_aoq_kind_t kind,p4_aoq_allocation_t* out,unsigned){
 assert(scripted_allocation&&"Only the explicitly scripted synthetic allocation may run");
 ++allocation_calls;fill_allocation(kind,out);return 0;
}
extern "C" void p4_aoq_allocation_clear(p4_aoq_allocation_t* p){if(p)std::memset(p,0,sizeof(*p));}
using Session=aoq::Session;
using Bytes=std::vector<uint8_t>;
static unsigned cases=0;
static void ready(Session& s){
 s.impl_.reset(new Session::Impl(s.cancel_epoch_,s.cancel_epoch_.load()));auto& p=*s.impl_;p.started=test_now;p.deadline=test_now+30000;
 p.diag.closed=false;p.diag.setup_accepted=true;p.diag.tls_verified=true;p.control_id=0;p.fd=7;p.packet=aoq::buffer(4096);
 p.offered=aoq_wire::pcm_track_profile(true,true);
 p.quic.reset(new esp_http3::QuicConnection({},{}));p.add(0);
 aoq_wire::SubgroupHeader h;h.priority=128;assert(aoq_wire::encode_subgroup_header(h,p.tx_header));
}
static void number(Bytes& v,uint64_t x){size_t n=x<64?1:x<16384?2:4;uint8_t prefix=n==1?0:n==2?0x40:0x80;for(size_t i=n;i;--i){auto b=uint8_t(x>>((i-1)*8));v.push_back(i==n?b|prefix:b);}}
static void field(Bytes& out,uint8_t type,const Bytes& value){out.push_back(type);out.push_back(uint8_t(value.size()>>8));out.push_back(uint8_t(value.size()));out.insert(out.end(),value.begin(),value.end());}
static Bytes control(uint64_t type,const Bytes& payload){Bytes out;number(out,type);out.push_back(uint8_t(payload.size()>>8));out.push_back(uint8_t(payload.size()));out.insert(out.end(),payload.begin(),payload.end());return out;}
static Bytes objects(uint64_t alias,uint64_t group,bool eog,const Bytes& props,const Bytes& payload,uint64_t status=0){
 aoq_wire::SubgroupHeader h;h.track_alias=alias;h.group=group;h.end_of_group=eog;h.priority=10;
 Bytes result,object;aoq_wire::ObjectCursor cursor;assert(aoq_wire::encode_subgroup_header(h,result));
 assert(aoq_wire::encode_object(0,status,true,props,payload.data(),payload.size(),cursor,object));result.insert(result.end(),object.begin(),object.end());return result;
}
static Bytes media(bool supported=true,bool opus=false){
 aoq_wire::MediaSetup m;aoq_wire::MediaBinding b;b.track="model_audio_out";b.alias=202;b.direction=1;
 b.selected=aoq_wire::pcm_six_track_profile()[2].media[0];if(!supported||opus)b.selected.codec=10;if(opus)b.selected.sample_rate_hz=48000;m.bindings.push_back(b);
 Bytes bytes;std::string error;assert(aoq_wire::encode_media_setup(m,bytes,error));return bytes;
}
struct Events {Session* session=nullptr;int audio=0,data=0,ends=0,groups=0,tracks=0,fins=0,send_result=-1,recursive=-1;bool accept_audio=true;};
static int audio(void* p,const aoq::Object& o){auto& e=*static_cast<Events*>(p);++e.audio;assert(o.group==55&&o.payload.size==960);assert(o.opaque_userdata.size==3&&!std::memcmp(o.opaque_userdata.data,"xyz",3));return 0;}
static int end(void* p,const aoq::End& event){auto& e=*static_cast<Events*>(p);++e.ends;e.groups+=event.end_group;e.tracks+=event.end_track;e.fins+=event.stream_fin;return 0;}
static int data(void* p,const aoq::Object& o){auto& e=*static_cast<Events*>(p);++e.data;assert(o.payload.size==2);e.send_result=e.session->send_json("{}",2);e.recursive=e.session->poll(0);return 0;}
int main(){
 {
  Session s;ready(s);s.impl_->options.conversation=true;
  assert(!s.impl_->start_receiver()&&s.impl_->diag.error==19);
  assert(!s.impl_->receiver_running.load()&&receiver_create_calls==1);++cases;
 }
 receiver_create_failure=false;
 // The codec boundary may fail or return inconsistent metadata. Never send
 // partial/mislabelled PCM to playback; preserve 48k-to-24k sample mapping.
 for(unsigned fault=0;fault<=7;++fault){
  test_opus::reset(fault);
  {
   Session s;ready(s);auto& p=*s.impl_;p.options.opus_downlink=true;
   for(auto& track:p.offered)if(track.alias==202){track.media[0].codec=10;track.media[0].sample_rate_hz=48000;}
   Events e{&s};p.callbacks.userdata=&e;
   p.callbacks.audio=[](void* ctx,const aoq::Object& o){
    audio(ctx,o);aoq_wire::AudioProperties v;
    assert(aoq_wire::decode_audio_properties(o.properties.data,o.properties.size,v)==aoq_wire::ParseStatus::Ok);
    assert(v.duration_samples==480&&v.sample_cursor==0&&v.timestamp_us==123);return 0;
   };
   auto selection=media(true,true);p.incoming(0,0,selection.data(),selection.size(),false);
   assert(s.poll(0)==0);
   assert(p.diag.opus48k_selected&&!p.diag.pcm24k_selected);
   Bytes props;number(props,6);number(props,123);number(props,13);number(props,3);props.insert(props.end(),{'x','y','z'});number(props,31);number(props,0);number(props,2);number(props,960);
   auto packet=objects(202,55,true,props,Bytes{0xaa,0xbb,0xcc});
   p.incoming(3,0,packet.data(),packet.size(),true);
   assert(s.poll(0)==(fault==7?19:fault?7:0));
   assert(e.audio==(fault?0:1)&&test_opus::opened==1&&test_opus::decoded==(fault==7?0:1));
   if(!fault)assert(p.diag.audio_decoded_bytes==960&&p.diag.audio_rx_bytes==3);
  }
  assert(test_opus::live==0);++cases;
 }
 {
  const Bytes setup{0x6f,0,0,8,1,1,0x80,0xff,0x0e,0x12,1,0};
  for(bool warm:{true,false}){
   esp_http3::test_control_chunks={setup};prefetched_allocation=warm;scripted_allocation=!warm;
   const unsigned before=allocation_calls;
   Session s;aoq::Options o;o.conversation=true;o.lifetime_ms=180000;o.use_prefetched_allocation=true;
   assert(!s.open(P4_AOQ_AUDIO,o));
   assert(s.diagnostics().allocation_prefetched==warm && allocation_calls==before+!warm);
   assert(!prefetched_allocation);s.close();join_receivers();scripted_allocation=false;esp_http3::test_control_chunks.clear();++cases;
  }
 }
 {
  Session s;ready(s);auto& p=*s.impl_;Events e{&s};e.accept_audio=false;
  p.callbacks={&e,data,audio,end,[](void* ctx,size_t n){assert(n==960);return static_cast<Events*>(ctx)->accept_audio;}};
  Bytes props;number(props,6);number(props,123);number(props,13);number(props,3);props.insert(props.end(),{'x','y','z'});number(props,31);number(props,0);number(props,2);number(props,480);
  auto selection=media();p.incoming(0,0,selection.data(),selection.size(),false);
  auto bytes=objects(202,55,true,props,Bytes(960,0x42));p.incoming(3,0,bytes.data(),bytes.size(),true);
  auto text=objects(302,0,false,{},Bytes{'{','}'});p.incoming(7,0,text.data(),text.size(),false);
  // A blocked Audio object must neither advance IDs nor prevent Data and
  // transport work. Releasing capacity delivers that same PCM and its FIN.
  for(unsigned i=0;i<3;++i){assert(!s.poll(0)&&e.audio==0&&e.data==1&&e.ends==0);assert(p.object_count==1&&!p.diag.receive_drained);}
  e.accept_audio=true;assert(!s.poll(0)&&e.audio==1&&e.ends==1&&e.fins==1&&p.object_count==2&&p.diag.receive_drained);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;p.add(7);p.peer_reset(7,0x10);
  assert(s.poll(0)==8&&s.diagnostics().closed);
  assert(!std::strcmp(s.diagnostics().stage,"peer_reset_stream"));++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;p.retired[p.retired_count++]={7,1234};p.peer_reset(7,0x10);
  assert(!s.poll(0)&&!s.diagnostics().closed);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;auto* rx=p.add(7);rx->header_read=true;rx->header.track_alias=202;
  p.callbacks.audio_discarded=[](void*,uint64_t id){return id==7;};
  p.peer_reset(7,0x10);assert(!s.poll(0)&&!p.find(7));
  const uint8_t late[]={1,2,3};p.incoming(7,0,late,sizeof(late),false);
  assert(!s.poll(0)&&!p.find(7));++cases;
 }
 for(unsigned alias:{0u,302u,402u}){
  Session s;ready(s);auto& p=*s.impl_;auto* rx=p.add(7);rx->header_read=alias!=0;rx->header.track_alias=alias;
  p.callbacks.audio_discarded=[](void*,uint64_t){return true;};
  p.peer_reset(7,0x10);assert(s.poll(0)==8);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;p.options.conversation=true;
  test_udp_packets=16;test_udp_packet_ms=3;const auto started=test_now;
  assert(s.poll(0)==0&&test_now-started==6&&test_udp_packets==14);
  // The caller can submit its next microphone frame before the remaining
  // downlink burst is decrypted; nothing was discarded by the pump.
  p.diag.pcm16k_input_selected=true;int16_t input[320]{};
  assert(s.send_pcm16(input,320,1000000)==0);
  while(test_udp_packets)assert(s.poll(0)==0);
  assert(p.diag.rx_packets==16);test_udp_packet_ms=0;++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;p.options.conversation=true;
  aoq_wire::SubgroupHeader h;h.track_alias=302;h.priority=10;
  Bytes bytes,encoded;aoq_wire::ObjectCursor cursor;
  assert(aoq_wire::encode_subgroup_header(h,bytes));
  for(unsigned i=0;i<25;++i){
   encoded.clear();assert(aoq_wire::encode_object(i,0,true,{},reinterpret_cast<const uint8_t*>("{}"),2,cursor,encoded));
   bytes.insert(bytes.end(),encoded.begin(),encoded.end());
  }
  p.incoming(3,0,bytes.data(),bytes.size(),true);
  for(unsigned count:{8u,16u,24u,25u}){assert(s.poll(0)==0);assert(p.diag.data_objects==count);}
  assert(p.diag.stream_fins==1&&p.diag.receive_drained);++cases;
 }
 // A cloud burst must still be drained when the audible ring is low.
 // Under the same queued UDP load, refill eight audio frames rather than
 // allowing the two-object network preference to starve real-time playback.
 for(bool urgent:{false,true}){
  Session s;ready(s);auto& p=*s.impl_;p.options.conversation=true;p.diag.pcm24k_selected=true;
  p.receive_storage=aoq::buffer(sizeof(Session::Impl::ReceiveQueue));
  p.receive_queue=new(p.receive_storage.get()) Session::Impl::ReceiveQueue;
  for(unsigned i=0;i<64;++i){assert(p.receive_queue->write_slot());assert(p.receive_queue->publish(1));}
  p.callbacks.userdata=&urgent;p.callbacks.audio_urgent=[](void* ctx){return *static_cast<bool*>(ctx);};
  aoq_wire::SubgroupHeader h;h.track_alias=202;h.priority=10;
  Bytes bytes,encoded,props,payload(960,0);aoq_wire::ObjectCursor cursor;aoq_wire::AudioProperties ap;
  ap.duration_samples=480;assert(aoq_wire::encode_pcm_properties(ap,props));
  assert(aoq_wire::encode_subgroup_header(h,bytes));
  for(unsigned i=0;i<16;++i){
   encoded.clear();assert(aoq_wire::encode_object(i,0,true,props,payload.data(),payload.size(),cursor,encoded));
   bytes.insert(bytes.end(),encoded.begin(),encoded.end());
  }
  p.incoming(3,0,bytes.data(),bytes.size(),true);assert(!s.poll(0));
  assert(p.diag.rx_packets==64&&p.receive_queue->pending()==0);
  assert(p.diag.audio_objects==(urgent?8u:2u));++cases;
 }
 for(const auto kind:{P4_AOQ_AUDIO,P4_AOQ_ASR,P4_AOQ_TTS}){
  esp_http3::test_control_chunks={{0x6f,0,0,8,1,1,0x80,0xff,0x0e,0x12,1,0}};
  scripted_allocation=true;Session s;assert(s.open(kind)==0);scripted_allocation=false;
  esp_http3::test_control_chunks.clear();auto& p=*s.impl_;
  bool has_in=false,has_out=false;for(const auto& t:p.offered){has_in|=t.alias==201;has_out|=t.alias==202;}
  assert(has_in==(kind!=P4_AOQ_TTS)&&has_out==(kind!=P4_AOQ_ASR));
  assert(s.diagnostics().setup_accepted&&!s.diagnostics().pcm24k_selected);++cases;
  assert(p.quic->reset&&p.quic->stop_sending);
  // A valid Data-only selection must not invent a PCM output selection.
  const auto full=aoq_wire::pcm_six_track_profile();aoq_wire::MediaBinding text;
  text.track=full[3].name;text.alias=full[3].alias;text.direction=full[3].direction;text.selected=full[3].media[0];
  aoq_wire::MediaSetup data_only;data_only.bindings={text};Bytes bytes;std::string error;
  assert(aoq_wire::encode_media_setup(data_only,bytes,error));
  const uint64_t offset=p.find(0)->window.consumed();
  p.incoming(0,offset,bytes.data(),bytes.size(),false);assert(s.poll(0)==0&&!s.diagnostics().pcm24k_selected);++cases;
  aoq_wire::MediaBinding audio;const auto& track=full[kind==P4_AOQ_TTS?0:2];
  audio.track=track.name;audio.alias=track.alias;audio.direction=track.direction;audio.selected=track.media[0];
  aoq_wire::MediaSetup selection;selection.bindings={audio};assert(aoq_wire::encode_media_setup(selection,bytes,error));
  const uint64_t next_offset=p.find(0)->window.consumed();p.incoming(0,next_offset,bytes.data(),bytes.size(),false);
  if(kind==P4_AOQ_AUDIO){assert(s.poll(0)==0&&s.diagnostics().pcm24k_selected);}
  else {assert(s.poll(0)==7&&!s.diagnostics().pcm24k_selected&&!std::strcmp(s.diagnostics().stage,"unsupported_media_selection"));}
  ++cases;
 }
 {
  // Independent literal TLVs: payload length 109 mirrors the observed length,
  // but the reason and credentials are entirely synthetic, not captured bytes.
  const std::string reason="synthetic refusal "+std::string(88,'R');assert(reason.size()==106);
  Bytes body;field(body,2,Bytes(reason.begin(),reason.end()));assert(body.size()==109);
  const auto away=control(0x10,body);
  Bytes notify_body;field(notify_body,1,Bytes{'P','R','I','V','A','T','E','_','N','O','T','I','F','Y'});
  field(notify_body,2,Bytes{1});field(notify_body,3,Bytes{'s','t','a','t','u','s'});
  const auto notify=control(0xff0e30,notify_body);
  const Bytes setup{0x6f,0,0,8,1,1,0x80,0xff,0x0e,0x12,1,0};
  for(bool fragmented:{false,true}){
   Bytes prefix=setup;prefix.insert(prefix.end(),notify.begin(),notify.end());
   if(fragmented)esp_http3::test_control_chunks={prefix,Bytes(away.begin(),away.begin()+4),Bytes(away.begin()+4,away.end())};
   else {prefix.insert(prefix.end(),away.begin(),away.end());esp_http3::test_control_chunks={prefix};}
   scripted_allocation=true;Session s;assert(s.open(P4_AOQ_TTS)==7);scripted_allocation=false;
   const auto d=s.diagnostics();assert(d.setup_accepted&&d.tls_verified&&d.closed&&d.error==7);
   assert(!std::strcmp(d.stage,"server_goaway")&&d.goaway_received&&d.goaway_reason_length==106);
   assert(d.last_control_type==16&&d.last_control_length==109&&d.notifications==1&&d.control_frames==3);
   assert(!d.pcm24k_selected&&d.media_setups==0&&d.sent_objects==0);
   assert(!std::strcmp(d.last_notify_tag,"status")&&!std::strstr(d.goaway_reason,"PRIVATE_NOTIFY"));
   assert(s.send_json("{}",2)!=0);esp_http3::test_control_chunks.clear();++cases;
  }
 }
 {
  const std::string sid=std::string(32,'a')+":1234567890:"+std::string(64,'b');
  const std::string token="SYNTHETIC_TOKEN_0123456789ABCDEFGH";
  const std::string hostile="reason "+sid+" "+token+" https://x.example/secret a@b.example \"quoted\" \\path "+std::string("\x01\xc3\xa9\xe4\xb8\xad",6);
  for(size_t cap:{size_t(1),size_t(2),size_t(8),size_t(64),size_t(160)}){
   std::vector<char> buffer(cap+2,'#');aoq::diagnostic_text(hostile,buffer.data()+1,cap);const char* safe=buffer.data()+1;
   assert(buffer.front()=='#'&&buffer.back()=='#'&&std::strlen(safe)<cap);
   assert(!std::strstr(safe,sid.c_str())&&!std::strstr(safe,token.c_str())&&!std::strstr(safe,"secret")&&!std::strstr(safe,"a@b"));
   for(const unsigned char* p=reinterpret_cast<const unsigned char*>(safe);*p;++p)assert(*p>=32&&*p<127&&*p!='"'&&*p!='\\');
   const std::string json="{\"reason\":\""+std::string(safe)+"\"}";auto* parsed=cJSON_Parse(json.c_str());assert(parsed);cJSON_Delete(parsed);++cases;
  }
  std::string long_reason;for(unsigned i=0;i<100;++i)long_reason+="safe ";char full[160];aoq::diagnostic_text(long_reason,full,sizeof(full));
  assert(std::strlen(full)==159);++cases;
 }
 {
  Session s;s.cancel();assert(!s.impl_);ready(s);assert(s.poll(0)==0);
  s.cancel();assert(s.poll(0)==4);ready(s);assert(s.poll(0)==0);++cases;
 }
 {
  Session s;std::atomic<bool> go{false},done{false};
  std::thread cancelling([&]{go.store(true);while(!done.load())s.cancel();});
  while(!go.load())std::this_thread::yield();
  for(unsigned i=0;i<1000;++i)s.impl_.reset(new Session::Impl(s.cancel_epoch_,s.cancel_epoch_.load()));
  done.store(true);cancelling.join();
  const auto count=s.cancel_epoch_.load();assert(count>0);
  ready(s);s.cancel();assert(s.poll(0)==4);++cases;
 }
 {
  p4_aoq_allocation_t allocation{};std::strcpy(allocation.sid,"abcdef0123:synthetic-expiry:synthetic-signature");
  p4_aoq_endpoint_t endpoint{};endpoint.route_index=65;esp_http3::QuicConfig cfg;
  assert(aoq::configure_transport_identity(cfg,allocation,endpoint));
  assert(cfg.alpn==aoq_wire::kAlpn&&cfg.hostname=="localhost");
  assert((cfg.initial_cid_prefix==Bytes{0,'a','b','c'}));
  const auto& tp=cfg.extra_transport_parameters;assert(tp.size()==3&&tp[0].first==0xff0e07&&tp[0].second==Bytes{1});
  assert(tp[1].first==0xff0e08&&std::string(tp[1].second.begin(),tp[1].second.end())==allocation.sid);
  assert(tp[2].first==0xff0e09&&(tp[2].second==Bytes{0x40,65}));++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;p.diag.setup_accepted=false;
  Bytes setup{0x6f,0,0,8,1,1,0x80,0xff,0x0e,0x12,1,0};
  p.incoming(0,6,setup.data()+6,setup.size()-6,false);assert(s.poll(0)==0&&!p.diag.setup_accepted);
  p.incoming(0,0,setup.data(),6,false);assert(s.poll(0)==0&&p.diag.setup_accepted&&!p.diag.pcm24k_selected);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;std::string large="{\"v\":\""+std::string(16376,'x')+"\"}";
  assert(large.size()==16384);for(int i=0;i<3;++i)assert(s.send_json(large.data(),large.size())==0);
  const auto objects=p.next_object;const auto bytes=p.queue_bytes;
  assert(s.send_json(large.data(),large.size())==3&&p.next_object==objects&&p.queue_bytes==bytes);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;for(int i=0;i<8;++i)assert(s.send_json("{}",2)==0);
  auto count=p.next_object,previous=p.tx_cursor.previous;auto size=p.queue_bytes;
  assert(s.send_json("{}",2)==3&&p.next_object==count&&p.tx_cursor.previous==previous&&p.queue_bytes==size);++cases;
  p.quic->write_limit=0;p.transmit();assert(p.tx_header_offset==0&&p.queue_count==8);++cases;
  p.quic->write_limit=3;for(int i=0;i<100&&p.queue_count;++i)p.transmit();assert(p.queue_count==0&&p.diag.sent_objects==8);
  const auto& bytes=p.quic->sent[2];aoq_wire::SubgroupHeader h;assert(aoq_wire::decode_subgroup_header(bytes.data(),bytes.size(),h)==aoq_wire::ParseStatus::Ok&&h.track_alias==301&&h.group==0&&h.subgroup==0);
  size_t at=h.consumed;aoq_wire::ObjectCursor cursor;for(uint64_t i=0;i<8;++i){aoq_wire::ObjectView o;assert(aoq_wire::decode_object(bytes.data()+at,bytes.size()-at,true,cursor,o)==aoq_wire::ParseStatus::Ok&&o.id==i&&o.payload_size==2);at+=o.consumed;}assert(at==bytes.size());++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;Events e{&s};p.callbacks={&e,data,audio,end};
  Bytes props;number(props,6);number(props,123);number(props,13);number(props,3);props.insert(props.end(),{'x','y','z'});number(props,31);number(props,0);number(props,2);number(props,480);
  auto bytes=objects(202,55,true,props,Bytes(960,0x42));size_t half=bytes.size()/2;
  p.incoming(3,half,bytes.data()+half,bytes.size()-half,true);p.incoming(3,0,bytes.data(),half,false);
  assert(s.poll(0)==0&&e.audio==0&&!p.diag.pcm24k_selected);++cases;
  auto selection=media();p.incoming(0,0,selection.data(),selection.size(),false);
  assert(s.poll(0)==0&&e.audio==1&&e.ends==1&&e.groups==1&&e.fins==1&&p.diag.pcm24k_selected);++cases;
  p.incoming(3,0,bytes.data(),bytes.size(),true);assert(s.poll(0)==0&&e.audio==1&&e.ends==1);++cases;
  auto text=objects(302,0,false,{},Bytes{'{','}'});p.incoming(7,0,text.data(),text.size(),false);
  assert(s.poll(0)==0&&e.data==1&&e.send_result==0&&e.recursive==3);++cases;
 }
 for(uint64_t terminal:{3u,4u}){
  Session s;ready(s);auto& p=*s.impl_;Events e{&s};p.callbacks={&e,nullptr,audio,end};
  auto selection=media();p.incoming(0,0,selection.data(),selection.size(),false);
  auto bytes=objects(202,55,false,{}, {},terminal);p.incoming(3,0,bytes.data(),bytes.size(),true);
  assert(s.poll(0)==0&&e.audio==0&&e.ends==2&&e.fins==1&&e.groups==(terminal==3)&&e.tracks==(terminal==4));++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;auto bytes=media(false);p.incoming(0,0,bytes.data(),bytes.size(),false);
  assert(s.poll(0)==7&&p.diag.closed&&!p.diag.pcm24k_selected);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;aoq_wire::MediaSetup m;aoq_wire::MediaBinding good,bad;
  good.track="model_audio_out";good.alias=202;good.direction=1;good.selected=aoq_wire::pcm_six_track_profile()[2].media[0];
  bad=good;bad.track="unoffered_track";bad.alias=999;m.bindings={good,bad};Bytes bytes;std::string error;
  assert(aoq_wire::encode_media_setup(m,bytes,error));p.incoming(0,0,bytes.data(),bytes.size(),false);
  assert(s.poll(0)==7&&p.diag.closed&&!p.diag.pcm24k_selected);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;auto bytes=objects(302,0,false,{},Bytes{'{','}'});bytes.pop_back();p.incoming(3,0,bytes.data(),bytes.size(),true);
  assert(s.poll(0)==7);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;p.token_deadline=test_now-1;assert(s.poll(0)==0);++cases;
  p.diag.setup_accepted=false;assert(s.poll(0)==15);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;uint8_t b=0;for(unsigned i=0;i<8;++i)p.incoming(3+i*4,0,&b,1,false);
  assert(p.diag.error==5&&p.diag.peak_streams==8);++cases;
 }
 {
  Session s;ready(s);s.cancel();assert(s.poll(0)==4&&s.diagnostics().closed);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;int16_t pcm[320]{};
  assert(s.send_pcm16(pcm,320,1000000)==3&&p.quic->datagrams.empty());
  p.diag.pcm16k_input_selected=true;p.quic->datagram_ready=false;
  assert(s.send_pcm16(pcm,320,1000000)==3&&p.next_audio_group==0&&p.diag.audio_tx_samples==0);
  p.quic->datagram_ready=true;assert(s.send_pcm16(pcm,320,1000000)==0);
  assert(s.send_pcm16(pcm,320,1020000)==0&&p.quic->datagrams.size()==2&&p.diag.audio_tx_samples==640);
  for(size_t i=0;i<2;++i){
   const auto& bytes=p.quic->datagrams[i];aoq_wire::DatagramHeader header;aoq_wire::ObjectView object;
   assert(aoq_wire::decode_datagram(bytes.data(),bytes.size(),header,object)==aoq_wire::ParseStatus::Ok);
   assert(header.track_alias==201&&header.group==i&&header.id==0&&header.end_of_group&&object.payload_size==640);
   aoq_wire::AudioProperties props;assert(aoq_wire::decode_audio_properties(object.properties,object.properties_size,props)==aoq_wire::ParseStatus::Ok);
   assert(props.timestamp_us==1000000+20000*i&&props.sample_cursor==320*i&&props.duration_samples==320);
  }
  assert(s.send_pcm16(pcm,321,1040000)==1&&s.send_pcm16(nullptr,320,1040000)==1&&s.send_pcm16(pcm,320,1020000)==1);
  p.quic->datagram_ok=false;assert(s.send_pcm16(pcm,320,1040000)==8&&p.next_audio_group==2);
  assert(s.poll(0)==8&&s.diagnostics().closed);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;assert(s.poll(0)==0&&s.diagnostics().receive_drained);
  const auto frame=objects(302,0,false,{},Bytes{'{','}'});
  p.incoming(3,5,frame.data()+5,frame.size()-5,false);
  assert(s.poll(0)==0&&!s.diagnostics().receive_drained);
  p.incoming(3,0,frame.data(),5,false);assert(s.poll(0)==0&&s.diagnostics().receive_drained);++cases;
 }
 {
  Session s;aoq::Options options;options.video_width=320;options.video_height=256;
  assert(s.open(P4_AOQ_AUDIO,options)==1);options.video_width=0;assert(s.open(P4_AOQ_OMNI,options)==1);++cases;
 }
 {
  Session s;ready(s);auto& p=*s.impl_;p.options.video_width=320;p.options.video_height=256;
  p.offered.insert(p.offered.begin(),aoq_wire::jpeg_track(320,256));
  aoq_wire::MediaSetup setup;aoq_wire::MediaBinding b;b.track="model_video_in";b.alias=101;b.selected=p.offered[0].media[0];setup.bindings={b};
  assert(s.send_jpeg(fixture_red_square,sizeof(fixture_red_square),1000000)==3);
  assert(p.selection(setup)&&p.diag.jpeg_selected);++cases;
  assert(s.send_jpeg(fixture_red_square,sizeof(fixture_red_square)-1,1000000)==7&&!p.video.bytes);
  assert(s.send_jpeg(fixture_red_square,sizeof(fixture_red_square),1000000)==0);
  assert(s.send_jpeg(fixture_blue_circle,sizeof(fixture_blue_circle),2000000)==3&&p.diag.video_frames_queued==1);++cases;
  assert(s.send_json("{}",2)==0);p.quic->write_limit=0;p.transmit();assert(p.video_id<0&&p.diag.video_pending);
  p.quic->write_limit=256;p.quic->raw_ready=false;
  for(unsigned i=0;i<32;++i)p.transmit();
  assert(p.diag.video_pending&&p.quic->fins.empty());
  p.quic->raw_ready=true;for(unsigned i=0;i<32&&p.video.bytes;++i)p.transmit();
  assert(!p.diag.video_pending&&p.diag.video_frames_sent==1&&p.quic->fins.size()==1&&p.diag.sent_objects==1);++cases;
  assert(s.send_jpeg(fixture_red_square,sizeof(fixture_red_square),1999999)==1);
  assert(s.send_jpeg(fixture_blue_circle,sizeof(fixture_blue_circle),2000000)==0);
  p.quic->fin_ok=false;for(unsigned i=0;i<32&&!p.diag.error;++i)p.transmit();assert(p.diag.error==8);++cases;
 }
 std::cout<<"PASS "<<cases<<" native session mocked transport cases; no network/device/allocation\n";
}
