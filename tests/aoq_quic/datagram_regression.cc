// Real component DATAGRAM admission/build/accounting methods. Only packet
// encryption, network send, clock and loss accounting are deterministic mocks.
#include "quic/quic_frame.h"
#include "quic/quic_varint.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace esp_http3::memory {
void* Allocate(size_t n,size_t) noexcept{return std::malloc(n);}
void Deallocate(void* p) noexcept{std::free(p);}
bool UsesPsram() noexcept{return false;}
}
namespace esp_http3::quic {
struct FakeCid {size_t size=8;size_t Length()const{return size;}};
size_t Build1RttPacket(const FakeCid& cid,uint64_t,bool,bool,const uint8_t* p,size_t n,int,uint8_t* out,size_t cap){
    const size_t total=1+cid.Length()+4+16+n;
    if(total>cap)return 0;std::memset(out,0,total);std::memcpy(out+total-n,p,n);return total;
}
}
namespace esp_http3 {
class TestConnection {
public:
    struct {bool enable_datagram=true;uint32_t max_datagram_frame_size=65535;}config_;
    uint64_t peer_max_datagram_frame_size_=65535,current_time_us_=100;
    quic::FakeCid dcid_;
    bool connected=true,send_ok=true;size_t sent=0,sends=0;
    std::array<uint8_t,1500> packet_buf_{},frame_buf_{};
    struct Crypto {bool ready=true;bool HasApplicationKeys()const{return ready;}int GetKeyPhase()const{return 0;}int GetClientAppSecrets()const{return 0;}}crypto_;
    struct Tracker {
        uint64_t pn=0;unsigned records=0;size_t bytes=0;
        uint64_t AllocatePacketNumber(){return pn++;}
        void OnPacketSent(uint64_t,uint64_t,size_t n,bool ack,const std::vector<uint8_t>& frames,uint64_t stream){
            assert(ack&&frames.empty()&&stream==UINT64_MAX);++records;bytes+=n;
        }
    }app_tracker_;
    struct Loss {
        struct Cc {size_t available=20000;bool CanSend(size_t n)const{return n<=available;}}cc;
        unsigned records=0;size_t bytes=0;
        const Cc& GetCongestionController()const{return cc;}
        void OnPacketSent(uint64_t,uint64_t,size_t n,bool ack){assert(ack);++records;bytes+=n;}
    }loss_detector_;
    bool IsConnected()const{return connected;}
    bool SendPacket(const uint8_t*,size_t n){++sends;sent=n;return send_ok;}
    bool CanSendDatagram(size_t)const;
    bool SendDatagram(const uint8_t*,size_t);
    size_t GetMaxDatagramSize()const;
    bool IsDatagramAvailable()const;
};
#include "datagram_methods.inc"
}
using namespace esp_http3;
int main(){
    TestConnection c;std::vector<uint8_t> payload(1501,7);
    assert(c.GetMaxDatagramSize()==1168);
    assert(c.CanSendDatagram(1168)&&!c.CanSendDatagram(1169));
    assert(c.SendDatagram(payload.data(),1168)&&c.sent==1200);
    assert(c.app_tracker_.records==1&&c.loss_detector_.records==1&&c.loss_detector_.bytes==1200);
    assert(!c.SendDatagram(payload.data(),1169)&&c.sends==1);
    assert(!c.SendDatagram(nullptr,1));
    c.connected=false;assert(!c.CanSendDatagram(1));c.connected=true;
    c.crypto_.ready=false;assert(!c.CanSendDatagram(1));c.crypto_.ready=true;
    c.config_.enable_datagram=false;assert(c.GetMaxDatagramSize()==0);c.config_.enable_datagram=true;
    c.peer_max_datagram_frame_size_=0;assert(c.GetMaxDatagramSize()==0);
    unsigned cases=0;
    for(size_t cid:{size_t(0),size_t(8),size_t(20)})for(uint64_t limit:{0ull,1ull,2ull,3ull,64ull,65ull,66ull,67ull,1200ull,65535ull,0x100000001ull}){
        c.dcid_.size=cid;c.peer_max_datagram_frame_size_=limit;
        const size_t maximum=c.GetMaxDatagramSize();
        if(maximum){
            assert(1+quic::VarintEncodedSize(maximum)+maximum<=limit);
            assert(1+cid+4+16+1+quic::VarintEncodedSize(maximum)+maximum<=1200);
            assert(!c.CanSendDatagram(maximum+1));
            assert(c.SendDatagram(payload.data(),maximum)&&c.sent<=1200);
        }else assert(!c.CanSendDatagram(1));
        ++cases;
    }
    c=TestConnection{};c.loss_detector_.cc.available=10;assert(!c.CanSendDatagram(640)&&!c.SendDatagram(payload.data(),640));
    assert(c.sends==0&&c.app_tracker_.records==0&&c.loss_detector_.records==0);
    c.loss_detector_.cc.available=20000;c.send_ok=false;assert(!c.SendDatagram(payload.data(),640));
    assert(c.sends==1&&c.app_tracker_.records==0&&c.loss_detector_.records==0);
    std::printf("PASS DATAGRAM real-method tests: %u size/CID cases, congestion, no-retransmit accounting, failed send\n",cases);
}
