// Actual StartHandshake/generator/Retry dispatcher bodies. RNG, transport,
// key exchange and Retry integrity parser are deterministic stand-ins: this
// verifies CID routing/ownership policy, not packet integrity cryptography.
#include "core/quic_connection.h"
#include "quic/quic_packet.h"
#include "quic/quic_frame.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>

namespace esp_http3::memory {
void* Allocate(size_t n,size_t) noexcept { return std::malloc(n); }
void Deallocate(void* p) noexcept { std::free(p); }
bool UsesPsram() noexcept { return false; }
}
namespace esp_http3::quic {
static bool retry_valid=true;
static ConnectionId retry_peer, expected_original;
bool ParseRetryPacket(const uint8_t*,size_t,const ConnectionId& original,
                      PacketInfo*,ConnectionId* peer,Http3Vector<uint8_t>* token) {
    assert(original==expected_original);
    if(!retry_valid)return false;
    *peer=retry_peer;*token={9,8,7};return true;
}
}
namespace esp_http3 {
static const char* TAG="cid_test";
class TestConnection {
public:
    QuicConfig config_;
    ConnectionState state_=ConnectionState::kIdle;
    bool trust_store_ready_=true;
    uint64_t handshake_start_time_us_=0,current_time_us_=0,last_activity_time_us_=0;
    unsigned packets_received_=0,bytes_received_=0,rng_calls=0,sends=0;
    quic::ConnectionId scid_,dcid_,initial_dcid_,registered_scid;
    quic::TransportParameters local_params_;
    Http3Vector<uint8_t> retry_token_;
    struct Tracker {unsigned resets=0;void Reset(){++resets;}} initial_tracker_;
    struct Crypto {
        quic::ConnectionId derived_from;
        bool SetKeyPair(const uint8_t*,const uint8_t*){return true;}
        void GenerateClientRandom(){}
        bool GenerateKeyPair(){return true;}
        bool DeriveInitialSecrets(const uint8_t* p,size_t n){derived_from.Set(p,n);return true;}
        void ResetTranscript(){}
    } crypto_;
    TestConnection(){config_.hostname="test.example";}
    void GenerateRandom(uint8_t* p,size_t n){for(size_t i=0;i<n;++i)p[i]=static_cast<uint8_t>(0xa0+8*rng_calls+i);++rng_calls;}
    bool GenerateLocalConnectionId(quic::ConnectionId*);
    bool StartHandshake();
    void ProcessReceivedData(uint8_t*,size_t);
    void InitializeLocalConnectionIds(){registered_scid=scid_;}
    bool SendInitialPacket(){++sends;return true;}
    size_t ProcessInitialPacket(uint8_t*,size_t){assert(false);return 0;}
    size_t ProcessHandshakePacket(uint8_t*,size_t){assert(false);return 0;}
    bool Process1RttPacket(uint8_t*,size_t){assert(false);return false;}
    void ProcessBufferedHandshakePackets(){}
};
#include "cid_methods.inc"
}
using namespace esp_http3;
static void CheckSuffix(const quic::ConnectionId& cid,unsigned base,size_t prefix) {
    assert(cid.Length()==8);
    for(size_t i=prefix;i<8;++i)assert(cid.data[i]==base+i);
}
int main(){
    TestConnection empty;assert(empty.StartHandshake());
    CheckSuffix(empty.scid_,0xa0,0);CheckSuffix(empty.dcid_,0xa8,0);
    assert(empty.rng_calls==2&&empty.registered_scid==empty.scid_);
    for(size_t n=1;n<=4;++n){
        TestConnection c;c.config_.initial_cid_prefix={0,0x81,0xfe,0x37};c.config_.initial_cid_prefix.resize(n);
        assert(c.StartHandshake());
        assert(!std::memcmp(c.scid_.Data(),c.config_.initial_cid_prefix.data(),n));
        assert(!std::memcmp(c.dcid_.Data(),c.config_.initial_cid_prefix.data(),n));
        CheckSuffix(c.scid_,0xa0,n);CheckSuffix(c.dcid_,0xa8,n);
        assert(c.initial_dcid_==c.dcid_&&c.crypto_.derived_from==c.dcid_);
        assert(c.local_params_.initial_source_connection_id==c.scid_);
        quic::ConnectionId next;assert(c.GenerateLocalConnectionId(&next));
        assert(!std::memcmp(next.Data(),c.config_.initial_cid_prefix.data(),n));CheckSuffix(next,0xb0,n);
    }
    for(size_t n:{size_t(5),size_t(8),size_t(20)}){
        TestConnection c;c.config_.initial_cid_prefix.resize(n,1);assert(!c.StartHandshake());
        assert(c.state_==ConnectionState::kFailed&&c.rng_calls==0&&c.sends==0);
        quic::ConnectionId unchanged;unchanged.length=1;unchanged.data[0]=0x5a;
        assert(!c.GenerateLocalConnectionId(&unchanged)&&unchanged.length==1&&unchanged.data[0]==0x5a);
    }
    TestConnection custom;custom.config_.extra_transport_parameters={{0xff0e07,{1}},{0xff0e08,{'s',':','i','d'}},{0xff0e09,{0}}};
    assert(custom.StartHandshake()&&custom.local_params_.extra_transport_parameters==custom.config_.extra_transport_parameters);
    TestConnection invalid;invalid.config_.extra_transport_parameters={{0x0f,{1}}};assert(!invalid.StartHandshake());
    assert(invalid.state_==ConnectionState::kFailed&&invalid.rng_calls==0&&invalid.sends==0);
    TestConnection c;c.config_.initial_cid_prefix={0,0x11,0x22,0x33};assert(c.StartHandshake());
    const auto scid=c.scid_,odcid=c.initial_dcid_;
    const uint8_t server[]={0x99,0x87,0x65,0x43,0x21,0x10,0xee,0xdd,0xcc,0xbb};
    quic::retry_peer.Set(server,sizeof(server));quic::expected_original=odcid;
    uint8_t packet[]={0xf0};quic::retry_valid=false;c.ProcessReceivedData(packet,sizeof(packet));
    assert(c.dcid_==odcid&&c.sends==1&&c.rng_calls==2);
    quic::retry_valid=true;c.ProcessReceivedData(packet,sizeof(packet));
    assert(c.dcid_==quic::retry_peer&&c.crypto_.derived_from==quic::retry_peer);
    assert(c.scid_==scid&&c.initial_dcid_==odcid&&c.registered_scid==scid);
    assert(c.sends==2&&c.rng_calls==2&&c.initial_tracker_.resets==1);
    assert((c.retry_token_==Http3Vector<uint8_t>{9,8,7}));
    puts("CID actual handshake/generator/Retry dispatcher: default, 1..4 prefix, random suffix, invalid lengths, server CID preserved PASS (RNG/crypto/Retry parser mock)");
}
