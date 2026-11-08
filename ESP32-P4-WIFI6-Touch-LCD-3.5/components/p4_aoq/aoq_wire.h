#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Derived from the downloaded official Windows SDK and matched to its pure
// encoders. This is a wire audit prototype, not proof of native interoperability.
namespace aoq_wire {
constexpr const char* kAlpn = "aoq-quic-01";
constexpr uint32_t kClientVersion = 0xff000001u;
constexpr uint64_t kSetupType = 0x2f00;
struct MediaParam {
    uint8_t codec = 21; // SDK CodecPref: PCM=11, Utf8=20, Json=21.
    uint8_t audio_model = 0;
    uint8_t audio_payload_format = 0;
    uint32_t sample_rate_hz = 0;
    uint8_t channels = 0;
    uint32_t video_width=0,video_height=0;
    uint16_t video_fps=0;
    std::string schema = "aoq.command.v1";
    uint32_t timescale = 1000;
};
struct Track {
    std::string name;
    uint8_t direction = 0; // Publish=0, Subscribe=1.
    uint32_t alias = 0;
    std::vector<MediaParam> media;
};
// Inference models declare only their documented audio directions. Data and
// command tracks remain bidirectional for task/session control.
std::vector<Track> pcm_track_profile(bool audio_in,bool audio_out);
std::vector<Track> pcm_six_track_profile();
bool encode_media_profile(const std::vector<Track>& tracks,
                          std::vector<uint8_t>& out, std::string& error);
bool encode_client_setup(const std::string& token,
                         const std::vector<Track>& tracks,
                         std::vector<uint8_t>& out, std::string& error);
// Convenience setup-only entry: six observed track descriptors, no media data.
// Token <=1024 bytes; encoded client setup <=16 KiB.
bool encode_client_setup(const char* token, std::vector<uint8_t>& out);
enum class ParseStatus { NeedMore, Ok, Invalid };
struct ServerSetup {
    uint32_t selected_version = 0;
    uint32_t accept_status = 0;
    std::string sdk_config;
    size_t consumed = 0;
    // SDK payload decoder preserves the raw version, without validation.
    // Prototype permits the sent draft value and observed server value 1.
    bool known_version() const {
        return selected_version == 1 || selected_version == kClientVersion;
    }
    bool accepted() const { return known_version() && accept_status == 0; }
};
ParseStatus decode_server_setup(const uint8_t* data, size_t size,
                                ServerSetup& out, std::string& error);
ParseStatus parse_server_setup(const uint8_t* data, size_t size,
                               uint64_t* version, uint64_t* accept_status,
                               size_t* consumed);
} // namespace aoq_wire
