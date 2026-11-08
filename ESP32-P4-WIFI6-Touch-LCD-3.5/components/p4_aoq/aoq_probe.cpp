// Explicit, bounded native connectivity experiment. This does not change the
// normal conversation transport and does not capture or play audio.
#include "p4_aoq.h"
#include "aoq_wire.h"
#include "aoq_transport.h"
#include "aoq_tts_trial.h"
#include "aoq_asr_trial.h"
#include "aoq_dialogue_trial.h"
#include "aoq_tts_play.h"
#include "core/quic_connection.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {
constexpr int kArgument=1,kMemory=19,kProtocol=7,kNetwork=8,kTimeout=15;
constexpr size_t kControlLimit=16384;
int64_t millis(){return esp_timer_get_time()/1000;}

struct AllocationDeleter {
    void operator()(p4_aoq_allocation_t* p) const {
        if(p){p4_aoq_allocation_clear(p);heap_caps_free(p);}
    }
};
struct BufferDeleter {void operator()(uint8_t* p)const{heap_caps_free(p);}};
using Buffer=std::unique_ptr<uint8_t,BufferDeleter>;
struct SecretBytes {
    std::vector<uint8_t>& value;
    ~SecretBytes(){volatile uint8_t* p=value.data();for(size_t i=0;i<value.size();++i)p[i]=0;}
};
struct Socket {
    int fd=-1;
    ~Socket(){if(fd>=0)close(fd);}
};
struct Probe {
    const char* stage="starting";
    int error=0,quic_error=0;
    bool tls_verified=false,setup_received=false,accepted=false;
    uint32_t version=0,accept_status=0;
    unsigned tx_packets=0,rx_packets=0;
    size_t tx_bytes=0,rx_bytes=0,setup_sent=0,control_bytes=0;
    int64_t started=millis(),allocated_ms=0,tls_ms=0,setup_ms=0;
};

// A setup control stream is small. Fail on conflicting retransmissions,
// overflow or conflicting final sizes; never interpret packet order as stream
// order. No raw payload or session credentials are included in diagnostics.
struct ControlBuffer {
    Buffer data{static_cast<uint8_t*>(heap_caps_calloc(kControlLimit,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT))};
    Buffer seen{static_cast<uint8_t*>(heap_caps_calloc(kControlLimit,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT))};
    size_t contiguous=0,highest_end=0,final_size=0;
    bool failed=false,final_seen=false;
    bool ready()const{return data && seen;}
    void insert(uint64_t offset,const uint8_t* bytes,size_t len,bool fin){
        if(!ready() || offset>kControlLimit || len>kControlLimit-offset || (!bytes && len)){
            failed=true;return;
        }
        const size_t end=static_cast<size_t>(offset)+len;
        if((final_seen && end>final_size) ||
           (fin && ((final_seen && end!=final_size) || end<highest_end))){
            failed=true;return;
        }
        if(fin){final_seen=true;final_size=end;}
        highest_end=std::max(highest_end,end);
        for(size_t i=0;i<len;++i){
            size_t at=static_cast<size_t>(offset)+i;
            if(seen.get()[at] && data.get()[at]!=bytes[i]){failed=true;return;}
            data.get()[at]=bytes[i];seen.get()[at]=1;
        }
        while(contiguous<kControlLimit && seen.get()[contiguous])++contiguous;
        // A final frame may precede missing earlier bytes; await reassembly.
    }
};

int run(p4_aoq_kind_t kind,Probe& report){
    std::unique_ptr<p4_aoq_allocation_t,AllocationDeleter> allocation(
        static_cast<p4_aoq_allocation_t*>(heap_caps_calloc(1,sizeof(p4_aoq_allocation_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
    if(!allocation){report.stage="allocation_memory";return kMemory;}
    report.stage="allocating";
    int error=p4_aoq_allocate(kind,allocation.get(),10000);
    report.allocated_ms=millis()-report.started;
    if(error){report.stage="allocation_failed";return error;}
    if(!allocation->endpoint_count){report.stage="no_endpoint";return kProtocol;}
    const int64_t expires_at=allocation->received_at_ms+
        static_cast<int64_t>(allocation->expires_in_seconds)*1000;
    if(millis()>=expires_at){report.stage="allocation_expired";return kTimeout;}
    const auto& endpoint=allocation->endpoints[0];
    sockaddr_storage address{};
    socklen_t address_size=0;
    auto* v4=reinterpret_cast<sockaddr_in*>(&address);
    int family=AF_INET;
    if(inet_pton(AF_INET,endpoint.host,&v4->sin_addr)==1){
        v4->sin_family=AF_INET;v4->sin_port=htons(endpoint.port);address_size=sizeof(*v4);

#if LWIP_IPV6
    } else if(inet_pton(AF_INET6,endpoint.host,&reinterpret_cast<sockaddr_in6*>(&address)->sin6_addr)==1){
        auto* v6=reinterpret_cast<sockaddr_in6*>(&address);
        family=AF_INET6;v6->sin6_family=AF_INET6;v6->sin6_port=htons(endpoint.port);address_size=sizeof(*v6);
#endif
    } else {
        // The allocation currently returns IPs. Do not introduce an unbounded
        // synchronous DNS lookup into this first native handshake experiment.
        report.stage="dns_endpoint_requires_resolver";return 9;
    }
    Socket socket;socket.fd=::socket(family,SOCK_DGRAM,IPPROTO_UDP);
    if(socket.fd<0){report.stage="socket_failed";return kNetwork;}
    if(::connect(socket.fd,reinterpret_cast<sockaddr*>(&address),address_size)<0){
        report.stage="udp_connect_failed";return kNetwork;
    }
    int nonblocking=1;
    if(ioctl(socket.fd,FIONBIO,&nonblocking)<0){report.stage="nonblocking_failed";return kNetwork;}
    ControlBuffer control;
    if(!control.ready()){report.stage="control_memory";return kMemory;}
    std::vector<uint8_t> setup;
    SecretBytes erase_setup{setup};
    std::string wire_error;
    if(!aoq_wire::encode_client_setup(allocation->token,aoq_wire::pcm_track_profile(kind!=P4_AOQ_TTS,kind!=P4_AOQ_ASR),setup,wire_error)){
        report.stage="setup_encode_failed";return kProtocol;
    }
    // Official SDK opens a bidi control stream and sends its varint stream
    // type once before the first framed message. The response on that same
    // stream starts directly with a framed SERVER_SETUP.
    setup.insert(setup.begin(),{0x6f,0x00});
    esp_http3::QuicConfig config;
    config.application_mode=esp_http3::QuicApplicationMode::kRaw;
    config.port=endpoint.port;
    config.trusted_leaf_sha256.assign(allocation->fingerprint_sha256,allocation->fingerprint_sha256+32);
    config.allow_unverified_peer=false;
    if(!aoq::configure_transport_identity(config,*allocation,endpoint)){report.stage="routing_identity_invalid";return kProtocol;}
    config.handshake_timeout_ms=8000;config.idle_timeout_ms=12000;
    config.max_data=65536;config.max_stream_data=32768;
    config.max_streams_bidi=4;config.max_streams_uni=16;
    config.enable_debug=false;
    esp_http3::QuicConnection connection([&](const uint8_t* data,size_t len){
        int n=send(socket.fd,data,len,0);
        if(n>0){++report.tx_packets;report.tx_bytes+=static_cast<size_t>(n);}
        return n;
    },config);
    int64_t stream=-1;
    connection.SetOnConnected([&]{
        report.tls_verified=true;report.tls_ms=millis()-report.started;
        report.stage="tls_verified";
    });
    connection.SetOnDisconnected([&](int code,const std::string&){report.quic_error=code;});
    connection.SetOnRawStreamFrame([&](uint64_t id,uint64_t offset,const uint8_t* data,size_t len,bool fin){
        if(stream>=0 && id==static_cast<uint64_t>(stream))control.insert(offset,data,len,fin);
    });
    report.stage="quic_handshake";
    if(!connection.StartHandshake()){report.stage="quic_start_failed";return 10;}
    Buffer packet{static_cast<uint8_t*>(heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT))};
    if(!packet){report.stage="packet_memory";return kMemory;}
    int64_t previous=millis(),deadline=std::min(previous+12000,expires_at);
    while(millis()<deadline){
        for(unsigned burst=0;burst<16;++burst){
            int n=recv(socket.fd,packet.get(),4096,0);
            if(n<0){
                if(errno!=EAGAIN && errno!=EWOULDBLOCK){report.stage="udp_receive_failed";return kNetwork;}
                break;
            }
            if(!n)break;
            ++report.rx_packets;report.rx_bytes+=static_cast<size_t>(n);
            connection.ProcessReceivedData(packet.get(),static_cast<size_t>(n));
        }
        auto now=millis();connection.OnTimerTick(static_cast<uint32_t>(now-previous));previous=now;
        if(control.failed){report.stage="control_reassembly_failed";return kProtocol;}
        if(connection.GetState()==esp_http3::ConnectionState::kFailed ||
           connection.GetState()==esp_http3::ConnectionState::kClosed){
            report.stage=report.tls_verified?"quic_closed_after_tls":"quic_handshake_failed";return kNetwork;
        }
        if(report.tls_verified){
            if(stream<0){stream=connection.OpenRawStream(false);if(stream<0){report.stage="control_open_failed";return kProtocol;}}
            if(report.setup_sent<setup.size()){
                auto n=connection.WriteRawStream(static_cast<uint64_t>(stream),setup.data()+report.setup_sent,setup.size()-report.setup_sent);
                if(n<0){report.stage="setup_send_failed";return kNetwork;}
                report.setup_sent+=static_cast<size_t>(n);
                report.stage="waiting_server_setup";
            }
            report.control_bytes=control.contiguous;
            if(control.contiguous){
                aoq_wire::ServerSetup server;
                auto parsed=aoq_wire::decode_server_setup(control.data.get(),control.contiguous,server,wire_error);
                if(parsed==aoq_wire::ParseStatus::Invalid){report.stage="server_setup_parse_failed";return kProtocol;}
                if(parsed==aoq_wire::ParseStatus::Ok){
                    report.setup_received=true;report.version=server.selected_version;
                    report.accept_status=server.accept_status;report.accepted=server.accepted();
                    report.setup_ms=millis()-report.started;
                    connection.AcknowledgeRawStreamData(static_cast<uint64_t>(stream),server.consumed);
                    connection.Close();
                    report.stage=report.accepted?"native_setup_accepted_no_media":"server_setup_rejected";
                    return report.accepted?0:kProtocol;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    connection.Close();
    report.stage=millis()>=expires_at?"allocation_expired":
        (report.tls_verified?"server_setup_timeout":"quic_handshake_timeout");
    return kTimeout;
}
}

extern "C" int p4_aoq_probe_command(const char* command,char* output,size_t capacity){
    if(!command || !output || capacity<1024)return kArgument;
    if(!strncmp(command,"input ",6))return p4_aoq_dialogue_input_command(command+6,output,capacity);
    if(!strncmp(command,"tts ",4))return p4_aoq_tts_trial_command(command,output,capacity);
    if(!strncmp(command,"asr ",4))return p4_aoq_asr_trial_command(command,output,capacity);
    if(!strncmp(command,"dialogue ",9))return p4_aoq_dialogue_trial_command(command,output,capacity);
    if(!strncmp(command,"video ",6))return p4_aoq_dialogue_trial_command(command,output,capacity);
    p4_aoq_kind_t kind;
    if(!strcmp(command,"probe audio"))kind=P4_AOQ_AUDIO;
    else if(!strcmp(command,"probe asr"))kind=P4_AOQ_ASR;
    else if(!strcmp(command,"probe tts"))kind=P4_AOQ_TTS;
    else return kArgument;
    Probe r;r.error=run(kind,r);
    snprintf(output,capacity,
        "{\"aoq_probe\":true,\"native_p4\":true,\"media_sent\":false,\"kind\":%u,\"stage\":\"%s\",\"error\":%d,"
        "\"tls_verified\":%s,\"setup_received\":%s,\"setup_accepted\":%s,\"selected_version\":%u,\"accept_status\":%u,"
        "\"quic_error\":%d,\"tx_packets\":%u,\"rx_packets\":%u,\"tx_bytes\":%u,\"rx_bytes\":%u,"
        "\"setup_sent\":%u,\"control_bytes\":%u,\"allocation_ms\":%lld,\"tls_ms\":%lld,\"setup_ms\":%lld,\"elapsed_ms\":%lld}",
        static_cast<unsigned>(kind),r.stage,r.error,r.tls_verified?"true":"false",r.setup_received?"true":"false",r.accepted?"true":"false",
        static_cast<unsigned>(r.version),static_cast<unsigned>(r.accept_status),r.quic_error,r.tx_packets,r.rx_packets,static_cast<unsigned>(r.tx_bytes),static_cast<unsigned>(r.rx_bytes),
        static_cast<unsigned>(r.setup_sent),static_cast<unsigned>(r.control_bytes),static_cast<long long>(r.allocated_ms),
        static_cast<long long>(r.tls_ms),static_cast<long long>(r.setup_ms),static_cast<long long>(millis()-r.started));
    return r.error;
}
extern "C" void p4_aoq_probe_cancel(void){
    p4_aoq_tts_trial_cancel();p4_aoq_asr_trial_cancel();p4_aoq_dialogue_trial_cancel();p4_aoq_tts_play_cancel();
}
