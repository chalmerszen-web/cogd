#include "core/ack_manager.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
namespace esp_http3::memory {
void* Allocate(size_t n,size_t) noexcept{return std::malloc(n);}
void Deallocate(void* p) noexcept{std::free(p);}
bool UsesPsram() noexcept{return false;}
}
int main(){
    esp_http3::AckManager ack;
    ack.OnPacketReceived(10,1000,true);
    const auto deadline=ack.GetAckDeadlineUs();
    assert(deadline==26000);
    for(unsigned i=1;i<=3;++i)ack.OnPacketReceived(10+i,1000+i*8000,false);
    std::printf("first_deadline=%llu after_ack_only=%llu due=%d largest=%lld\n",
      (unsigned long long)deadline,(unsigned long long)ack.GetAckDeadlineUs(),
      ack.ShouldSendAck(26000),(long long)ack.GetLargestReceived());
    std::fflush(stdout);
    assert(ack.GetLargestReceived()==13 && ack.HasReceivedPacket(13));
    assert(ack.GetAckDeadlineUs()==deadline && ack.ShouldSendAck(deadline));
    ack.OnAckSent();
    assert(!ack.ShouldSendAck(100000) && !ack.GetAckDeadlineUs());
    ack.OnPacketReceived(14,100000,false);
    assert(!ack.ShouldSendAck(200000));
    ack.OnPacketReceived(15,210000,true);
    assert(ack.GetAckDeadlineUs()==235000 && !ack.ShouldSendAck(234999));
    ack.OnPacketReceived(16,211000,true);
    assert(ack.ShouldSendAck());
    ack.Reset();
    ack.OnPacketReceived(1,0,true);
    assert(ack.GetAckDeadlineUs()==25000 && ack.ShouldSendAck(25000));
    std::puts("PASS pending ACK timer remains bounded under ACK-only traffic");
}
