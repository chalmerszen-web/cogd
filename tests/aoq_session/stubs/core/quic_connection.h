#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/types.h>
namespace esp_http3 {
// Test-only receive script. These bytes never touch an OS socket.
inline std::vector<std::vector<uint8_t>> test_control_chunks;
enum class QuicApplicationMode{kRaw};
enum class ConnectionState{kConnected,kFailed,kClosed};
struct QuicConfig {
 uint32_t max_raw_stream_records=64;
 QuicApplicationMode application_mode{};std::string alpn,hostname;uint16_t port=0;
 std::vector<uint8_t> trusted_leaf_sha256,initial_cid_prefix;
 std::vector<std::pair<uint64_t,std::vector<uint8_t>>> extra_transport_parameters;
 bool allow_unverified_peer=false,enable_debug=false;
 bool enable_datagram=false;uint32_t max_datagram_frame_size=0;
 uint32_t handshake_timeout_ms=0,idle_timeout_ms=0,max_stream_data=0,max_data=0,max_streams_bidi=0,max_streams_uni=0;
};
class QuicConnection {
public:
 using Frame=std::function<void(uint64_t,uint64_t,const uint8_t*,size_t,bool)>;
 std::map<uint64_t,std::vector<uint8_t>> sent;size_t write_limit=3;uint64_t uni=2;
 ConnectionState state=ConnectionState::kConnected;std::function<void()> connected;Frame frame;
 std::function<void(int,uint64_t)> reset,stop_sending;
 size_t scripted_chunk=0;uint64_t scripted_offset=0;
 bool datagram_ready=true,datagram_ok=true;size_t datagram_max=1168;std::vector<std::vector<uint8_t>> datagrams;
 bool raw_ready=true,fin_ok=true;std::vector<uint64_t> fins;
 QuicConnection(std::function<int(const uint8_t*,size_t)>,const QuicConfig&){}
 void Close(){state=ConnectionState::kClosed;}
 void SetOnConnected(std::function<void()> f){connected=f;}
 void SetOnDisconnected(std::function<void(int,const std::string&)>){}
 void SetOnStreamReset(std::function<void(int,uint64_t)> f){reset=f;}
 void SetOnStreamStopSending(std::function<void(int,uint64_t)> f){stop_sending=f;}
 void SetOnRawStreamFrame(Frame f){frame=f;}
 void SetOnDatagram(std::function<void(const uint8_t*,size_t)>){ }
 size_t GetMaxDatagramSize()const{return datagram_max;}
 bool CanSendDatagram(size_t n)const{return datagram_ready&&n<=datagram_max;}
 bool SendDatagram(const uint8_t* p,size_t n){if(!datagram_ok)return false;datagrams.emplace_back(p,p+n);return true;}
 struct Stats {uint32_t rtt_ms=50,declared_lost_datagrams=0;};Stats GetStats()const{return {};}
 bool StartHandshake(){if(connected)connected();return true;}
 int64_t OpenRawStream(bool unidirectional){if(!unidirectional)return 0;auto id=uni;uni+=4;return static_cast<int64_t>(id);}
 ssize_t WriteRawStream(uint64_t id,const uint8_t* p,size_t n){n=std::min(n,write_limit);if(n)sent[id].insert(sent[id].end(),p,p+n);return static_cast<ssize_t>(n);}
 bool CanSendRawPacket()const{return raw_ready;}
 bool FinishRawStream(uint64_t id){if(!raw_ready||!fin_ok)return false;fins.push_back(id);return true;}
 bool AcknowledgeRawStreamData(uint64_t,size_t){return true;}
 void ProcessReceivedData(const uint8_t*,size_t){}
 void OnTimerTick(uint32_t){
  if(frame&&scripted_chunk<test_control_chunks.size()){
   const auto& bytes=test_control_chunks[scripted_chunk++];
   frame(0,scripted_offset,bytes.data(),bytes.size(),false);scripted_offset+=bytes.size();
  }
 }
 ConnectionState GetState()const{return state;}
};
}
