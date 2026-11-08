#pragma once
#include "p4_aoq.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace aoq {
// Short, single-owner native experiment. Return values are core agent_err_t
// numbers. No Wi-Fi setup, NVS, audio device, playback or runtime hook is owned.
struct Bytes { const uint8_t* data=nullptr; size_t size=0; };
struct Object {
    uint64_t stream=0, group=0, subgroup=0, id=0, status=0;
    Bytes properties, opaque_userdata, payload;
};
struct End {
    uint64_t stream=0, alias=0, group=0, subgroup=0, object=0, status=0;
    bool stream_fin=false, end_group=false, end_track=false;
};
struct Callbacks {
    void* userdata=nullptr;
    // Views are borrowed until this callback returns. Return 0 to consume;
    // nonzero aborts the session. send_json is allowed here; recursive poll,
    // open or object destruction is not. No callback runs on another thread.
    int (*data)(void*, const Object&)=nullptr;  // alias 302, validated JSON object
    int (*audio)(void*, const Object&)=nullptr; // alias 202, validated PCM object
    int (*end)(void*, const End&)=nullptr;     // transport/group/track end only
    // Optional Audio backpressure. False retains this object and its receive
    // credit; UDP ACKs, timers, Data and other streams must keep progressing.
    bool (*audio_ready)(void*, size_t payload_bytes)=nullptr;
};
struct Options {
    unsigned lifetime_ms=30000;       // allocation + transport + trial, <=30 s
    // Explicit bounded conversation profile. The caller renews at an idle
    // boundary before 180 s; diagnostics retain their original 30 s limits.
    bool conversation=false;
    bool opus_downlink=false; // negotiated 48 kHz Opus, decoded locally to 24 kHz PCM
    bool use_prefetched_allocation=false; // one unused, unexpired native allocation
    unsigned allocation_timeout_ms=10000;
    unsigned setup_timeout_ms=12000;  // after allocation, bounded by lifetime
    int64_t deadline_ms=0;            // optional earlier esp_timer deadline
    bool (*cancelled)(void*)=nullptr;
    void* cancel_userdata=nullptr;
    bool trace_metadata=false; // explicit diagnostics only, no string payloads
    uint32_t video_width=0,video_height=0; // OMNI only; both zero disables Video
};
struct Diagnostics {
    const char* stage="idle"; // fixed strings only; no payload/credential text
    int error=0, quic_error=0;
    bool tls_verified=false, setup_accepted=false, pcm24k_selected=false, pcm16k_input_selected=false, closed=true;
    bool opus48k_selected=false;
    uint64_t audio_decoded_bytes=0;
    uint64_t audio_decode_total_us=0;
    uint32_t audio_decode_max_us=0;
    uint32_t selected_version=0, accept_status=0;
    uint64_t tx_bytes=0, rx_bytes=0, consumed_bytes=0;
    uint64_t last_control_type=0;
    size_t last_control_length=0;
    bool goaway_received=false;
    uint32_t goaway_reason_length=0;
    char goaway_reason[160]{},last_notify_tag[64]{};
    uint64_t json_rx_bytes=0, audio_rx_bytes=0, json_tx_bytes=0;
    uint64_t audio_tx_samples=0;
    uint32_t audio_datagrams_sent=0;
    uint32_t command_datagrams_received=0;
    uint32_t declared_lost_datagrams=0;
    bool jpeg_selected=false,video_pending=false;
    uint32_t video_frames_queued=0,video_frames_sent=0;
    uint64_t video_tx_bytes=0;
    uint32_t rtt_ms=0;
    uint32_t poll_gap_max_ms=0;
    uint32_t udp_queue_peak=0,udp_queue_waits=0;
    int64_t last_udp_ms=0,last_audio_ms=0;
    size_t contiguous_bytes=0,out_of_order_span=0;
    uint32_t audio_backpressure=0;
    bool receive_drained=false; // latest pump saw UDP EAGAIN and no buffered bytes/gaps
    uint32_t tx_packets=0, rx_packets=0, control_frames=0, media_setups=0, notifications=0;
    uint32_t data_objects=0, audio_objects=0, command_objects=0, terminal_objects=0;
    uint32_t queued_objects=0, sent_objects=0, queue_full=0, stream_fins=0;
    size_t queue_bytes=0, active_streams=0, peak_streams=0;
    int64_t elapsed_ms=0;
    int64_t allocation_ms=0;
    bool allocation_prefetched=false;
};

class Session {
public:
    Session();
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    // Fresh allocation, numeric relay only, pinned TLS and SERVER_SETUP.
    // Success means setup_accepted, NOT media ready or model task ready.
    int open(p4_aoq_kind_t, const Options& = Options{}, const Callbacks& = Callbacks{});
    // Drives nonblocking UDP, timers and callbacks for at most max_ms (0..50),
    // excluding bounded codec/callback work. Deadline/cancel errors are final.
    int poll(unsigned max_ms=10);
    // Entire JSON is copied into the bounded queue on 0. Busy=3 means zero
    // acceptance; callers may retry. Partial network writes remain internal.
    // Queue acceptance does NOT mean peer delivery, task-started or ACK.
    int send_json(const char*, size_t);
    // One mono PCM16LE frame, 1..320 samples at 16kHz. Call only after the
    // model's actual ready event. No JSON/Base64 duplicate is sent. Busy means
    // zero acceptance; another error may close the session. Success is socket
    // submission, not delivery (QUIC DATAGRAM is intentionally unreliable).
    int send_pcm16(const int16_t*,size_t samples,uint64_t timestamp_us);
    // Independent baseline JPEG, <=65536 bytes, dimensions exactly as offered.
    // Copies one complete frame on success. Busy means zero acceptance; only
    // one frame may wait for transport at a time. One fresh group/stream/frame.
    // Call after model ready, at <=1 fps; never duplicate via Data image events.
    int send_jpeg(const uint8_t*,size_t bytes,uint64_t timestamp_us);
    // Sole cross-thread method while Session remains alive. An atomic epoch
    // cancels the operation whose open-entry snapshot precedes this request;
    // a later explicit open starts a new operation. Join callers before destroy.
    void cancel();
    void close();  // callback-safe deferred close; otherwise immediate cleanup
    Diagnostics diagnostics() const;
private:
    struct Impl;
    // Stable for the Session lifetime, including replacement of impl_ in open.
    // Declared first so it also outlives Impl during member destruction.
    std::atomic<uint32_t> cancel_epoch_{0};
    std::unique_ptr<Impl> impl_;
};

// Limits: 16 KiB JSON, 8 queued objects / 64 KiB encoded queue, 8 active receive
// streams. Short probes use a 72 KiB receive window; conversations use 512 KiB
// so compressed delivery can run ahead of decoding. Mirrored data plus seen
// costs three times the receive window in PSRAM (at most 12 MiB for 8 streams).
// Conversations also have a bounded 512-packet / approximately 2 MiB UDP queue,
// covering a 64 KiB object plus 4 KiB properties. At most 32 peer uni streams
// and 4096 total objects per short connection; no production lifetime GC.
// Audio arriving before MEDIA_SETUP is buffered, never played or reclassified.
// Only an actual PCM24k/mono/Segment/Single selection authorizes Audio callback.
// Group and LOC type-19 userdata stay opaque; neither is a model response ID.
// FIN alone ends a group only when subgroup.end_of_group says so; neither FIN
// nor status 3/4 means model task-finished. Callers must inspect Data events.
// Allocation uses bounded synchronous HTTPS; cancellation is checked before
// and after it, not inside an ESP-IDF HTTP call. No automatic retries, DNS,
// endpoint failover or invented outbound MEDIA_SETUP/NOTIFY is performed.
} // namespace aoq
