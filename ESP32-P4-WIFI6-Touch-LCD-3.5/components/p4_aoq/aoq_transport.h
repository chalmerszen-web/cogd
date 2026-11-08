#pragma once
#include "p4_aoq.h"
#include "aoq_wire.h"
#include "core/quic_connection.h"
#include <utility>
#include <vector>

namespace aoq {
// Matched to the official SDK's synthetic loopback ClientHello. The signed SID
// is not split or decoded. It is session material: never log these parameters
// or export a resulting Initial into public diagnostics.
inline bool configure_transport_identity(esp_http3::QuicConfig& config,
                                         const p4_aoq_allocation_t& allocation,
                                         const p4_aoq_endpoint_t& endpoint) {
    size_t length=0;
    while(length<sizeof(allocation.sid) && allocation.sid[length]) ++length;
    if(length<3 || length==sizeof(allocation.sid)) return false;
    config.alpn=aoq_wire::kAlpn;
    config.hostname="localhost"; // XQUIC's observed fallback SNI; trust remains the HTTPS-issued leaf pin.
    config.initial_cid_prefix={0,static_cast<uint8_t>(allocation.sid[0]),
        static_cast<uint8_t>(allocation.sid[1]),static_cast<uint8_t>(allocation.sid[2])};
    const uint64_t value=endpoint.route_index;
    const size_t size=value<64?1:value<16384?2:value<(uint64_t(1)<<30)?4:8;
    std::vector<uint8_t> route(size);
    for(size_t i=0;i<size;++i) route[i]=static_cast<uint8_t>(value>>((size-1-i)*8));
    route[0]|=size==1?0:size==2?0x40:size==4?0x80:0xc0;
    const auto* sid=reinterpret_cast<const uint8_t*>(allocation.sid);
    config.extra_transport_parameters={
        {0xff0e07,{1}}, // ClientSide role, encoded QUIC varint.
        {0xff0e08,std::vector<uint8_t>(sid,sid+length)},
        {0xff0e09,std::move(route)}
    };
    return true;
}
} // namespace aoq
