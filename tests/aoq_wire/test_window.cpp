#include "aoq_stream_window.h"
#include <cassert>
#include <cstring>
#include <vector>
using aoq_wire::StreamWindow;
struct Storage {
    uint8_t bytes[64]{}, seen[32]{};
    StreamWindow window{bytes, seen, 32};
};
int main() {
    const uint8_t input[] = "abcdefghijklmnopqrstuvwxyz";
    Storage s;
    // A final fragment may precede all earlier fragments.
    assert(s.window.insert(20,input+20,6,true));
    assert(!s.window.complete() && s.window.size()==0);
    assert(s.window.insert(8,input+8,12,false));
    assert(s.window.insert(0,input,8,false));
    assert(s.window.complete() && s.window.size()==26);
    assert(std::memcmp(s.window.data(),input,26)==0);
    assert(s.window.insert(6,input+6,16,false));
    assert(s.window.consume(17));
    assert(s.window.insert(0,input,26,true));
    assert(s.window.complete() && s.window.size()==9);
    assert(std::memcmp(s.window.data(),input+17,9)==0);
    assert(s.window.consume(9) && s.window.drained());
    assert(s.window.insert(0,input,26,true) && s.window.drained());
    assert(!s.window.insert(26,input,1,false));
    Storage gap;
    assert(gap.window.insert(0,input,4,false));
    assert(gap.window.insert(8,input+8,4,false));
    assert(gap.window.consume(4) && gap.window.size()==0);
    assert(gap.window.insert(4,input+4,4,false));
    assert(gap.window.size()==8 && std::memcmp(gap.window.data(),input+4,8)==0);
    assert(!gap.window.insert(7,input,1,false)); // conflicting retained byte
    Storage final_size;
    assert(final_size.window.insert(20,input+20,6,false));
    assert(!final_size.window.insert(10,nullptr,0,true));
    Storage changed_final;
    assert(changed_final.window.insert(10,nullptr,0,true));
    assert(!changed_final.window.insert(9,nullptr,0,true));
    Storage bounds;
    assert(!bounds.window.insert(31,input,2,false));
    Storage overflow;
    assert(!overflow.window.insert((uint64_t(1)<<62)-1,input,1,false));
    Storage empty;
    assert(empty.window.insert(0,nullptr,0,true) && empty.window.drained());
    Storage invalid_consume;
    assert(!invalid_consume.window.consume(1));
    StreamWindow invalid(nullptr,nullptr,0);
    assert(invalid.failed() && !invalid.insert(0,nullptr,0,false));
    // Long streams retain a bounded window while consumed data is released.
    Storage rolling;
    for (uint64_t i=0;i<10000;++i) {
        assert(rolling.window.insert(i*26+13,input+13,13,false));
        assert(rolling.window.insert(i*26,input,13,false));
        assert(rolling.window.size()==26 && std::memcmp(rolling.window.data(),input,26)==0);
        assert(rolling.window.consume(26));
    }
    assert(rolling.window.insert(260000,nullptr,0,true) && rolling.window.drained());

    // Keep unread bytes while consuming and refilling across many ring turns.
    // A late half-packet must leave a hole, and retransmissions that straddle
    // the consumed prefix must not overwrite the retained bytes.
    std::vector<uint8_t> stream(10037);
    for (size_t i=0;i<stream.size();++i) stream[i]=uint8_t((i*37+i/251)%251);
    Storage sliding;
    size_t produced=32, consumed=0;
    assert(sliding.window.insert(0,stream.data(),produced,false));
    while (consumed<stream.size()) {
        assert(sliding.window.size()==produced-consumed);
        assert(std::memcmp(sliding.window.data(),stream.data()+consumed,produced-consumed)==0);
        size_t count=std::min(size_t(1+consumed%11),produced-consumed);
        assert(count && sliding.window.consume(count)); consumed+=count;
        size_t append=std::min(count,stream.size()-produced);
        if (append) {
            size_t split=append/2;
            assert(sliding.window.insert(produced+split,stream.data()+produced+split,append-split,false));
            assert(sliding.window.size()==produced-consumed+(split?0:append));
            assert(sliding.window.insert(produced,stream.data()+produced,split,false));
            produced+=append;
            size_t old=consumed>7?consumed-7:0;
            assert(sliding.window.insert(old,stream.data()+old,produced-old,false));
        }
    }
    assert(sliding.window.insert(produced,nullptr,0,true) && sliding.window.drained());

    Storage wrapped_conflict;
    assert(wrapped_conflict.window.insert(0,stream.data(),32,false));
    assert(wrapped_conflict.window.consume(23));
    assert(wrapped_conflict.window.insert(32,stream.data()+32,23,false));
    assert(!wrapped_conflict.window.insert(35,stream.data()+36,1,false));
}
