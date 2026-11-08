#include "aoq_packet_queue.h"
#include <cassert>
#include <cstring>
#include <thread>

int main() {
    aoq::PacketQueue<4,16> small;
    assert(!small.front() && !small.release());
    assert(!small.publish(0) && !small.publish(17));
    for(unsigned i=0;i<4;++i){auto* p=small.write_slot();assert(p);p->data[0]=uint8_t(i);assert(small.publish(1));}
    assert(!small.write_slot() && !small.publish(1) && small.peak()==4);
    const auto* held=small.front();assert(held->data[0]==0);
    for(unsigned i=0;i<4;++i){const auto* p=small.front();assert(p && p->size==1 && p->data[0]==i);assert(small.release());}
    assert(!small.front());

    // Exercise producer/consumer overlap, retained front ownership, variable
    // sizes and many slot wraps. Every byte must arrive once and in order.
    aoq::PacketQueue<8,128> queue;
    constexpr uint32_t count=100000;
    std::thread producer([&]{
        for(uint32_t i=0;i<count;++i){
            decltype(queue.write_slot()) p;
            while(!(p=queue.write_slot()))std::this_thread::yield();
            const size_t n=4+i%125;
            std::memcpy(p->data.data(),&i,4);
            for(size_t k=4;k<n;++k)p->data[k]=uint8_t(i+k);
            assert(queue.publish(n));
        }
    });
    for(uint32_t i=0;i<count;++i){
        decltype(queue.front()) p;
        while(!(p=queue.front()))std::this_thread::yield();
        if(i%7==0)std::this_thread::yield();
        uint32_t actual=0;std::memcpy(&actual,p->data.data(),4);
        assert(actual==i && p->size==4+i%125);
        for(size_t k=4;k<p->size;++k)assert(p->data[k]==uint8_t(i+k));
        assert(queue.release());
    }
    producer.join();assert(!queue.front() && queue.peak()<=8);
}
