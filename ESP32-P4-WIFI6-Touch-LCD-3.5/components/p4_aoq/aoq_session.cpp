#include "aoq_session.h"
#include "aoq_prefetch.h"
#include "aoq_wire.h"
#include "aoq_objects.h"
#include "aoq_media_control.h"
#include "aoq_stream_window.h"
#include "aoq_packet_queue.h"
#include "aoq_transport.h"
#include "aoq_metadata.h"
#include "aoq_video.h"
#include "core/quic_connection.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_opus_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <new>
#include <vector>

namespace aoq { namespace {
constexpr int Argument=1,Busy=3,Cancelled=4,Limit=5,JsonError=6,Protocol=7,Network=8,Dns=9,Timeout=15,Memory=19;
constexpr size_t WindowSize=72*1024,ConversationWindowSize=512*1024,JsonMax=48*1024,QueueMax=64*1024;
constexpr size_t ControlMax=16640,StreamCount=8,PeerStreams=32,ObjectMax=4096;
constexpr uint64_t ReceiveMax=4*1024*1024,PacketMax=8*1024*1024,JsonTotalMax=1024*1024;
int64_t now_ms(){return esp_timer_get_time()/1000;}
void wipe(void* data,size_t n){auto* p=static_cast<volatile uint8_t*>(data);while(n--)*p++=0;}
struct BufferDelete {void operator()(uint8_t* p)const{heap_caps_free(p);}};
using Buffer=std::unique_ptr<uint8_t,BufferDelete>;
Buffer buffer(size_t n){return Buffer(static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));}
struct AllocationDelete {void operator()(p4_aoq_allocation_t* p)const{if(p){p4_aoq_allocation_clear(p);heap_caps_free(p);}}};
struct WipeVector {std::vector<uint8_t>& v;~WipeVector(){if(!v.empty())wipe(v.data(),v.size());}};

bool varint(const uint8_t* b,size_t n,size_t& p,uint64_t& value){
    if(p>=n)return false;
    const size_t count=size_t(1)<<(b[p]>>6);
    if(count>n-p)return false;
    value=b[p]&63;
    for(size_t i=1;i<count;++i)value=(value<<8)|b[p+i];
    p+=count;return true;
}
// Preserve opaque user data without interpreting any bytes as a response ID.
bool user_data(Bytes properties,Bytes& out){
    size_t p=0,count=0;uint64_t type=0;out={};
    while(p<properties.size){
        uint64_t delta=0,value=0;
        if(++count>64||!varint(properties.data,properties.size,p,delta)||
           (count>1&&!delta)||type>((uint64_t(1)<<62)-1)-delta)return false;
        type+=delta;
        if(!varint(properties.data,properties.size,p,value))return false;
        if(type&1){
            if(value>properties.size-p)return false;
            if(type==19)out={properties.data+p,static_cast<size_t>(value)};
            p+=static_cast<size_t>(value);
        }
    }
    return true;
}
bool json_object(const uint8_t* p,size_t n){
    if(!p||!n||n>JsonMax||memchr(p,0,n))return false;
    unsigned depth=0;bool quoted=false,escaped=false;
    for(size_t i=0;i<n;++i){
        const char c=static_cast<char>(p[i]);
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}
        else if(c=='"')quoted=true;
        else if(c=='{'||c=='['){if(++depth>32)return false;}
        else if(c=='}'||c==']'){if(!depth)return false;--depth;}
    }
    const char* end=nullptr;
    auto* value=cJSON_ParseWithLengthOpts(reinterpret_cast<const char*>(p),n,&end,false);
    bool valid=cJSON_IsObject(value);
    if(end){const char* last=reinterpret_cast<const char*>(p)+n;while(end<last&&(*end==' '||*end=='\t'||*end=='\r'||*end=='\n'))++end;valid=valid&&end==last;}
    else valid=false;
    cJSON_Delete(value);return valid;
}
// Diagnostics accept only bounded printable ASCII without JSON quotes. Mask
// long credential-shaped tokens, URLs and addresses; never expose notify data.
void diagnostic_text(const std::string& input,char* out,size_t capacity){
    size_t w=0;
    for(size_t at=0;at<input.size()&&w+1<capacity;){
        size_t end=at;while(end<input.size()&&input[end]!=' '&&input[end]!='\t'&&input[end]!='\r'&&input[end]!='\n')++end;
        const auto word=input.substr(at,end-at);
        const bool hidden=word.size()>=24||word.find("://")!=std::string::npos||word.find('@')!=std::string::npos;
        if(hidden){const char mask[]="[redacted]";for(char c:mask)if(c&&w+1<capacity)out[w++]=c;}
        else for(unsigned char c:word)if(w+1<capacity)out[w++]=(c>=32&&c<127&&c!='"'&&c!='\\')?static_cast<char>(c):' ';
        at=end;
        if(at<input.size()){++at;if(w+1<capacity)out[w++]=' ';}
    }
    out[w]=0;
}
bool same_param(const aoq_wire::MediaParam& a,const aoq_wire::MediaParam& b){
    return a.codec==b.codec&&a.audio_model==b.audio_model&&a.audio_payload_format==b.audio_payload_format&&
           a.sample_rate_hz==b.sample_rate_hz&&a.channels==b.channels&&a.schema==b.schema&&a.timescale==b.timescale&&
           a.video_width==b.video_width&&a.video_height==b.video_height&&a.video_fps==b.video_fps;
}
} // namespace

struct Session::Impl {
    struct Rx {
        uint64_t id;
        Buffer bytes,seen;
        aoq_wire::StreamWindow window;
        aoq_wire::SubgroupHeader header;
        aoq_wire::ObjectCursor cursor;
        bool header_read=false,terminal=false,group_ended=false;
        void* opus=nullptr;
        Rx(uint64_t stream,size_t capacity):id(stream),bytes(buffer(2*capacity)),seen(buffer(capacity)),
            window(bytes.get(),seen.get(),capacity){}
        ~Rx(){if(opus)esp_opus_dec_close(opus);}
    };
    struct Tombstone {uint64_t id=0,final_size=0;};
    struct Tx {Buffer bytes;size_t size=0,offset=0;};
    Options options;Callbacks callbacks;Diagnostics diag;
    int fd=-1;bool pumping=false,closing=false,close_requested=false;
    const std::atomic<uint32_t>& cancel_epoch;
    const uint32_t cancel_snapshot;
    int64_t started=0,deadline=0,setup_deadline=0,token_deadline=0,previous_tick=0;
    std::unique_ptr<esp_http3::QuicConnection> quic;
    Buffer packet,decoded_pcm;
    // A 72 KiB stream window may contain hundreds of short Opus datagrams.
    // Packet count, not only byte capacity, must cover that receive burst.
    using ReceiveQueue=PacketQueue<512,4096>;
    Buffer receive_storage;
    ReceiveQueue* receive_queue=nullptr;
    std::atomic<bool> receiver_stop{false},receiver_running{false};
    std::atomic<int> receiver_error{0};
    std::atomic<unsigned> receiver_waits{0};
    std::vector<uint8_t> setup,tx_header;
    std::vector<aoq_wire::Track> offered;
    size_t setup_offset=0,tx_header_offset=0;
    int64_t control_id=-1,data_id=-1;
    std::array<std::unique_ptr<Rx>,StreamCount> streams;
    std::array<Tombstone,256> retired{};
    size_t retired_count=0;
    std::array<Tx,8> queue;
    size_t queue_read=0,queue_write=0,queue_count=0,queue_bytes=0;
    aoq_wire::ObjectCursor tx_cursor;
    uint64_t next_object=0,object_count=0;
    uint64_t next_audio_group=0,audio_timestamp=0;
    Tx video;int64_t video_id=-1;uint64_t video_timestamp=0;
    size_t peer_limit()const{return options.conversation?256:PeerStreams;}
    uint64_t receive_limit()const{return options.conversation?32*1024*1024:ReceiveMax;}
    uint64_t packet_limit()const{return options.conversation?64*1024*1024:PacketMax;}
    uint64_t json_total_limit()const{return options.conversation?4*1024*1024:JsonTotalMax;}
    size_t object_limit()const{return options.conversation?32768:ObjectMax;}
    size_t json_limit()const{return options.conversation?JsonMax:16*1024;}

    Impl(const std::atomic<uint32_t>& epoch,uint32_t snapshot):cancel_epoch(epoch),cancel_snapshot(snapshot){}
    ~Impl(){shutdown();}
    static void receive_task(void* context){
        auto* self=static_cast<Impl*>(context);
        while(!self->receiver_stop.load(std::memory_order_acquire)){
            auto* slot=self->receive_queue->write_slot();
            if(!slot){self->receiver_waits.fetch_add(1,std::memory_order_relaxed);vTaskDelay(1);continue;}
            const int n=recv(self->fd,slot->data.data(),slot->data.size(),0);
            if(n<0){
                if(errno!=EAGAIN && errno!=EWOULDBLOCK){self->receiver_error.store(errno,std::memory_order_release);break;}
                vTaskDelay(1);continue;
            }
            if(!n){vTaskDelay(1);continue;}
            if(!self->receive_queue->publish(static_cast<size_t>(n))){self->receiver_error.store(ENOBUFS,std::memory_order_release);break;}
        }
        self->receiver_running.store(false,std::memory_order_release);
        vTaskDelete(nullptr); // No further access to Impl after publishing stopped.
    }
    bool start_receiver(){
        if(!options.conversation)return true;
        receive_storage=buffer(sizeof(ReceiveQueue));
        if(!receive_storage){fail(Memory,"udp_queue_memory");return false;}
        receive_queue=new(receive_storage.get()) ReceiveQueue;
        receiver_running.store(true,std::memory_order_release);
        if(xTaskCreatePinnedToCore(receive_task,"aoq_udp_rx",4096,this,12,nullptr,0)!=pdPASS){
            receiver_running.store(false,std::memory_order_release);fail(Memory,"udp_reader_task");return false;
        }
        return true;
    }
    int receive_packet(){
        if(!receive_queue)return recv(fd,packet.get(),4096,0);
        const auto* slot=receive_queue->front();
        if(!slot){const int error=receiver_error.load(std::memory_order_acquire);errno=error?error:EAGAIN;return -1;}
        const int n=slot->size;memcpy(packet.get(),slot->data.data(),static_cast<size_t>(n));
        receive_queue->release();return n;
    }
    void receiver_metrics(){
        if(receive_queue)diag.udp_queue_peak=receive_queue->peak();
        diag.udp_queue_waits=receiver_waits.load(std::memory_order_relaxed);
    }
    int fail(int error,const char* stage){if(!diag.error){diag.error=error;diag.stage=stage;}return diag.error;}
    bool stopping(){return cancel_epoch.load(std::memory_order_relaxed)!=cancel_snapshot||close_requested||
                          (options.cancelled&&options.cancelled(options.cancel_userdata));}
    int bounds(){
        diag.elapsed_ms=now_ms()-started;
        if(stopping())return fail(Cancelled,"cancelled");
        if(now_ms()>=deadline)return fail(Timeout,"trial_deadline");
        if(!diag.setup_accepted&&token_deadline&&now_ms()>=token_deadline)return fail(Timeout,"allocation_expired_before_setup");
        if(!diag.setup_accepted&&setup_deadline&&now_ms()>=setup_deadline)return fail(Timeout,"setup_deadline");
        return diag.error;
    }
    void shutdown(){
        closing=true;
        receiver_stop.store(true,std::memory_order_release);
        // recv is nonblocking. Join before closing the socket or releasing
        // packet storage; protocol callbacks remain on the original owner.
        while(receiver_running.load(std::memory_order_acquire))vTaskDelay(1);
        receiver_metrics();
        if(receive_queue){receive_queue->~ReceiveQueue();receive_queue=nullptr;}
        receive_storage.reset();
        if(quic){quic->Close();quic.reset();}
        if(fd>=0){::close(fd);fd=-1;}
        for(auto& q:queue){if(q.bytes)wipe(q.bytes.get(),q.size);q=Tx{};}
        if(video.bytes)wipe(video.bytes.get(),video.size);
        video=Tx{};video_id=-1;diag.video_pending=false;
        for(auto& s:streams)s.reset();
        if(!setup.empty()){wipe(setup.data(),setup.size());setup.clear();}
        packet.reset();diag.closed=true;diag.active_streams=0;diag.queue_bytes=0;
        decoded_pcm.reset();
        queue_count=queue_bytes=0;
    }
    Rx* find(uint64_t id){for(auto& s:streams)if(s&&s->id==id)return s.get();return nullptr;}
    void peer_reset(uint64_t id,uint64_t code){
        // A reset after a fully consumed FIN cannot retract delivered media.
        // An unfinished stream, however, will never produce its missing EOF.
        // Propagate that failure instead of leaving the conversation waiting
        // indefinitely after its playback buffer has run dry.
        for(size_t i=0;i<retired_count;++i)if(retired[i].id==id)return;
        const auto* s=find(id);
        std::printf("AOQ_RESET stream=%llu code=%llu alias=%llu consumed=%llu pending=%u final=%u audio_bytes=%llu\n",
            static_cast<unsigned long long>(id),static_cast<unsigned long long>(code),static_cast<unsigned long long>(s&&s->header_read?s->header.track_alias:0),
            static_cast<unsigned long long>(s?s->window.consumed():0),s&&s->window.pending(),s&&s->window.has_final_size(),
            static_cast<unsigned long long>(diag.audio_rx_bytes));
        std::printf("AOQ_RESET_WINDOW contiguous=%u reordered_span=%u backpressure=%u audio_age_ms=%lld\n",
            static_cast<unsigned>(s?s->window.size():0),static_cast<unsigned>(s?s->window.out_of_order_span():0),
            static_cast<unsigned>(diag.audio_backpressure),
            diag.last_audio_ms?static_cast<long long>(now_ms()-diag.last_audio_ms):-1LL);
        if(s && s->header_read && s->window.size()){
            auto cursor=s->cursor;aoq_wire::ObjectView object;
            const auto parsed=aoq_wire::decode_object(s->window.data(),s->window.size(),s->header.has_properties,cursor,object);
            if(parsed==aoq_wire::ParseStatus::Ok)
                std::printf("AOQ_RESET_BUFFERED object=%llu status=%llu payload=%u eog=%u\n",
                    static_cast<unsigned long long>(object.id),static_cast<unsigned long long>(object.status),
                    static_cast<unsigned>(object.payload_size),unsigned(s->header.end_of_group));
        }
        fail(Network,"peer_reset_stream");
    }
    Rx* add(uint64_t id){
        for(auto& s:streams)if(!s){
            s.reset(new(std::nothrow) Rx(id,options.conversation?ConversationWindowSize:WindowSize));
            if(!s||s->window.failed()){s.reset();fail(Memory,"receive_window_memory");return nullptr;}
            ++diag.active_streams;diag.peak_streams=std::max(diag.peak_streams,diag.active_streams);return s.get();
        }
        fail(Limit,"active_stream_limit");return nullptr;
    }
    void incoming(uint64_t id,uint64_t offset,const uint8_t* p,size_t n,bool fin){
        if(diag.error||closing)return;
        if(fin&&options.trace_metadata)std::printf("AOQ_FIN stream=%llu offset=%llu bytes=%u\n",static_cast<unsigned long long>(id),static_cast<unsigned long long>(offset),static_cast<unsigned>(n));
        for(size_t i=0;i<retired_count;++i)if(retired[i].id==id){
            const auto end=retired[i].final_size;
            if(offset>end||n>end-offset||(fin&&offset+n!=end))fail(Protocol,"retired_stream_final_size");
            return; // Retransmitted bytes of an already consumed stream.
        }
        if(id!=0&&(id&3)!=3){fail(Protocol,"unexpected_stream_direction");return;}
        Rx* s=find(id);if(!s)s=add(id);if(!s)return;
        if(!s->window.insert(offset,p,n,fin))fail(Protocol,"stream_window_invalid");
    }
    bool consume(Rx& s,size_t n){
        if(n>receive_limit()-diag.consumed_bytes){fail(Limit,"receive_total_limit");return false;}
        if(!quic->AcknowledgeRawStreamData(s.id,n)||!s.window.consume(n)){fail(Protocol,"stream_consume_failed");return false;}
        diag.consumed_bytes+=n;return true;
    }
    bool selection(const aoq_wire::MediaSetup& media){
        // Every new selection replaces permission. A partial update without an
        // Audio binding therefore suspends Audio until a new explicit binding.
        diag.pcm24k_selected=false;
        diag.opus48k_selected=false;
        diag.pcm16k_input_selected=false;
        diag.jpeg_selected=false;
        bool next_selected=false,next_input=false,next_video=false;
        for(const auto& b:media.bindings){
            const aoq_wire::Track* track=nullptr;
            for(const auto& t:offered)if(t.alias==b.alias&&t.name==b.track&&t.direction==b.direction)track=&t;
            if(!track)return false;
            bool offered_param=false;
            for(const auto& p:track->media)if(same_param(p,b.selected))offered_param=true;
            if(!offered_param)return false;
            if(b.alias==101){
                if(b.selected.codec!=1||b.direction!=0||!options.video_width||
                   b.selected.video_width!=options.video_width||b.selected.video_height!=options.video_height||b.selected.video_fps!=1)return false;
                next_video=true;
            }
            if(b.alias==202){
                if(options.opus_downlink?!aoq_wire::selected_opus48k_segment(b):!aoq_wire::selected_pcm24k_segment(b))return false;
                next_selected=true;
            }
            if(b.alias==201){
                if(b.selected.codec!=11||b.selected.audio_model!=1||b.selected.audio_payload_format!=0||
                   b.selected.sample_rate_hz!=16000||b.selected.channels!=1)return false;
                next_input=true;
            }
        }
        diag.pcm24k_selected=next_selected&&!options.opus_downlink;
        diag.opus48k_selected=next_selected&&options.opus_downlink;
        if(next_selected)std::printf("AOQ_DOWNLINK codec=%s wire_rate=%u output_rate=24000 channels=1\n",
            options.opus_downlink?"opus":"pcm",options.opus_downlink?48000u:24000u);
        diag.pcm16k_input_selected=next_input;
        diag.jpeg_selected=next_video;
        return true;
    }
    void controls(Rx& s){
        while(!diag.error&&s.window.size()){
            if(bounds())return;
            const auto* p=s.window.data();const size_t n=s.window.size();size_t at=0;uint64_t type=0;
            if(!varint(p,n,at,type)||n-at<2)break;
            const size_t payload=(size_t(p[at])<<8)|p[at+1];
            diag.last_control_type=type;diag.last_control_length=payload;
            if(payload>ControlMax){fail(Limit,"control_frame_limit");return;}
            if(payload>n-at-2)break;
            size_t used=0;std::string error;aoq_wire::ParseStatus status;
            if(type==aoq_wire::kSetupType){
                if(diag.setup_accepted){fail(Protocol,"duplicate_server_setup");return;}
                aoq_wire::ServerSetup server;status=aoq_wire::decode_server_setup(p,n,server,error);
                if(status==aoq_wire::ParseStatus::Ok){
                    diag.selected_version=server.selected_version;diag.accept_status=server.accept_status;
                    if(!server.accepted()){fail(Protocol,"server_setup_rejected");return;}
                    diag.setup_accepted=true;diag.stage="setup_accepted_no_media_claim";used=server.consumed;
                }
            }else if(!diag.setup_accepted){fail(Protocol,"control_before_server_setup");return;}
            else if(type==aoq_wire::kMediaSetupType){
                aoq_wire::MediaSetup media;status=aoq_wire::decode_media_setup(p,n,media,error);
                if(status==aoq_wire::ParseStatus::Ok){
                    if(!selection(media)){fail(Protocol,"unsupported_media_selection");return;}
                    ++diag.media_setups;used=media.consumed;
                }
            }else if(type==aoq_wire::kNotifyType){
                aoq_wire::Notify note;status=aoq_wire::decode_notify(p,n,note,error);
                if(status==aoq_wire::ParseStatus::Ok){
                    ++diag.notifications;used=note.consumed;
                    diagnostic_text(note.tag,diag.last_notify_tag,sizeof(diag.last_notify_tag));
                    if(options.trace_metadata){std::printf("AOQ_NOTIFY type=%u\n",note.type);metadata_json("notify",note.payload.data(),note.payload.size());}
                }
            }else if(type==aoq_wire::kGoAwayType){
                aoq_wire::GoAway away;status=aoq_wire::decode_goaway(p,n,away,error);
                if(status!=aoq_wire::ParseStatus::Ok){fail(Protocol,"goaway_decode_failed");return;}
                diag.goaway_received=true;diag.goaway_reason_length=static_cast<uint32_t>(away.reason_phrase.size());
                diagnostic_text(away.reason_phrase,diag.goaway_reason,sizeof(diag.goaway_reason));
                ++diag.control_frames;
                if(consume(s,away.consumed))fail(Protocol,"server_goaway");
                return;
            }else{fail(Protocol,"unknown_control_type");return;}
            if(status!=aoq_wire::ParseStatus::Ok||!used){fail(Protocol,"control_decode_failed");return;}
            if(++diag.control_frames>256){fail(Limit,"control_count_limit");return;}
            if(!consume(s,used))return;
        }
        if(s.window.complete())fail(Protocol,s.window.size()?"truncated_control_fin":"control_stream_fin");
    }
    bool dispatch_end(const End& event){
        if(callbacks.end){const int e=callbacks.end(callbacks.userdata,event);if(e){fail(e>0&&e<=24?e:Protocol,"end_callback_rejected");return false;}}
        return !bounds();
    }
    void objects(Rx& s,unsigned& budget){
        if(!diag.setup_accepted)return;
        if(!s.header_read){
            auto status=aoq_wire::decode_subgroup_header(s.window.data(),s.window.size(),s.header);
            if(status==aoq_wire::ParseStatus::Invalid){fail(Protocol,"subgroup_header_invalid");return;}
            if(status==aoq_wire::ParseStatus::NeedMore){if(s.window.complete())fail(Protocol,"truncated_subgroup_fin");return;}
            if(s.header.track_alias!=302&&s.header.track_alias!=202&&s.header.track_alias!=402){fail(Protocol,"unexpected_track_alias");return;}
            if(!consume(s,s.header.consumed))return;
            s.header_read=true;
            if(options.trace_metadata)std::printf("AOQ_HEADER stream=%llu alias=%llu eog=%d props=%d\n",static_cast<unsigned long long>(s.id),static_cast<unsigned long long>(s.header.track_alias),s.header.end_of_group,s.header.has_properties);
        }
        // Cross-stream QUIC order is not control order. Retain Audio until the
        // actual server selection is parsed; never infer readiness from setup.
        if(s.header.track_alias==202&&!diag.pcm24k_selected&&!diag.opus48k_selected)return;
        while(!diag.error&&s.window.size()&&budget){
            if(bounds())return;
            if(s.terminal){fail(Protocol,"object_after_terminal");return;}
            aoq_wire::ObjectView object;
            auto cursor=s.cursor;
            auto status=aoq_wire::decode_object(s.window.data(),s.window.size(),s.header.has_properties,cursor,object);
            if(status==aoq_wire::ParseStatus::NeedMore){if(s.window.complete())fail(Protocol,"truncated_object_fin");return;}
            if(status==aoq_wire::ParseStatus::Invalid){fail(Protocol,"object_invalid");return;}
            aoq_wire::AudioProperties audio,normalized;
            size_t audio_bytes=object.payload_size;
            if(s.header.track_alias==202&&object.payload_size){
                if(aoq_wire::decode_audio_properties(object.properties,object.properties_size,audio)!=aoq_wire::ParseStatus::Ok)
                    {fail(Protocol,"audio_properties_invalid");return;}
                if(diag.opus48k_selected){
                    if(!aoq_wire::opus48_to_pcm24_properties(audio,normalized)){fail(Protocol,"opus_duration_invalid");return;}
                    audio_bytes=normalized.duration_samples*2;
                }else if(!aoq_wire::pcm_payload_size_matches(audio,1,audio_bytes)){fail(Protocol,"pcm_object_invalid");return;}
            }
            if(s.header.track_alias==202&&object.payload_size&&object.status!=3&&object.status!=4&&
               callbacks.audio_ready&&!callbacks.audio_ready(callbacks.userdata,audio_bytes)){
                ++diag.audio_backpressure;return;
            }
            if(++object_count>object_limit()){fail(Limit,"object_count_limit");return;}
            --budget;
            if(!s.cursor.has_previous&&s.header.subgroup_id_mode==1)s.header.subgroup=object.id;
            Object view{s.id,s.header.group,s.header.subgroup,object.id,object.status,
                        {object.properties,object.properties_size},{},{object.payload,object.payload_size}};
            if(!object.payload_size&&options.trace_metadata)std::printf("AOQ_EMPTY stream=%llu object=%llu status=%llu properties=%u\n",static_cast<unsigned long long>(s.id),static_cast<unsigned long long>(object.id),static_cast<unsigned long long>(object.status),static_cast<unsigned>(object.properties_size));
            if(!user_data(view.properties,view.opaque_userdata)){fail(Protocol,"object_properties_invalid");return;}
            if(object.status==3||object.status==4){
                s.terminal=true;s.group_ended=object.status==3;++diag.terminal_objects;
                End end{s.id,s.header.track_alias,s.header.group,s.header.subgroup,object.id,object.status,
                        false,object.status==3,object.status==4};
                if(!dispatch_end(end))return;
            }else if(object.payload_size){
                int result=0;
                if(s.header.track_alias==302){
                    if(object.payload_size>json_limit()||object.payload_size>json_total_limit()-diag.json_rx_bytes){fail(Limit,"json_receive_total_limit");return;}
                    if(!json_object(object.payload,object.payload_size)){fail(JsonError,"data_json_invalid");return;}
                    ++diag.data_objects;diag.json_rx_bytes+=object.payload_size;
                    if(options.trace_metadata)metadata_json("data",object.payload,object.payload_size);
                    if(callbacks.data)result=callbacks.data(callbacks.userdata,view);
                }else if(s.header.track_alias==202){
                    std::vector<uint8_t> normalized_properties;
                    if(diag.opus48k_selected){
                        if(!decoded_pcm){decoded_pcm=buffer(5760);if(!decoded_pcm){fail(Memory,"opus_pcm_memory");return;}}
                        if(!s.opus){
                            esp_opus_dec_cfg_t config{};config.sample_rate=24000;config.channel=1;
                            config.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_120_MS;config.self_delimited=false;
                            if(esp_opus_dec_open(&config,sizeof(config),&s.opus)!=ESP_AUDIO_ERR_OK){fail(Memory,"opus_decoder_open");return;}
                        }
                        esp_audio_dec_in_raw_t raw{};raw.buffer=const_cast<uint8_t*>(object.payload);raw.len=object.payload_size;
                        esp_audio_dec_out_frame_t pcm{};pcm.buffer=decoded_pcm.get();pcm.len=5760;
                        esp_audio_dec_info_t info{};const auto began=esp_timer_get_time();
                        const auto error=esp_opus_dec_decode(s.opus,&raw,&pcm,&info);
                        const auto decoded_us=uint32_t(esp_timer_get_time()-began);
                        diag.audio_decode_max_us=std::max(diag.audio_decode_max_us,decoded_us);
                        diag.audio_decode_total_us+=decoded_us;
                        if(error!=ESP_AUDIO_ERR_OK || raw.consumed!=object.payload_size || pcm.decoded_size!=audio_bytes ||
                           info.sample_rate!=24000 || info.channel!=1 || info.bits_per_sample!=16){fail(Protocol,"opus_decode_mismatch");return;}
                        if(!aoq_wire::encode_pcm_properties(normalized,normalized_properties)){fail(Protocol,"opus_normalize_failed");return;}
                        view.properties={normalized_properties.data(),normalized_properties.size()};
                        view.payload={decoded_pcm.get(),pcm.decoded_size};
                    }
                    ++diag.audio_objects;diag.audio_rx_bytes+=object.payload_size;diag.last_audio_ms=now_ms();
                    diag.audio_decoded_bytes+=view.payload.size;
                    if(options.trace_metadata&&diag.audio_objects==1){std::printf("AOQ_AUDIO userdata=%u\n",static_cast<unsigned>(view.opaque_userdata.size));metadata_json("audio-userdata",view.opaque_userdata.data,view.opaque_userdata.size);}
                    if(callbacks.audio)result=callbacks.audio(callbacks.userdata,view);
                }else {++diag.command_objects;if(options.trace_metadata)metadata_json("command",object.payload,object.payload_size);} // No assumed model semantics for command_out.
                if(result){fail(result>0&&result<=24?result:Protocol,"object_callback_rejected");return;}
                if(bounds())return;
            }
            if(!consume(s,object.consumed))return;
            s.cursor=cursor;
        }
        if(!diag.error&&s.window.drained()){
            End end{s.id,s.header.track_alias,s.header.group,s.header.subgroup,s.cursor.previous,0,
                    true,s.header.end_of_group&&!s.group_ended,false};
            if(!dispatch_end(end))return;
            if(retired_count==peer_limit()){fail(Limit,"retired_stream_limit");return;}
            retired[retired_count++]={s.id,s.window.consumed()};++diag.stream_fins;
            for(auto& entry:streams)if(entry.get()==&s){entry.reset();--diag.active_streams;break;}
        }
    }
    void parse_streams(unsigned& budget){
        if(auto* control=find(0))controls(*control);
        for(size_t i=0;i<streams.size()&&!diag.error&&budget;++i)if(streams[i]&&streams[i]->id!=0)objects(*streams[i],budget);
    }
    bool write_part(uint64_t id,const uint8_t* p,size_t size,size_t& offset){
        const auto n=quic->WriteRawStream(id,p+offset,size-offset);
        if(n<0||static_cast<size_t>(n)>size-offset){fail(Network,"stream_write_failed");return false;}
        offset+=static_cast<size_t>(n);return n>0;
    }
    void transmit(){
        if(!diag.tls_verified||diag.error)return;
        if(control_id<0){
            control_id=quic->OpenRawStream(false);
            if(control_id!=0){fail(Protocol,"control_stream_open_failed");return;}
            if(!add(0))return;
        }
        for(unsigned burst=0;burst<16&&setup_offset<setup.size();++burst)
            if(!write_part(0,setup.data(),setup.size(),setup_offset))return;
        if(setup_offset<setup.size()||!diag.setup_accepted)return;
        if(!setup.empty()){wipe(setup.data(),setup.size());setup.clear();}
        if(!queue_count){transmit_video();return;}
        if(data_id<0){
            data_id=quic->OpenRawStream(true);
            if(data_id<0){fail(Protocol,"data_stream_open_failed");return;}
        }
        if(tx_header_offset<tx_header.size()&&!write_part(static_cast<uint64_t>(data_id),tx_header.data(),tx_header.size(),tx_header_offset))return;
        if(tx_header_offset<tx_header.size())return;
        for(unsigned burst=0;burst<16&&queue_count&&!diag.error;++burst){
            auto& item=queue[queue_read];
            if(!write_part(static_cast<uint64_t>(data_id),item.bytes.get(),item.size,item.offset))return;
            if(item.offset==item.size){
                queue_bytes-=item.size;wipe(item.bytes.get(),item.size);item=Tx{};
                queue_read=(queue_read+1)%queue.size();--queue_count;++diag.sent_objects;
                diag.queue_bytes=queue_bytes;
            }
        }
        if(!queue_count)transmit_video();
    }
    void transmit_video(){
        if(!video.bytes||!diag.jpeg_selected||diag.error)return;
        if(video_id<0){
            video_id=quic->OpenRawStream(true);
            if(video_id<0){fail(Protocol,"video_stream_open_failed");return;}
        }
        // At most one packet per pump pass. Audio is sent before poll() and
        // Data has priority here; a large JPEG cannot monopolize one loop.
        if(video.offset<video.size){
            if(!write_part(uint64_t(video_id),video.bytes.get(),video.size,video.offset))return;
        }
        if(video.offset==video.size&&quic->CanSendRawPacket()){
            if(!quic->FinishRawStream(uint64_t(video_id))){fail(Network,"video_stream_fin_failed");return;}
            wipe(video.bytes.get(),video.size);video=Tx{};video_id=-1;
            ++diag.video_frames_sent;diag.video_pending=false;
        }
    }
    int start(p4_aoq_kind_t kind){
        started=now_ms();deadline=started+options.lifetime_ms;
        if(options.deadline_ms)deadline=std::min(deadline,options.deadline_ms);
        diag.closed=false;diag.stage="allocating";
        if(bounds())return diag.error;
        const auto available=deadline-now_ms();
        if(available<1000)return fail(Timeout,"allocation_deadline");
        std::unique_ptr<p4_aoq_allocation_t,AllocationDelete> allocation(
            static_cast<p4_aoq_allocation_t*>(heap_caps_calloc(1,sizeof(p4_aoq_allocation_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
        if(!allocation)return fail(Memory,"allocation_memory");
        diag.allocation_prefetched=options.use_prefetched_allocation &&
            p4_aoq_prefetch_take(kind,allocation.get(),options.setup_timeout_ms);
        int error=diag.allocation_prefetched?0:p4_aoq_allocate(kind,allocation.get(),static_cast<unsigned>(std::min<int64_t>(options.allocation_timeout_ms,available)));
        diag.allocation_ms=now_ms()-started;
        if(error)return fail(error,"allocation_failed");
        if(bounds())return diag.error;
        token_deadline=allocation->received_at_ms+static_cast<int64_t>(allocation->expires_in_seconds)*1000;
        setup_deadline=std::min(deadline,now_ms()+options.setup_timeout_ms);
        if(bounds())return diag.error;
        if(!allocation->endpoint_count||strlen(allocation->sid)<3)return fail(Protocol,"allocation_missing_endpoint_or_sid");
        const auto& endpoint=allocation->endpoints[0];sockaddr_storage address{};socklen_t size=0;int family=AF_INET;
        auto* v4=reinterpret_cast<sockaddr_in*>(&address);
        if(inet_pton(AF_INET,endpoint.host,&v4->sin_addr)==1){v4->sin_family=AF_INET;v4->sin_port=htons(endpoint.port);size=sizeof(*v4);}
#if LWIP_IPV6
        else if(inet_pton(AF_INET6,endpoint.host,&reinterpret_cast<sockaddr_in6*>(&address)->sin6_addr)==1){
            auto* v6=reinterpret_cast<sockaddr_in6*>(&address);family=AF_INET6;v6->sin6_family=AF_INET6;v6->sin6_port=htons(endpoint.port);size=sizeof(*v6);
        }
#endif
        else return fail(Dns,"numeric_relay_required");
        fd=::socket(family,SOCK_DGRAM,IPPROTO_UDP);
        if(fd<0||::connect(fd,reinterpret_cast<sockaddr*>(&address),size)<0)return fail(Network,"udp_connect_failed");
        int nonblocking=1;if(ioctl(fd,FIONBIO,&nonblocking)<0)return fail(Network,"udp_nonblocking_failed");
        packet=buffer(4096);if(!packet)return fail(Memory,"packet_memory");
        if(!start_receiver())return diag.error;
        std::string encode_error;
        offered=aoq_wire::pcm_track_profile(kind!=P4_AOQ_TTS,kind!=P4_AOQ_ASR);
        if(options.opus_downlink)for(auto& track:offered)if(track.alias==202){
            track.media[0].codec=10;track.media[0].sample_rate_hz=48000;
        }
        if(options.video_width)offered.insert(offered.begin(),aoq_wire::jpeg_track(options.video_width,options.video_height));
        // Native audio currently implements single PCM only, not RED bundles.
        for(auto& track:offered)if(track.alias==201)track.media.resize(1);
        if(!aoq_wire::encode_client_setup(allocation->token,offered,setup,encode_error))return fail(Protocol,"client_setup_encode_failed");
        setup.insert(setup.begin(),{0x6f,0x00});
        aoq_wire::SubgroupHeader h;h.track_alias=301;h.group=0;h.subgroup=0;h.priority=128;
        if(!aoq_wire::encode_subgroup_header(h,tx_header))return fail(Protocol,"data_header_encode_failed");
        esp_http3::QuicConfig cfg;cfg.application_mode=esp_http3::QuicApplicationMode::kRaw;
        cfg.port=endpoint.port;cfg.allow_unverified_peer=false;cfg.enable_debug=false;
        cfg.trusted_leaf_sha256.assign(allocation->fingerprint_sha256,allocation->fingerprint_sha256+32);
        if(!configure_transport_identity(cfg,*allocation,endpoint))return fail(Protocol,"transport_identity_invalid");
        cfg.handshake_timeout_ms=8000;cfg.idle_timeout_ms=30000;
        cfg.enable_datagram=kind!=P4_AOQ_TTS;cfg.max_datagram_frame_size=1200;
        // Keep compressed delivery independent of decoder speed. The provider
        // can generate a long answer faster than this CPU decodes it; received
        // compressed media needs bounded space before playback catches up.
        cfg.max_stream_data=options.conversation?ConversationWindowSize:WindowSize;
        cfg.max_data=cfg.max_stream_data*StreamCount;cfg.max_streams_bidi=1;cfg.max_streams_uni=peer_limit();
        // 180 one-frame video streams plus Data/control and up to 256 peer
        // streams must fit the same connection. Keep all final-size records
        // until retirement, so a late duplicate never creates a new stream.
        cfg.max_raw_stream_records=options.conversation?512:64;
        quic.reset(new(std::nothrow) esp_http3::QuicConnection([this](const uint8_t* p,size_t n){
            if(diag.error)return -1;
            if(n>packet_limit()-diag.tx_bytes){fail(Limit,"packet_transmit_limit");return -1;}
            const int sent=send(fd,p,n,0);if(sent>0){++diag.tx_packets;diag.tx_bytes+=static_cast<size_t>(sent);}return sent;
        },cfg));
        if(!quic)return fail(Memory,"quic_memory");
        quic->SetOnConnected([this]{diag.tls_verified=true;diag.stage="tls_verified_waiting_setup";});
        quic->SetOnDisconnected([this](int code,const std::string&){if(!closing)diag.quic_error=code;});
        quic->SetOnStreamReset([this](int id,uint64_t code){if(!closing)peer_reset(static_cast<uint64_t>(id),code);});
        quic->SetOnStreamStopSending([this](int id,uint64_t){
            if(!closing&&(id==control_id||id==data_id||id==video_id))fail(Network,"peer_stopped_send_stream");
        });
        quic->SetOnRawStreamFrame([this](uint64_t id,uint64_t offset,const uint8_t* p,size_t n,bool fin){incoming(id,offset,p,n,fin);});
        quic->SetOnDatagram([this](const uint8_t* p,size_t n){
            aoq_wire::DatagramHeader header;aoq_wire::ObjectView object;
            if(aoq_wire::decode_datagram(p,n,header,object)!=aoq_wire::ParseStatus::Ok){fail(Protocol,"datagram_object_invalid");return;}
            // The only negotiated downstream Audio mode is reliable Segment.
            // No datagram may bypass its ordered buffer or playback identity.
            if(header.track_alias==402){
                ++diag.command_objects;
                if(++diag.command_datagrams_received==1)
                    std::printf("AOQ_COMMAND_DATAGRAM received bytes=%u\n",static_cast<unsigned>(n));
                if(options.trace_metadata&&diag.command_objects<=8)metadata_json("command-datagram",object.payload,object.payload_size);
            }
            else fail(Protocol,"unexpected_datagram_track");
        });
        previous_tick=now_ms();diag.stage="quic_handshake";
        if(!quic->StartHandshake())return fail(Network,"quic_start_failed");
        return 0;
    }
    int pump(unsigned max_ms){
        if(pumping)return Busy;
        if(diag.closed)return diag.error?diag.error:Busy;
        pumping=true;
        if(previous_tick&&now_ms()>=previous_tick)
            diag.poll_gap_max_ms=std::max(diag.poll_gap_max_ms,uint32_t(now_ms()-previous_tick));
        const auto until=std::min(deadline,now_ms()+max_ms);
        do{
            if(bounds())break;
            // A nonblocking poll still did sixteen decryptions and up to 256
            // object callbacks. On P4 a fast cloud answer could hold the
            // microphone sender for over 100 ms. Return between packets and
            // bound object callbacks; retained bytes and QUIC receive credit
            // provide backpressure without dropping PCM. Drain UDP before
            // decoding: eight buffered Opus frames can spend the entire packet
            // budget, starving ACKs and new packets on every subsequent poll.
            const bool receive_backlog=receive_queue && receive_queue->pending()>=32;
            unsigned object_budget=options.conversation?(receive_backlog?2:8):128;
            bool udp_empty=false;diag.receive_drained=false;
            transmit();if(diag.error)break;
            // Under a burst, spend more of this pass on decrypt/ACK and less
            // on decoding. A full socket queue previously waited for seconds
            // while the owner decoded eight objects between short RX polls.
            const auto work_until=now_ms()+(receive_backlog?12:4);
            for(unsigned burst=0;burst<(receive_backlog?64u:16u);++burst){
                if(options.conversation&&now_ms()>=work_until)break;
                const int n=receive_packet();
                if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK)fail(Network,"udp_receive_failed");else udp_empty=true;break;}
                if(!n)break;
                if(static_cast<size_t>(n)>packet_limit()-diag.rx_bytes){fail(Limit,"packet_receive_limit");break;}
                ++diag.rx_packets;diag.rx_bytes+=static_cast<size_t>(n);diag.last_udp_ms=now_ms();
                quic->ProcessReceivedData(packet.get(),static_cast<size_t>(n));
                if(diag.error)break;
            }
            if(diag.error)break;
            const auto now=now_ms();quic->OnTimerTick(static_cast<uint32_t>(now-previous_tick));previous_tick=now;
            if(quic->GetState()==esp_http3::ConnectionState::kFailed||quic->GetState()==esp_http3::ConnectionState::kClosed){fail(Network,"quic_closed");break;}
            parse_streams(object_budget);if(diag.error)break;
            transmit();
            const auto stats=quic->GetStats();
            diag.rtt_ms=stats.rtt_ms;
            receiver_metrics();
            diag.declared_lost_datagrams=stats.declared_lost_datagrams;
            diag.receive_drained=udp_empty;
            for(const auto& stream:streams)if(stream&&stream->window.pending())diag.receive_drained=false;
            if(!max_ms||now_ms()>=until||diag.error)break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }while(now_ms()<until);
        pumping=false;
        if(diag.error)shutdown();
        diag.elapsed_ms=now_ms()-started;return diag.error;
    }
    int enqueue(const char* text,size_t n){
        if(diag.closed||!diag.setup_accepted)return Busy;
        if(bounds())return diag.error;
        if(!text||!n||n>json_limit())return Argument;
        if(queue_count==queue.size()||n>QueueMax-queue_bytes){++diag.queue_full;return Busy;}
        if(n>json_total_limit()-diag.json_tx_bytes||next_object>=object_limit())return Limit;
        if(!json_object(reinterpret_cast<const uint8_t*>(text),n))return JsonError;
        std::vector<uint8_t> properties,encoded;WipeVector erase{encoded};auto cursor=tx_cursor;
        if(!aoq_wire::encode_text_properties(static_cast<uint64_t>(esp_timer_get_time()),properties)||
           !aoq_wire::encode_object(next_object,0,true,properties,reinterpret_cast<const uint8_t*>(text),n,cursor,encoded))return Protocol;
        if(encoded.size()>QueueMax-queue_bytes){++diag.queue_full;return Busy;}
        Buffer bytes=buffer(encoded.size());if(!bytes)return Memory;
        memcpy(bytes.get(),encoded.data(),encoded.size());
        auto& item=queue[queue_write];item.bytes=std::move(bytes);item.size=encoded.size();item.offset=0;
        queue_write=(queue_write+1)%queue.size();++queue_count;queue_bytes+=encoded.size();
        tx_cursor=cursor;++next_object;++diag.queued_objects;diag.queue_bytes=queue_bytes;diag.json_tx_bytes+=n;
        return 0;
    }
    int pcm(const int16_t* samples,size_t count,uint64_t timestamp){
        if(diag.closed||!diag.setup_accepted||!diag.pcm16k_input_selected)return Busy;
        if(bounds())return diag.error;
        if(!samples||!count||count>320||timestamp>((uint64_t(1)<<62)-1)||
           (diag.audio_datagrams_sent&&timestamp<=audio_timestamp))return Argument;
        if(diag.audio_tx_samples+count>uint64_t(16000)*(options.conversation?180:30)||next_audio_group>=object_limit())return Limit;
        std::vector<uint8_t> props,encoded;WipeVector erase{encoded};
        aoq_wire::AudioProperties properties;properties.timestamp_us=timestamp;
        properties.sample_cursor=diag.audio_tx_samples;properties.duration_samples=static_cast<uint32_t>(count);
        aoq_wire::DatagramHeader header;header.group=next_audio_group;header.end_of_group=true;
        if(!aoq_wire::encode_pcm_properties(properties,props)||
           !aoq_wire::encode_datagram(header,props,reinterpret_cast<const uint8_t*>(samples),count*2,encoded))return Protocol;
        if(encoded.size()>quic->GetMaxDatagramSize())return fail(Protocol,"audio_datagram_not_negotiated");
        if(!quic->CanSendDatagram(encoded.size()))return Busy;
        if(!quic->SendDatagram(encoded.data(),encoded.size()))return fail(Network,"audio_datagram_send_failed");
        ++next_audio_group;++diag.audio_datagrams_sent;diag.audio_tx_samples+=count;audio_timestamp=timestamp;
        return 0;
    }
    int jpeg(const uint8_t* bytes,size_t size,uint64_t timestamp){
        if(diag.closed||!diag.setup_accepted||!diag.jpeg_selected)return Busy;
        if(bounds())return diag.error;
        if(video.bytes)return Busy;
        if(!bytes||!size||size>65536||timestamp>((uint64_t(1)<<62)-1)||
           (diag.video_frames_queued&&(timestamp<=video_timestamp||timestamp-video_timestamp<1000000)))return Argument;
        if(diag.video_frames_queued>=(options.conversation?180u:30u)||size>uint64_t(options.conversation?12:2)*1024*1024-diag.video_tx_bytes)return Limit;
        std::vector<uint8_t> encoded;WipeVector erase{encoded};
        if(!aoq_wire::encode_jpeg_stream(bytes,size,options.video_width,options.video_height,timestamp,diag.video_frames_queued,encoded))return Protocol;
        video.bytes=buffer(encoded.size());if(!video.bytes)return Memory;
        std::memcpy(video.bytes.get(),encoded.data(),encoded.size());video.size=encoded.size();video.offset=0;
        ++diag.video_frames_queued;diag.video_tx_bytes+=size;diag.video_pending=true;video_timestamp=timestamp;return 0;
    }
};

Session::Session()=default;
Session::~Session()=default;
int Session::open(p4_aoq_kind_t kind,const Options& options,const Callbacks& callbacks){
    const uint32_t cancel_snapshot=cancel_epoch_.load(std::memory_order_relaxed);
    if(impl_&&!impl_->diag.closed)return Busy;
    if(kind<P4_AOQ_AUDIO||kind>P4_AOQ_OMNI||options.lifetime_ms<1000||options.lifetime_ms>(options.conversation?180000u:30000u)||
       options.allocation_timeout_ms<1000||options.allocation_timeout_ms>10000||
       !options.setup_timeout_ms||options.setup_timeout_ms>12000||
       (options.deadline_ms&&options.deadline_ms<=now_ms()))return Argument;
    if((options.video_width||options.video_height)&&
       (kind!=P4_AOQ_OMNI||!options.video_width||options.video_width>1920||!options.video_height||options.video_height>1080))return Argument;
    impl_.reset(new(std::nothrow) Impl(cancel_epoch_,cancel_snapshot));if(!impl_)return Memory;
    impl_->options=options;impl_->callbacks=callbacks;
    int error=impl_->start(kind);
    while(!error&&!impl_->diag.setup_accepted)error=impl_->pump(20);
    if(error)impl_->shutdown();
    return error;
}
int Session::poll(unsigned max_ms){if(max_ms>50)return Argument;return impl_?impl_->pump(max_ms):Busy;}
int Session::send_json(const char* p,size_t n){return impl_?impl_->enqueue(p,n):Busy;}
int Session::send_pcm16(const int16_t* p,size_t n,uint64_t timestamp){return impl_?impl_->pcm(p,n,timestamp):Busy;}
int Session::send_jpeg(const uint8_t* p,size_t n,uint64_t timestamp){return impl_?impl_->jpeg(p,n,timestamp):Busy;}
void Session::cancel(){cancel_epoch_.fetch_add(1,std::memory_order_relaxed);}
void Session::close(){
    if(!impl_)return;
    if(impl_->pumping){impl_->close_requested=true;return;}
    impl_->shutdown();
}
Diagnostics Session::diagnostics()const{
    if(!impl_)return Diagnostics{};
    auto result=impl_->diag;
    for(const auto& s:impl_->streams)if(s){
        result.contiguous_bytes+=s->window.size();
        result.out_of_order_span+=s->window.out_of_order_span();
    }
    return result;
}
} // namespace aoq
