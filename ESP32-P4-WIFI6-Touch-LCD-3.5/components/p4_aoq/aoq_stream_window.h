#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace aoq_wire {
// Bounded ordered view over offset-bearing QUIC frames. Storage is provided by
// the caller (PSRAM on P4): data has 2*capacity bytes, seen has capacity bytes.
// The mirrored ring gives the parser one contiguous view without moving the
// entire receive window for every small Opus object. No packet can trigger an
// allocation. Consumed retransmissions are ignored; conflicting retained bytes
// fail.
class StreamWindow {
public:
    StreamWindow(uint8_t* data, uint8_t* seen, size_t capacity)
        : data_(data), seen_(seen), capacity_(capacity) {
        failed_ = !data || !seen || !capacity || capacity > SIZE_MAX / 2;
        if (!failed_) std::memset(seen_, 0, capacity_);
    }
    bool insert(uint64_t offset, const uint8_t* bytes, size_t length, bool fin) {
        constexpr uint64_t max_offset = (uint64_t(1) << 62) - 1;
        if (failed_ || (!bytes && length) || offset > max_offset || length > max_offset - offset)
            return fail();
        const uint64_t end = offset + length;
        if ((final_seen_ && end > final_size_) ||
            (fin && ((final_seen_ && end != final_size_) || end < highest_end_)))
            return fail();
        if (end > base_ && end - base_ > capacity_) return fail();
        if (fin) { final_seen_ = true; final_size_ = end; }
        highest_end_ = std::max(highest_end_, end);
        if (end <= base_) return true;
        if (offset < base_) {
            const auto skip = static_cast<size_t>(base_ - offset);
            bytes += skip; length -= skip; offset = base_;
        }
        const auto start = static_cast<size_t>(offset - base_);
        size_t physical = (head_ + start) % capacity_;
        for (size_t i = 0; i < length; ++i) {
            if (seen_[physical] && data_[physical] != bytes[i]) return fail();
            data_[physical] = data_[physical + capacity_] = bytes[i];
            seen_[physical] = 1;
            if (++physical == capacity_) physical = 0;
        }
        extent_ = std::max(extent_, start + length);
        physical = (head_ + contiguous_) % capacity_;
        while (contiguous_ < extent_ && seen_[physical]) {
            ++contiguous_;
            if (++physical == capacity_) physical = 0;
        }
        return true;
    }
    bool consume(size_t count) {
        if (failed_ || count > contiguous_) return fail();
        if (!count) return true;
        const size_t first = std::min(count, capacity_ - head_);
        std::memset(seen_ + head_, 0, first);
        std::memset(seen_, 0, count - first);
        head_ = (head_ + count) % capacity_;
        base_ += count; extent_ -= count; contiguous_ -= count;
        return true;
    }
    const uint8_t* data() const { return data_ ? data_ + head_ : nullptr; }
    size_t size() const { return contiguous_; }
    // The span beyond contiguous bytes includes a known receive gap. It is
    // diagnostic state, not consumed data or newly granted receive credit.
    size_t out_of_order_span() const { return extent_ - contiguous_; }
    bool pending() const { return extent_ != 0; } // includes known out-of-order gaps
    uint64_t consumed() const { return base_; }
    bool failed() const { return failed_; }
    bool has_final_size() const { return final_seen_; }
    bool complete() const { return !failed_ && final_seen_ && base_ + contiguous_ == final_size_; }
    bool drained() const { return complete() && !contiguous_; }
private:
    bool fail() { failed_ = true; return false; }
    uint8_t *data_, *seen_;
    size_t capacity_, head_ = 0, extent_ = 0, contiguous_ = 0;
    uint64_t base_ = 0, highest_end_ = 0, final_size_ = 0;
    bool failed_ = false, final_seen_ = false;
};
} // namespace aoq_wire
