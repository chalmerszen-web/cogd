#pragma once
#include "aoq_wire.h"

namespace aoq_wire {
constexpr uint64_t kMediaSetupType=0xff0e20;
constexpr uint64_t kNotifyType=0xff0e30;
constexpr uint64_t kGoAwayType=0x10;
struct MediaBinding {
    std::string track;
    uint32_t alias=0;
    uint8_t direction=0,priority=0;
    MediaParam selected;
};
struct MediaSetup {
    std::vector<MediaBinding> bindings;
    uint8_t reason=0; // Initial=0, Update=1.
    size_t consumed=0;
};
struct Notify {
    uint8_t type=0; // SDK NotifyType: Custom=0, Internal=1, Message=2. No side effects.
    std::string tag;
    std::vector<uint8_t> payload;
    size_t consumed=0;
};
struct GoAway {
    std::string new_session_uri,reason_phrase;
    uint64_t largest_request_id=0;
    size_t consumed=0;
};
// Complete control frames: varint(type), u16BE payload length, TLV payload.
// Unknown TLVs are skipped; duplicate known TLVs/malformed lengths are rejected.
ParseStatus decode_media_setup(const uint8_t*,size_t,MediaSetup&,std::string& error);
ParseStatus decode_notify(const uint8_t*,size_t,Notify&,std::string& error);
// URI/reason are untrusted diagnostic data; decoding never redirects or logs.
ParseStatus decode_goaway(const uint8_t*,size_t,GoAway&,std::string& error);
bool encode_media_setup(const MediaSetup&,std::vector<uint8_t>&,std::string& error);
bool encode_notify(const Notify&,std::vector<uint8_t>&,std::string& error);
// Checks an actual received selection; direction is local (Subscribe=1), not
// sender-relative. This does not by itself mark a connection ready.
bool selected_pcm24k_segment(const MediaBinding&);
bool selected_opus48k_segment(const MediaBinding&);
} // namespace aoq_wire
