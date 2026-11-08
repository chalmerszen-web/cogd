#pragma once
#include "aoq_wire.h"

// SDK-matched stream codec prototype. No transport/session lifecycle is owned here.
namespace aoq_wire {
struct SubgroupHeader {
    uint64_t track_alias=301,group=0,subgroup=0;
    uint8_t priority=0;
    bool has_properties=true;
    uint8_t subgroup_id_mode=0; // 0: zero; 1: first object; 2: explicit.
    bool end_of_group=false,default_priority=false,first_object=false;
    size_t consumed=0;
};
struct ObjectCursor { bool has_previous=false; uint64_t previous=0; };
struct ObjectView {
    uint64_t id=0,status=0;
    const uint8_t* properties=nullptr;
    size_t properties_size=0;
    const uint8_t* payload=nullptr;
    size_t payload_size=0,consumed=0;
};
bool encode_subgroup_header(const SubgroupHeader&,std::vector<uint8_t>& out);
ParseStatus decode_subgroup_header(const uint8_t*,size_t,SubgroupHeader&);
bool encode_text_properties(uint64_t capture_timestamp_us,std::vector<uint8_t>& out);
struct AudioProperties {
    uint64_t timestamp_us=0,sample_cursor=0,source_silence_us=0;
    uint32_t duration_samples=0;
    uint8_t payload_kind=0,audio_level=0;
    bool has_audio_level=false;
};
// Minimal single-frame PCM properties. Rate/channels are negotiated in profile.
bool encode_pcm_properties(const AudioProperties&,std::vector<uint8_t>& out);
ParseStatus decode_audio_properties(const uint8_t*,size_t,AudioProperties&);
bool pcm_payload_size_matches(const AudioProperties&,uint8_t channels,size_t bytes);
// Opus uses a negotiated 48 kHz sample clock; the decoder produces 24 kHz.
// Validate before reserving decoded queue space, without decoding twice on retry.
bool opus48_to_pcm24_properties(const AudioProperties&,AudioProperties& out);
bool encode_object(uint64_t id,uint64_t status,bool has_properties,
                   const std::vector<uint8_t>& properties,
                   const uint8_t* payload,size_t payload_size,
                   ObjectCursor&,std::vector<uint8_t>& out);
// Views reference input; caller retains its buffer. Cursor changes only on Ok.
// Hard caps: properties 4096B, payload 65536B. A complete object is required.
ParseStatus decode_object(const uint8_t*,size_t,bool has_properties,
                          ObjectCursor&,ObjectView&);
struct DatagramHeader {
    uint64_t track_alias=201,group=0,subgroup=0,id=0;
    uint8_t priority=10;
    bool end_of_group=false;
};
// AOQ's complete one-object datagram (SDK type 0x41). This is distinct from
// stream subgroup framing. Caps the encoded datagram at 1200B; the transport
// must apply its tighter negotiated/packet-overhead bound before transmission.
bool encode_datagram(const DatagramHeader&,const std::vector<uint8_t>& properties,
                     const uint8_t* payload,size_t size,std::vector<uint8_t>& out);
// Atomic input: a truncated datagram is Invalid, not a stream NeedMore. Returned
// views borrow the input. Only normal (status 0) objects exist in this format.
ParseStatus decode_datagram(const uint8_t*,size_t,DatagramHeader&,ObjectView&);
} // namespace aoq_wire
