#include "quic/quic_frame.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace esp_http3::memory {
void* Allocate(size_t n,size_t) noexcept { return std::malloc(n); }
void Deallocate(void* p) noexcept { std::free(p); }
bool UsesPsram() noexcept { return false; }
}
using namespace esp_http3;
static std::vector<uint8_t> Param(uint64_t id,uint64_t value) {
  uint8_t v[8],out[32];quic::BufferWriter vw(v,sizeof(v));vw.WriteVarint(value);
  quic::BufferWriter ow(out,sizeof(out));ow.WriteVarint(id);ow.WriteVarint(vw.Offset());ow.WriteBytes(v,vw.Offset());
  return {out,out+ow.Offset()};
}
static unsigned defects;
static void Check(const char* name,const std::vector<uint8_t>& v,bool expected) {
  quic::TransportParameters p;bool accepted=quic::ParseTransportParameters(v.data(),v.size(),&p);
  bool pass=accepted==expected;defects+=!pass;
  std::printf("%s expected_accepted=%d actual_accepted=%d verdict=%s\n",name,expected,accepted,pass?"PASS":"FAIL");
}
int main() {
  Check("valid_ack_delay_exponent_3",Param(0x0a,3),true);
  Check("invalid_ack_delay_exponent_21",Param(0x0a,21),false);
  Check("invalid_ack_delay_exponent_64",Param(0x0a,64),false);
  Check("invalid_udp_payload_1199",Param(0x03,1199),false);
  Check("invalid_max_ack_delay_16384",Param(0x0b,16384),false);
  Check("invalid_initial_max_streams_bidi",Param(0x08,(uint64_t(1)<<60)+1),false);
  auto duplicate=Param(0x0a,3);auto second=Param(0x0a,4);duplicate.insert(duplicate.end(),second.begin(),second.end());
  Check("duplicate_ack_delay_exponent",duplicate,false);
  Check("nonempty_disable_active_migration",{0x0c,0x01,0x00},false);
  auto trailing=Param(0x0a,3);trailing[1]=2;trailing.push_back(0);Check("integer_parameter_trailing_byte",trailing,false);
  Check("maximum_valid_ack_exponent",Param(0x0a,20),true);
  Check("maximum_valid_streams",Param(0x09,uint64_t(1)<<60),true);
  Check("invalid_streams_uni",Param(0x09,(uint64_t(1)<<60)+1),false);
  Check("invalid_active_cid_limit",Param(0x0e,1),false);
  Check("valid_unknown_parameter",{0x3f,0x02,0x00,0xff},true);
  Check("duplicate_unknown_parameter",{0x3f,0x00,0x3f,0x00},false);
  Check("truncated_parameter_value",{0x0a,0x02,0x03},false);
  Check("truncated_parameter_id",{0x40},false);
  std::vector<uint8_t> longcid={0x0f,21};longcid.resize(23,1);Check("oversized_connection_id",longcid,false);
  Check("invalid_reset_token_length",{0x02,0x01,0x00},false);
  quic::TransportParameters unchanged;unchanged.ack_delay_exponent=7;
  auto partial=Param(0x0a,3);partial.push_back(0x40);
  bool atomic=!quic::ParseTransportParameters(partial.data(),partial.size(),&unchanged)&&unchanged.ack_delay_exponent==7;
  defects+=!atomic;std::printf("failure_does_not_partially_commit verdict=%s\n",atomic?"PASS":"FAIL");
  std::printf("confirmed_invalid_parameter_acceptances=%u\n",defects);
  return defects?1:0;
}
