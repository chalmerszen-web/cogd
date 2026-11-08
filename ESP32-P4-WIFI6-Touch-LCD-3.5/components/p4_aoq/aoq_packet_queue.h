#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace aoq {
// One socket reader and one protocol owner. A producer never overwrites a
// packet until the consumer releases it; publication transfers byte ownership.
template<size_t Count, size_t Bytes> class PacketQueue {
    static_assert(Count>0 && (Count&(Count-1))==0 && Count<UINT32_MAX/2 && Bytes>0 && Bytes<=UINT16_MAX);
public:
    struct Packet { uint16_t size=0; std::array<uint8_t,Bytes> data; };
    Packet* write_slot() {
        const auto w=written_.load(std::memory_order_relaxed);
        if(w-read_.load(std::memory_order_acquire)>=Count)return nullptr;
        return &packets_[w%Count];
    }
    bool publish(size_t size) {
        auto* p=write_slot();if(!p || !size || size>Bytes)return false;
        p->size=static_cast<uint16_t>(size);
        const auto w=written_.load(std::memory_order_relaxed)+1;
        written_.store(w,std::memory_order_release);
        const auto depth=w-read_.load(std::memory_order_acquire);
        if(depth>peak_.load(std::memory_order_relaxed))peak_.store(depth,std::memory_order_relaxed);
        return true;
    }
    const Packet* front() const {
        const auto r=read_.load(std::memory_order_relaxed);
        return r==written_.load(std::memory_order_acquire)?nullptr:&packets_[r%Count];
    }
    bool release() {
        if(!front())return false;
        read_.fetch_add(1,std::memory_order_release);return true;
    }
    unsigned peak() const {return peak_.load(std::memory_order_relaxed);}
    // Consumer-side snapshot; the producer may publish more immediately.
    unsigned pending() const {
        return written_.load(std::memory_order_acquire)-read_.load(std::memory_order_relaxed);
    }
private:
    std::array<Packet,Count> packets_;
    std::atomic<uint32_t> written_{0},read_{0},peak_{0};
};
} // namespace aoq
