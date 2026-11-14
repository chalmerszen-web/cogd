#include "p4_capture_store.h"
#include <vector>
#include <cassert>
#include <iostream>
static int allocations=0,fail_after=-1;
static void *fragmented(size_t bytes) {
    assert(bytes<=65536);
    if(fail_after>=0 && allocations++>=fail_after)return nullptr;
    return std::malloc(bytes);
}
static void *contiguous(size_t bytes) {
    if(fail_after>=0 && allocations++>=fail_after)return nullptr;
    return std::malloc(bytes);
}
static size_t result_request=0;
static void *fragmented_result(size_t bytes) {
    result_request=bytes;
    return bytes<=1024*1024?std::malloc(bytes):nullptr;
}
int main() {
    // A two-megabyte output cannot be allocated, but an existing stopped DMA
    // buffer can be detached at its actual JPEG length. It must remain intact
    // on failure, and the detached result must survive sensor buffer reuse.
    std::vector<unsigned char> encoded(2*1024*1024);
    const size_t jpeg_bytes=616243;
    for(size_t i=0;i<encoded.size();++i)encoded[i]=static_cast<unsigned char>(i*17+i/251);
    assert(!fragmented_result(encoded.size()));
    auto detached=static_cast<unsigned char *>(p4_capture::copy_encoded_result(encoded.data(),jpeg_bytes,encoded.size(),fragmented_result));
    assert(detached && result_request==jpeg_bytes && !std::memcmp(detached,encoded.data(),jpeg_bytes));
    std::memset(encoded.data(),0,encoded.size());
    for(size_t i=0;i<jpeg_bytes;++i)assert(detached[i]==static_cast<unsigned char>(i*17+i/251));
    std::free(detached);
    result_request=0;
    assert(!p4_capture::copy_encoded_result(encoded.data(),encoded.size()+1,encoded.size(),fragmented_result));
    assert(!p4_capture::copy_encoded_result(encoded.data(),0,encoded.size(),fragmented_result));
    assert(!p4_capture::copy_encoded_result(nullptr,1,encoded.size(),fragmented_result));
    assert(!result_request);
    assert(!p4_capture::copy_encoded_result(encoded.data(),encoded.size(),encoded.size(),fragmented_result));
    assert(encoded.front()==0 && encoded.back()==0);
    p4_capture::FramePair pair;
    assert(pair.allocate(1920*1080*2,contiguous));
    auto first=pair.frame(0),second=pair.frame(1);
    assert(first && second && first!=second && !pair.frame(2));
    // After unrelated allocations fragment the heap, repeated full-resolution
    // captures must retain both buffers without another large allocation.
    fail_after=0;allocations=0;
    for(unsigned i=0;i<20;++i){assert(pair.allocate(1920*1080*2,contiguous));assert(pair.frame(0)==first && pair.frame(1)==second);}
    assert(!allocations);assert(pair.allocate(640*480*2,contiguous));
    pair.clear();assert(!pair.capacity() && !pair.frame(0));
    fail_after=1;allocations=0;assert(!pair.allocate(1920*1080*2,contiguous));
    assert(!pair.capacity() && !pair.frame(0) && !pair.frame(1));
    fail_after=-1;assert(pair.allocate(1920*1080*2,contiguous));pair.clear();
    p4_capture::FrameStore frame;
    // Full 1080p RGB565; unique pixels cross block and final partial boundaries.
    std::vector<unsigned char> input(1920*1080*2),out(input.size());
    for(size_t i=0;i<input.size();++i)input[i]=static_cast<unsigned char>((i*31+i/65536)%251);
    assert(frame.allocate(input.size(),fragmented));assert(frame.copy_from(input.data(),input.size()));
    assert(!frame.copy_to(out.data(),out.size()-1));assert(frame.copy_to(out.data(),out.size()));assert(input==out);
    // A later best-frame replacement and an allocation failure cannot expose
    // a partial/old photograph as the selected source.
    for(auto &b:input)b^=0xa5;
    assert(frame.copy_from(input.data(),input.size()));assert(frame.copy_to(out.data(),out.size()));assert(input==out);
    fail_after=5;allocations=0;assert(!frame.allocate(input.size(),fragmented));assert(!frame.copy_to(out.data(),out.size()));
    fail_after=-1;assert(frame.allocate(input.size(),fragmented));assert(frame.copy_from(input.data(),input.size()));
    assert(frame.copy_to(out.data(),out.size()));assert(input==out);frame.clear();assert(!frame.copy_from(input.data(),input.size()));
    std::cout<<"PASS fragmented full-resolution photo storage\n";
}
