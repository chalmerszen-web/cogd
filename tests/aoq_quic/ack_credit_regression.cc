// Extracts the actual send and both ACK entry paths, with the real sent-packet
// tracker, loss detector and congestion controller. Packet encryption/socket
// submission alone are mocked. This catches credit exhaustion after one cwnd.
#include "core/ack_manager.h"
#include "core/loss_detector.h"
#include "core/frame_processor.h"
#include "quic/quic_frame.h"
#include "quic/quic_varint.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <esp_log.h>
namespace esp_http3::memory {
void* Allocate(size_t n,size_t) noexcept{return std::malloc(n);}
void Deallocate(void* p) noexcept{std::free(p);}
bool UsesPsram() noexcept{return false;}
}
namespace esp_http3::quic {
struct FakeCid {size_t Length()const{return 8;}};
size_t Build1RttPacket(const FakeCid& cid,uint64_t,bool,bool,const uint8_t* p,size_t n,int,uint8_t* out,size_t cap){
    const size_t size=1+cid.Length()+4+16+n;if(size>cap)return 0;
    std::memset(out,0,size);std::memcpy(out+size-n,p,n);return size;
}
}
namespace esp_http3 {
class TestConnection {
public:
    struct {bool enable_datagram=true;uint32_t max_datagram_frame_size=1200;}config_;
    struct {uint8_t ack_delay_exponent=3;}peer_params_;
    struct {bool HasApplicationKeys()const{return true;}int GetKeyPhase()const{return 0;}int GetClientAppSecrets()const{return 0;}}crypto_;
    uint64_t peer_max_datagram_frame_size_=1200,current_time_us_=1000;
    quic::FakeCid dcid_;std::array<uint8_t,1500> packet_buf_{},frame_buf_{};
    SentPacketTracker initial_tracker_,handshake_tracker_,app_tracker_;LossDetector loss_detector_;
    unsigned sends=0,writable=0;size_t packet_bytes=0;
    int close_error=0;
    void Close(int error,const std::string&){close_error=error;}
    bool IsConnected()const{return true;}
    bool SendPacket(const uint8_t*,size_t size){++sends;packet_bytes=size;return true;}
    void NotifyWritable(){++writable;}
    bool CanSendDatagram(size_t)const;bool SendDatagram(const uint8_t*,size_t);
    size_t GetMaxDatagramSize()const;bool IsDatagramAvailable()const;
    void OnFrameAck(const AckFrameData&);
    void ProcessAckFrame(quic::BufferReader*,quic::PacketType);
    void ack(uint64_t largest,uint64_t first,bool parsed=false,quic::PacketType type=quic::PacketType::k1Rtt){
        ++current_time_us_;
        if(!parsed){AckFrameData a;a.largest_ack=largest;a.first_ack_range=first;OnFrameAck(a);return;}
        uint8_t bytes[64];quic::BufferWriter writer(bytes,sizeof(bytes));
        assert(quic::BuildAckFrame(&writer,largest,0,first));
        quic::BufferReader reader(bytes,writer.Offset());ProcessAckFrame(&reader,type);
    }
};
#include "ack_methods.inc"
}
using namespace esp_http3;
int main(){
    {
        TestConnection c;
        const uint8_t truncated[]={0x02,0x01,0x00,0x01,0x00};
        quic::BufferReader reader(truncated,sizeof(truncated));
        c.ProcessAckFrame(&reader,quic::PacketType::k1Rtt);
        assert(c.close_error==0x07);
    }
    const std::array<uint8_t,660> audio{};
    for(bool parsed:{false,true}){
        TestConnection c;auto& cc=c.loss_detector_.GetCongestionController();
        for(unsigned cycle=0;cycle<4;++cycle){
            unsigned burst=0;while(c.CanSendDatagram(audio.size())){assert(c.SendDatagram(audio.data(),audio.size()));++burst;assert(burst<200);}
            assert(burst>=20&&cc.GetBytesInFlight()>0);
            c.ack(c.sends-1,c.sends-1,parsed);assert(cc.GetBytesInFlight()==0&&c.CanSendDatagram(audio.size()));
            const auto available=cc.GetCongestionWindow();c.ack(c.sends-1,c.sends-1,parsed);
            assert(cc.GetBytesInFlight()==0&&available==cc.GetCongestionWindow());
        }
    }
    {
        TestConnection c;auto& cc=c.loss_detector_.GetCongestionController();assert(c.SendDatagram(audio.data(),audio.size()));
        const auto charged=cc.GetBytesInFlight(),available=cc.GetCongestionWindow();
        const auto pn=c.app_tracker_.AllocatePacketNumber();c.app_tracker_.OnPacketSent(pn,c.current_time_us_,100,false);
        c.ack(pn,0);assert(cc.GetBytesInFlight()==charged&&cc.GetCongestionWindow()==available);
        c.ack(pn,pn);assert(cc.GetBytesInFlight()==0);
    }
    {
        TestConnection c;auto& cc=c.loss_detector_.GetCongestionController();unsigned losses=0;
        c.loss_detector_.SetOnLoss([&](const std::vector<SentPacketInfo*>& packets){for(const auto* p:packets){assert(p->frames.empty());++losses;}});
        for(unsigned i=0;i<5;++i)assert(c.SendDatagram(audio.data(),audio.size()));
        c.ack(4,0);assert(losses==2&&cc.GetBytesInFlight()==2*c.packet_bytes);
        const auto flight=cc.GetBytesInFlight(),available=cc.GetCongestionWindow();c.ack(0,0);
        assert(cc.GetBytesInFlight()==flight&&cc.GetCongestionWindow()==available);
    }
    {
        TestConnection c;unsigned pto=0;c.loss_detector_.SetOnPto([&]{++pto;});
        assert(c.SendDatagram(audio.data(),audio.size()));++c.current_time_us_;assert(c.SendDatagram(audio.data(),audio.size()));
        c.ack(0,0);assert(c.loss_detector_.GetTimeUntilNextPto(c.current_time_us_)>0);
        assert(c.loss_detector_.OnTimerTick(c.current_time_us_+10000000)&&pto==1);
    }
    {
        TestConnection c;auto& cc=c.loss_detector_.GetCongestionController();
        c.initial_tracker_.OnPacketSent(0,1000,1200,true);c.loss_detector_.OnPacketSent(0,1000,1200,true);
        c.handshake_tracker_.OnPacketSent(0,1000,300,true);c.loss_detector_.OnPacketSent(0,1000,300,true);
        c.ack(0,0,true,quic::PacketType::kInitial);assert(cc.GetBytesInFlight()==300);
        c.ack(0,0,true,quic::PacketType::kHandshake);assert(cc.GetBytesInFlight()==0);
    }
    {
        TestConnection c;assert(c.SendDatagram(audio.data(),audio.size()));const auto flight=c.loss_detector_.GetCongestionController().GetBytesInFlight();
        c.ack(0,1);assert(c.loss_detector_.GetCongestionController().GetBytesInFlight()==flight);
    }
    std::puts("PASS real ACK/congestion integration: repeated cwnd recovery, duplicates, ACK-only exclusion, loss, DATAGRAM PTO, all packet spaces");
}
