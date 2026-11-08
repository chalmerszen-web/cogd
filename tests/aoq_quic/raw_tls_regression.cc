// Actual production method bodies, real frame/TLS parsers and SHA256.
// Socket, key exchange, certificate parsing/signature verifier are stand-ins:
// this checks dispatch/authentication POLICY, not cryptographic interoperability.
#include "core/quic_connection.h"
#include "core/flow_controller.h"
#include "quic/quic_crypto.h"
#include "quic/quic_frame.h"
#include "tls/tls_handshake.h"
#include <openssl/sha.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <esp_log.h>

namespace esp_http3::memory {
void* Allocate(size_t n,size_t) noexcept { return std::malloc(n); }
void Deallocate(void* p) noexcept { std::free(p); }
bool UsesPsram() noexcept { return false; }
}
namespace esp_http3::quic {
bool Sha256(const uint8_t* p,size_t n,uint8_t* out) { return SHA256(p,n,out)!=nullptr; }
// Used only by the PSK ClientHello framing test; no PSK crypto claim.
bool HkdfExtract(const uint8_t*,size_t,const uint8_t*,size_t,uint8_t* out) { std::memset(out,0,32);return true; }
bool HkdfExpandLabel(const uint8_t*,size_t,const uint8_t*,size_t,const uint8_t*,size_t,uint8_t* out,size_t n) { std::memset(out,0,n);return true; }
bool HmacSha256(const uint8_t*,size_t,const uint8_t*,size_t,uint8_t* out) { std::memset(out,0,32);return true; }
bool ComputeFinishedVerifyData(const uint8_t*,const uint8_t*,uint8_t* out) { std::memset(out,42,32);return true; }
}
struct FakeCert { int pk=0; };
using mbedtls_pk_sigalg_t = int;
constexpr int MBEDTLS_X509_KU_DIGITAL_SIGNATURE=1,MBEDTLS_PK_SIGALG_NONE=0,MBEDTLS_PK_SIGALG_ECDSA=1,MBEDTLS_PK_SIGALG_RSA_PSS=2,MBEDTLS_MD_SHA256=1;
#define MBEDTLS_OID_SERVER_AUTH "serverAuth"
#define MBEDTLS_OID_SIZE(x) (sizeof(x)-1)
static int ca_result=0, signature_result=0, ca_calls=0;
static void mbedtls_x509_crt_free(FakeCert*) {}
static void mbedtls_x509_crt_init(FakeCert*) {}
static int mbedtls_x509_crt_parse_der(FakeCert*,const uint8_t*,size_t) {return 0;}
static int mbedtls_x509_crt_check_key_usage(FakeCert*,int) {return 0;}
static int mbedtls_x509_crt_check_extended_key_usage(FakeCert*,const char*,size_t) {return 0;}
static int mbedtls_x509_crt_verify(FakeCert*,FakeCert*,void*,const char*,uint32_t* flags,void*,void*) {++ca_calls;*flags=0;return ca_result;}
static size_t mbedtls_pk_get_bitlen(int*) {return 256;}
static int mbedtls_pk_verify_ext(int,int*,int,const uint8_t*,size_t,const uint8_t*,size_t) {return signature_result;}

namespace esp_http3 {
static const char* TAG="RawTest";
class TestConnection {
public:
    QuicConfig config_;
    quic::TransportParameters peer_params_;
    FlowController flow_controller_;
    struct RawStreamState { bool send_closed=false,has_final_size=false; uint64_t final_size=0; };
    std::map<uint64_t,RawStreamState> raw_streams_;
    uint64_t next_raw_bidi_=0,next_raw_uni_=2;
    OnRawStreamFrameCallback on_raw_stream_frame_;
    std::set<uint64_t> reset_streams_,remote_stop_sending_streams_,local_stop_sending_streams_;
    bool connected=true,send_ok=true;
    struct Loss {
        struct Cc {bool ready=true;bool CanSend(size_t bytes)const{assert(bytes==1200);return ready;}} cc;
        const Cc& GetCongestionController()const{return cc;}
    } loss_detector_;
    int close_error=-1;
    std::vector<uint8_t> sent;
    TestConnection() {
        config_.application_mode=QuicApplicationMode::kRaw;
        config_.max_data=8192;config_.max_stream_data=4096;
        config_.max_streams_bidi=4;config_.max_streams_uni=4;
        peer_params_.initial_max_streams_bidi=4;peer_params_.initial_max_streams_uni=4;
        peer_params_.initial_max_stream_data_bidi_remote=4096;
        peer_params_.initial_max_stream_data_bidi_local=4096;
        peer_params_.initial_max_stream_data_uni=4096;
        flow_controller_.Initialize(8192,4096);flow_controller_.OnMaxDataReceived(8192);
    }
    bool IsConnected() const {return connected;}
    void Close(int e,const std::string&) {connected=false;close_error=e;}
    bool SendStreamData(uint64_t id,const uint8_t* p,size_t n,bool) {
        if(n)sent.assign(p,p+n);
        flow_controller_.OnStreamBytesSent(id,n);return send_ok;
    }
    void SendMaxDataFrame() {}
    void SendMaxStreamDataFrame(uint64_t) {}
    int64_t OpenRawStream(bool=false);
    bool CanSendRawPacket() const;
    ssize_t WriteRawStream(uint64_t,const uint8_t*,size_t);
    bool FinishRawStream(uint64_t);
    bool AcknowledgeRawStreamData(uint64_t,size_t);
    void ReceiveRawStreamFrame(uint64_t,uint64_t,const uint8_t*,size_t,bool);
    FakeCert peer_certificate_chain_,trusted_ca_chain_;
    bool psk_accepted_=false,certificate_validated_=false,certificate_verify_validated_=false;
    struct Crypto {
        bool HasApplicationKeys()const{return true;}
        void GetTranscriptHash(uint8_t* p) {std::memset(p,0,32);}
        quic::CryptoSecrets secret;
        const quic::CryptoSecrets& GetServerHandshakeSecrets() {return secret;}
    } crypto_;
    bool ProcessCertificate(const uint8_t*,size_t);
    bool ProcessCertificateVerify(const uint8_t*,size_t);
    bool ProcessServerFinished(const uint8_t*,size_t);
};
#include "raw_tls_methods.inc"
}
using namespace esp_http3;

static std::string ClientAlpn(const uint8_t* p,size_t n) {
    quic::BufferReader r(p,n);uint8_t b;uint16_t v;
    assert(r.ReadUint8(&b)&&b==1&&r.Skip(3));
    assert(r.ReadUint16(&v)&&r.Skip(32)&&r.ReadUint8(&b)&&r.Skip(b));
    assert(r.ReadUint16(&v)&&r.Skip(v)&&r.ReadUint8(&b)&&r.Skip(b));
    assert(r.ReadUint16(&v)&&v==r.Remaining());
    while(r.Remaining()) {uint16_t id,len;assert(r.ReadUint16(&id)&&r.ReadUint16(&len));
        if(id==16) {uint16_t total;assert(r.ReadUint16(&total)&&total+2==len&&r.ReadUint8(&b)&&b+1==total);return std::string(reinterpret_cast<const char*>(r.Current()),b);}
        assert(r.Skip(len));
    } return {};
}
static void AlpnTests() {
    uint8_t out[4096],random[32]{},key[32]{};quic::TransportParameters tp;tp.initial_max_data=1024;
    auto n=tls::BuildClientHello("relay.example",random,key,tp,out,sizeof(out));assert(n&&ClientAlpn(out,n)=="h3");
    n=tls::BuildClientHello("relay.example",random,key,tp,out,sizeof(out),"test-raw-01");assert(n&&ClientAlpn(out,n)=="test-raw-01");
    n=tls::BuildClientHello("",random,key,tp,out,sizeof(out),"ip-relay");assert(n&&ClientAlpn(out,n)=="ip-relay");
    assert(!tls::BuildClientHello("relay.example",random,key,tp,out,sizeof(out),""));
    assert(!tls::BuildClientHello("relay.example",random,key,tp,out,sizeof(out),std::string(256,'a')));
    n=tls::BuildClientHello("relay.example",random,key,tp,out,sizeof(out),std::string(255,'a'));assert(n&&ClientAlpn(out,n)==std::string(255,'a'));
    tls::PskParameters psk;psk.valid=true;psk.identity.ticket={1,2,3};
    n=tls::BuildClientHelloWithPsk("relay.example",random,key,tp,psk,out,sizeof(out),"test-raw-01");assert(n&&ClientAlpn(out,n)=="test-raw-01");
    psk.valid=false;n=tls::BuildClientHelloWithPsk("relay.example",random,key,tp,psk,out,sizeof(out),"fallback");assert(n&&ClientAlpn(out,n)=="fallback");
    puts("ALPN actual ClientHello/default/PSK/fallback/bounds PASS");
}

static std::map<uint64_t,std::vector<uint8_t>> ClientTransportParameters(const uint8_t* p,size_t n) {
    quic::BufferReader r(p,n);uint8_t b;uint16_t v;
    assert(r.ReadUint8(&b)&&b==1&&r.Skip(3));
    assert(r.ReadUint16(&v)&&r.Skip(32)&&r.ReadUint8(&b)&&r.Skip(b));
    assert(r.ReadUint16(&v)&&r.Skip(v)&&r.ReadUint8(&b)&&r.Skip(b));
    assert(r.ReadUint16(&v)&&v==r.Remaining());
    std::map<uint64_t,std::vector<uint8_t>> result;bool found=false;
    while(r.Remaining()) {
        uint16_t type,length;assert(r.ReadUint16(&type)&&r.ReadUint16(&length)&&length<=r.Remaining());
        if(type==57) {
            assert(!found);found=true;quic::BufferReader tp(r.Current(),length);
            while(tp.Remaining()) {uint64_t id,len;assert(tp.ReadVarint(&id)&&tp.ReadVarint(&len)&&len<=tp.Remaining());
                assert(result.emplace(id,std::vector<uint8_t>(tp.Current(),tp.Current()+len)).second);assert(tp.Skip(len));}
        }
        assert(r.Skip(length));
    }
    assert(found);return result;
}
static void ExtraTransportTests() {
    uint8_t out[4096],random[32]{},key[32]{};quic::TransportParameters tp;tp.initial_max_data=1024;
    auto n=tls::BuildClientHello("localhost",random,key,tp,out,sizeof(out),"aoq-quic-01");assert(n);
    const std::vector<uint8_t> baseline(out,out+n);
    // The default standard TP serialization remains the known pre-extension bytes.
    uint8_t raw[2048];auto raw_size=quic::BuildTransportParameters(tp,raw,sizeof(raw));
    const uint8_t expected_default[]={4,2,0x44,0};assert(raw_size==sizeof(expected_default)&&!std::memcmp(raw,expected_default,raw_size));
    std::vector<uint8_t> sid(108,'s');sid[3]=':'; // public synthetic fixture, no live allocation data
    tp.extra_transport_parameters={{0xff0e07,{1}},{0xff0e08,sid},{0xff0e09,{0}}};
    auto verify=[&](size_t length) {assert(length);auto values=ClientTransportParameters(out,length);
        for(const auto& entry:tp.extra_transport_parameters)assert(values.at(entry.first)==entry.second);
        assert(values.size()==4);};
    n=tls::BuildClientHello("localhost",random,key,tp,out,sizeof(out),"aoq-quic-01");verify(n);
    // Opaque bytes are not reinterpreted as integers or C strings (embedded NUL preserved).
    tp.extra_transport_parameters[1].second={0,0xff,':',0,0x80};
    n=tls::BuildClientHello("localhost",random,key,tp,out,sizeof(out),"aoq-quic-01");verify(n);
    tls::PskParameters psk;psk.valid=true;psk.identity.ticket={1,2,3};
    n=tls::BuildClientHelloWithPsk("localhost",random,key,tp,psk,out,sizeof(out),"aoq-quic-01");verify(n);
    const uint64_t max=(uint64_t{1}<<62)-1;
    tp.extra_transport_parameters={{max,std::vector<uint8_t>(1014,0xa5)}}; // 8 ID + 2 length + 1014 value = 1024
    assert(quic::ValidateExtraTransportParameters(tp.extra_transport_parameters));
    n=tls::BuildClientHello("localhost",random,key,tp,out,sizeof(out),"aoq-quic-01");assert(n);
    assert(ClientTransportParameters(out,n).at(max)==tp.extra_transport_parameters[0].second);
    auto reject=[&](quic::ExtraTransportParameters bad) {
        tp.extra_transport_parameters=std::move(bad);std::memset(raw,0xa7,sizeof(raw));
        assert(!quic::ValidateExtraTransportParameters(tp.extra_transport_parameters));
        assert(!quic::BuildTransportParameters(tp,raw,sizeof(raw)));
        assert(std::all_of(raw,raw+sizeof(raw),[](uint8_t v){return v==0xa7;}));
        assert(!tls::BuildClientHello("localhost",random,key,tp,out,sizeof(out),"aoq-quic-01"));
        assert(!tls::BuildClientHelloWithPsk("localhost",random,key,tp,psk,out,sizeof(out),"aoq-quic-01"));
    };
    reject({{max,std::vector<uint8_t>(1015)}});reject({{max+1,{}}});reject({{UINT64_MAX,{}}});
    reject({{0xff0e07,{1}},{0xff0e07,{2}}});
    for(uint64_t id=0;id<=0x20;++id)reject({{id,{}}});reject({{0x2ab2,{}}});
    quic::ExtraTransportParameters eight;for(uint64_t i=0;i<8;++i)eight.push_back({0x100+i,{}});
    tp.extra_transport_parameters=eight;assert(quic::ValidateExtraTransportParameters(eight));
    n=tls::BuildClientHello("localhost",random,key,tp,out,sizeof(out),"aoq-quic-01");assert(n&&ClientTransportParameters(out,n).size()==9);
    eight.push_back({0x108,{}});reject(eight);
    quic::ExtraTransportParameters aggregate;for(uint64_t i=0;i<8;++i)aggregate.push_back({0x100+i,std::vector<uint8_t>(128)});reject(aggregate);
    tp.extra_transport_parameters.clear();n=tls::BuildClientHello("localhost",random,key,tp,out,sizeof(out),"aoq-quic-01");
    assert(n==baseline.size()&&!std::memcmp(out,baseline.data(),n));
    // Separate output/Initial MTU bounds still apply to large hostname/ALPN + extras.
    tp.extra_transport_parameters={{max,std::vector<uint8_t>(1014)}};
    assert(!tls::BuildClientHello(std::string(251,'a'),random,key,tp,out,1500,std::string(255,'b')));
    puts("extra TP actual ClientHello/PSK: opaque AOQ-shaped bytes, default, 8/1024 encoded bounds, duplicates/core IDs/output bounds PASS");
}

static void RawTests() {
    uint8_t bytes[]={0,255,0,42};TestConnection c;
    assert(c.OpenRawStream()==0&&c.OpenRawStream()==4&&c.OpenRawStream(true)==2);
    assert(c.WriteRawStream(0,bytes,4)==4&&c.sent==std::vector<uint8_t>(bytes,bytes+4));
    c.loss_detector_.cc.ready=false;const auto before=c.flow_controller_.GetStreamState(0)->send_offset;
    assert(!c.CanSendRawPacket()&&c.WriteRawStream(0,bytes,4)==0&&!c.FinishRawStream(0));
    assert(c.connected&&!c.raw_streams_[0].send_closed&&c.flow_controller_.GetStreamState(0)->send_offset==before);
    c.loss_detector_.cc.ready=true;assert(c.CanSendRawPacket());
    assert(c.FinishRawStream(0)&&!c.FinishRawStream(0)&&c.WriteRawStream(0,bytes,4)==-1);
    c.config_.application_mode=QuicApplicationMode::kHttp3;assert(c.OpenRawStream()==-1&&c.WriteRawStream(4,bytes,4)==-1);
    TestConnection limited;limited.peer_params_.initial_max_streams_bidi=0;assert(limited.OpenRawStream()==-1);
    TestConnection recv;unsigned callbacks=0;recv.on_raw_stream_frame_=[&](uint64_t id,uint64_t offset,const uint8_t* p,size_t n,bool fin){assert(id==1&&n==4);++callbacks;};
    recv.ReceiveRawStreamFrame(1,4,bytes,4,true);recv.ReceiveRawStreamFrame(1,0,bytes,4,false);recv.ReceiveRawStreamFrame(1,0,bytes,4,false);
    assert(callbacks==3&&recv.connected&&recv.flow_controller_.GetStreamState(1)->recv_offset==8);
    assert(!recv.AcknowledgeRawStreamData(1,9)&&recv.AcknowledgeRawStreamData(1,8));
    recv.ReceiveRawStreamFrame(1,8,bytes,4,false);assert(recv.close_error==6);
    TestConnection direction;direction.ReceiveRawStreamFrame(2,0,bytes,4,false);assert(direction.close_error==5);
    TestConnection unopened;unopened.ReceiveRawStreamFrame(0,0,bytes,4,false);assert(unopened.close_error==5);
    TestConnection streamlimit;streamlimit.ReceiveRawStreamFrame(17,0,bytes,4,false);assert(streamlimit.close_error==4);
    TestConnection flow;flow.ReceiveRawStreamFrame(1,4096,bytes,4,false);assert(flow.close_error==3);
    TestConnection overflow;overflow.ReceiveRawStreamFrame(1,(uint64_t{1}<<62)-1,bytes,4,false);assert(overflow.close_error==5);
    TestConnection sendfail;sendfail.OpenRawStream();sendfail.send_ok=false;assert(sendfail.WriteRawStream(0,bytes,4)==-1&&!sendfail.connected);
    TestConnection uni;uni.ReceiveRawStreamFrame(3,0,bytes,4,true);assert(uni.connected&&uni.WriteRawStream(3,bytes,4)==-1);
    // A multi-turn camera connection must outlive the old 64-record limit.
    // Keep closed records: late frames still obey their original final size.
    TestConnection longrun;longrun.config_.max_raw_stream_records=512;
    longrun.config_.max_streams_uni=256;longrun.peer_params_.initial_max_streams_uni=600;
    for(unsigned i=0;i<180;++i){auto id=longrun.OpenRawStream(true);assert(id==int64_t(2+4*i));assert(longrun.FinishRawStream(id));}
    for(unsigned i=0;i<256;++i)longrun.ReceiveRawStreamFrame(3+4*i,0,bytes,4,true);
    assert(longrun.connected&&longrun.raw_streams_.size()==436);
    longrun.ReceiveRawStreamFrame(3,0,bytes,4,true);assert(longrun.connected&&longrun.raw_streams_.size()==436);
    while(longrun.raw_streams_.size()<512)assert(longrun.OpenRawStream(true)>=0);
    assert(longrun.OpenRawStream(true)==-1&&longrun.raw_streams_.size()==512);
    longrun.ReceiveRawStreamFrame(3,4,bytes,4,false);assert(longrun.close_error==6);
    TestConnection defaultcap;defaultcap.peer_params_.initial_max_streams_uni=128;
    for(unsigned i=0;i<64;++i)assert(defaultcap.OpenRawStream(true)>=0);
    assert(defaultcap.OpenRawStream(true)==-1);
    TestConnection recvcap;recvcap.config_.max_raw_stream_records=1;
    recvcap.ReceiveRawStreamFrame(3,0,bytes,4,true);assert(recvcap.connected);
    recvcap.ReceiveRawStreamFrame(7,0,bytes,4,true);assert(recvcap.close_error==1&&recvcap.raw_streams_.size()==1);
    puts("raw production methods: exact bytes, IDs, FIN, offsets, duplicate frame delivery, flow/range/limits/send failure PASS");
}
static void PinTests() {
    // A syntactically valid TLS Certificate envelope; DER parser is the stated mock.
    uint8_t cert[]={0,0,0,8,0,0,3,1,2,3,0,0};
    uint8_t leaf[]={1,2,3},hash[32];quic::Sha256(leaf,sizeof(leaf),hash);
    TestConnection c;c.config_.trusted_leaf_sha256.assign(hash,hash+32);ca_calls=0;
    assert(c.ProcessCertificate(cert,sizeof(cert))&&c.certificate_validated_&&ca_calls==0);
    TestConnection wrong;wrong.config_.trusted_leaf_sha256.assign(hash,hash+32);wrong.config_.trusted_leaf_sha256[31]^=1;
    assert(!wrong.ProcessCertificate(cert,sizeof(cert))&&!wrong.certificate_validated_);
    TestConnection malformed;malformed.config_.trusted_leaf_sha256={1};assert(!malformed.ProcessCertificate(cert,sizeof(cert)));
    TestConnection ca;ca_result=-1;assert(!ca.ProcessCertificate(cert,sizeof(cert))&&ca_calls==1);ca_result=0;
    uint8_t finished[32];std::memset(finished,42,32);assert(!c.ProcessServerFinished(finished,32));
    uint8_t cv[]={0x04,0x03,0,1,1};signature_result=-1;assert(!c.ProcessCertificateVerify(cv,sizeof(cv))&&!c.certificate_verify_validated_);
    signature_result=0;assert(c.ProcessCertificateVerify(cv,sizeof(cv))&&c.ProcessServerFinished(finished,32));
    finished[31]^=1;assert(!c.ProcessServerFinished(finished,32));
    puts("leaf pin actual SHA256 + CA fallback + signature/Finished policy gating PASS (certificate/signature mock)");
}
int main() {AlpnTests();ExtraTransportTests();RawTests();PinTests();}
