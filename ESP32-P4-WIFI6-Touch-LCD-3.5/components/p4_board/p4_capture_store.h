#pragma once
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace p4_capture {
// Detach only the encoded bytes before returning the stopped sensor's buffers.
// The returned allocation belongs to the caller; the source always stays with
// the capture owner, including on invalid size or allocation failure.
inline void *copy_encoded_result(const void *source,size_t used,size_t capacity,
                                 void *(*alloc)(size_t)) {
    if(!source || !used || used>capacity || !alloc)return nullptr;
    void *result=alloc(used);
    if(result)std::memcpy(result,source,used);
    return result;
}
// CSI requires two contiguous frames even when one is only its fallback.
// Retain these caller-owned frames across photo requests so transient network
// allocations cannot split their memory before the next capture. Only the
// capture owner may clear them, after STREAMOFF and JPEG borrow release.
class FramePair {
    std::array<void *,2> frames{};
    size_t bytes=0;
public:
    FramePair()=default;
    FramePair(const FramePair&)=delete;
    FramePair& operator=(const FramePair&)=delete;
    ~FramePair(){clear();}
    void clear(){for(auto &p:frames){std::free(p);p=nullptr;}bytes=0;}
    bool allocate(size_t size,void *(*alloc)(size_t)) {
        if(!size || size>8*1024*1024)return false;
        if(bytes>=size)return true;
        clear();
        for(auto &p:frames)if(!(p=alloc(size))){clear();return false;}
        bytes=size;return true;
    }
    void *frame(unsigned index) const {return index<frames.size()?frames[index]:nullptr;}
    size_t capacity() const {return bytes;}
};
// The sensor already owns two contiguous DMA frames. Retain the selected full
// resolution frame in small blocks, then copy into a stopped sensor DMA buffer.
// This avoids requiring a third simultaneous multi-megabyte contiguous block.
class FrameStore {
    static constexpr size_t block_bytes=65536,max_blocks=128;
    std::array<void *,max_blocks> blocks{};
    size_t bytes=0;
public:
    FrameStore()=default;
    FrameStore(const FrameStore&)=delete;
    FrameStore& operator=(const FrameStore&)=delete;
    ~FrameStore(){clear();}
    void clear(){for(auto &p:blocks){std::free(p);p=nullptr;}bytes=0;}
    bool allocate(size_t size,void *(*alloc)(size_t)) {
        clear();if(!size || size>block_bytes*max_blocks)return false;
        for(size_t i=0;i<(size+block_bytes-1)/block_bytes;++i) {
            blocks[i]=alloc(std::min(block_bytes,size-i*block_bytes));
            if(!blocks[i]){clear();return false;}
        }
        bytes=size;return true;
    }
    bool copy_from(const void *source,size_t size) {
        if(!source || !bytes || size!=bytes)return false;
        auto p=static_cast<const unsigned char *>(source);
        for(size_t i=0;i*block_bytes<bytes;++i)std::memcpy(blocks[i],p+i*block_bytes,std::min(block_bytes,bytes-i*block_bytes));
        return true;
    }
    bool copy_to(void *destination,size_t size) const {
        if(!destination || !bytes || size<bytes)return false;
        auto p=static_cast<unsigned char *>(destination);
        for(size_t i=0;i*block_bytes<bytes;++i)std::memcpy(p+i*block_bytes,blocks[i],std::min(block_bytes,bytes-i*block_bytes));
        return true;
    }
};
}
