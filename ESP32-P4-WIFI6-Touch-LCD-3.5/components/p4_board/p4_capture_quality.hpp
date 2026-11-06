#pragma once
// Tutor still-frame selection only. No changes to person/face preview gates.
#include <algorithm>
#include <array>
#include <cstdint>

namespace p4_capture {
constexpr unsigned window_ms = 1800, sample_ms = 120, warmup_ms = 450;
constexpr unsigned max_gradient_points = 16384;
constexpr unsigned measure_drain_ms = 2*sample_ms;
inline unsigned gradient_stride(unsigned w,unsigned h) {
    unsigned stride=2;
    if(w<10 || h<10)return stride;
    while(uint64_t((w-8+stride-1)/stride)*((h-8+stride-1)/stride)>max_gradient_points)++stride;
    return stride;
}
// Both timestamps are relative to this capture request. A new score does not
// make an older retained candidate fresh. Shared by host and production gates.
inline bool fresh_frame_times(uint64_t now, uint64_t last_sample, uint64_t selected) {
    return selected<=last_sample && last_sample<=now &&
           now-last_sample<=2*sample_ms && now-selected<=2*sample_ms;
}
inline bool final_frame_ready(bool selected, bool measuring, uint64_t now,
                              uint64_t last_sample, uint64_t selected_at) {
    return selected && !measuring && fresh_frame_times(now,last_sample,selected_at);
}
constexpr unsigned grid_w = 32, grid_h = 24;
using Grid = std::array<uint8_t, grid_w * grid_h>;

inline unsigned luma(uint16_t p) {
    return (((p >> 11) & 31) * 8 + ((p >> 5) & 63) * 8 + (p & 31) * 8) / 4;
}
inline unsigned delta(unsigned a, unsigned b) { return a > b ? a - b : b - a; }

struct Metrics {
    unsigned mean = 0, range = 0, edge_samples = 0, sharp_edges = 0;
    unsigned sharpness = 0, sharp_fraction = 0, motion = 0, changed_permille = 0;
    unsigned gradient_points = 0, stride = 0;
    // A mostly white sheet is valid when its sparse dark edges remain intact.
    bool exposed() const { return mean >= 16 && range >= 24; }
    bool detailed() const {
        // Local contrast, not full-image average texture: a sparse simple sum
        // can qualify. Fine/coarse edge concentration rejects broad soft edges.
        // Coarser sampling also evaluates off-centre edges, so demand a
        // stronger concentration than the original centre-only measurement.
        return exposed() && edge_samples >= 12 && sharp_edges >= 8 &&
               sharpness >= (stride>2?400u:310u) && sharp_fraction >= (stride>2?350u:180u);
    }
};

inline Metrics measure(const uint16_t *pixels, unsigned w, unsigned h, Grid &grid) {
    Metrics result;
    if (!pixels || w < 10 || h < 10) return result;
    unsigned sum = 0, lo = 255, hi = 0;
    for (unsigned y = 0; y < grid_h; ++y) for (unsigned x = 0; x < grid_w; ++x) {
        unsigned sx = std::min(w - 1, (2 * x + 1) * w / (2 * grid_w));
        unsigned sy = std::min(h - 1, (2 * y + 1) * h / (2 * grid_h));
        unsigned v = luma(pixels[sy * w + sx]);
        grid[y * grid_w + x] = uint8_t(v);
        sum += v; lo = std::min(lo, v); hi = std::max(hi, v);
    }
    result.mean = sum / grid.size();
    uint64_t fine_sum = 0, coarse_sum = 0;
    result.stride=gradient_stride(w,h);
    // Deterministically distribute each cell's point across both axes, avoiding
    // a regular sampling lattice that aliases thin mathematical glyph strokes.
    for (unsigned by = 4, row=0; by + 4 < h; by += result.stride,++row)
    for (unsigned bx = 4, col=0; bx + 4 < w; bx += result.stride,++col) {
        unsigned phase=(col*73856093u)^(row*19349663u);
        unsigned x=result.stride>2?std::min(w-5,bx+phase%result.stride):bx;
        unsigned y=result.stride>2?std::min(h-5,by+(phase>>8)%result.stride):by;
        ++result.gradient_points;
        unsigned values[] = {luma(pixels[y*w+x+1]), luma(pixels[y*w+x-1]),
                             luma(pixels[(y+1)*w+x]), luma(pixels[(y-1)*w+x]),
                             luma(pixels[y*w+x+4]), luma(pixels[y*w+x-4]),
                             luma(pixels[(y+4)*w+x]), luma(pixels[(y-4)*w+x])};
        for (unsigned v : values) { lo = std::min(lo, v); hi = std::max(hi, v); }
        unsigned fine = std::max(delta(values[0], values[1]), delta(values[2], values[3]));
        // The outer intervals reuse the same eight loads. Normalize their
        // three-pixel spans to two pixels, so broad linear blur keeps its old
        // fine/coarse ratio while a sharp edge off the cell centre is retained.
        unsigned outer=std::max(std::max(delta(values[4],values[0]),delta(values[1],values[5])),
                                std::max(delta(values[6],values[2]),delta(values[3],values[7])));
        if(result.stride>2)fine=std::max(fine,outer*2/3);
        unsigned coarse = std::max(delta(values[4], values[5]), delta(values[6], values[7]));
        if (coarse < 24) continue;
        ++result.edge_samples;
        fine_sum += fine; coarse_sum += coarse;
        if (fine >= 12 && fine * 1000 >= coarse * 400) ++result.sharp_edges;
    }
    result.sharpness = coarse_sum ? unsigned(fine_sum * 1000 / coarse_sum) : 0;
    result.sharp_fraction = result.edge_samples ? result.sharp_edges * 1000 / result.edge_samples : 0;
    result.range = hi - lo;
    return result;
}

struct Decision { bool sampled = false, replace = false, discard = false; };

class Window {
    // Keep this object in capture-worker static storage, not its small stack.
    Grid previous_{}, current_{};
    uint64_t started_ = 0, last_ = 0;
    unsigned previous_mean_ = 0, stable_ = 0;
    bool previous_valid_ = false, previous_detailed_ = false;
public:
    unsigned request = 0, samples = 0, selected_elapsed_ms = 0;
    bool selected = false;
    Metrics latest{}, best{};
    const char *reason = "no_frames";

    void begin(unsigned id, uint64_t now_ms) {
        request = id; started_ = now_ms; last_ = now_ms;
        previous_mean_ = stable_ = samples = selected_elapsed_ms = 0;
        previous_valid_ = previous_detailed_ = selected = false;
        latest = {}; best = {}; reason = "no_frames";
    }
    bool due(uint64_t now_ms) const {
        return now_ms >= started_ && now_ms - started_ < window_ms &&
               (!samples || now_ms - last_ >= sample_ms);
    }
    bool fresh(uint64_t now_ms) const {
        return selected && now_ms>=started_ &&
               fresh_frame_times(now_ms-started_,last_-started_,selected_elapsed_ms);
    }
    Decision observe(const uint16_t *pixels, unsigned w, unsigned h, uint64_t now_ms) {
        if (!due(now_ms)) return {};
        latest = measure(pixels, w, h, current_);
        unsigned changed = 0, movement = 0;
        if (previous_valid_) {
            for (unsigned i = 0; i < current_.size(); ++i) {
                unsigned d = delta(current_[i], previous_[i]); movement += d;
                if (d >= 16) ++changed;
            }
            latest.motion = movement / current_.size();
            latest.changed_permille = changed * 1000 / current_.size();
        }
        bool motion_ok = previous_valid_ && latest.exposed() && latest.motion <= 5 &&
                         latest.changed_permille <= 70 && delta(latest.mean, previous_mean_) <= 6;
        // An earlier stable scene is invalid after movement, exposure change or
        // loss of detail. Selection belongs to the final stable scene segment.
        bool discard = selected && (!motion_ok || !latest.detailed());
        if(discard){selected=false;best={};selected_elapsed_ms=0;}
        bool stable = motion_ok && previous_detailed_ && latest.detailed();
        stable_ = stable ? stable_ + 1 : 0;
        previous_ = current_; previous_mean_ = latest.mean;
        previous_valid_ = latest.exposed(); previous_detailed_=latest.detailed(); last_ = now_ms; ++samples;
        if (!latest.exposed()) reason = "exposure_or_low_contrast";
        else if (latest.edge_samples < 12) reason = "insufficient_edges";
        else if (!latest.detailed()) reason = "blurred";
        else if (now_ms - started_ < warmup_ms) reason = "warming_up";
        else if (stable_ < 2) reason = "moving";
        else {
            reason = "candidate_ready";
            // Copy every newly qualified stable frame. Keeping an older high
            // score can expire at the deadline even when the last frame is good.
            selected = true; best = latest;
            selected_elapsed_ms = unsigned(now_ms - started_);
            return {true, true, discard};
        }
        return {true, false, discard};
    }
};
} // namespace p4_capture
