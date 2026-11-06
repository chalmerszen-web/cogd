#include "p4_capture_quality.hpp"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>

using namespace p4_capture;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(0)
constexpr unsigned width=160,height=120;
using Frame=std::vector<uint16_t>;
static uint16_t gray(unsigned value) {
    return uint16_t(((value>>3)<<11)|((value>>2)<<5)|(value>>3));
}
static Frame simple(unsigned ox=20,unsigned oy=30) {
    Frame f(width*height,gray(255));
    // Sparse glyph-like strokes, less than ten percent ink coverage. This
    // deliberately prevents a whole-image texture average from being the gate.
    const char *rows[]={"0100011100000000000001110", "1100000010000000000010001",
                        "0100000010011101110000001", "0100000100000000000001111",
                        "0100001000011101110000001", "0100010000000000000010001",
                        "1110011110000000000001110"};
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<25;++x)if(rows[y][x]=='1')
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx)
            if(ox+x*2+dx<width && oy+y*2+dy<height)f[(oy+y*2+dy)*width+ox+x*2+dx]=gray(20);
    return f;
}
static Frame soft_edge() {
    Frame f(width*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        unsigned value=x<60?20:x>84?235:20+(x-60)*215/24;
        f[y*width+x]=gray(value);
    }
    return f;
}
static void stable(Window &w,const Frame &frame,unsigned begin,unsigned end) {
    for(unsigned t=begin;t<end;t+=sample_ms)w.observe(frame.data(),width,height,t);
}
static void metrics_cases() {
    Grid grid{};auto sparse=simple();auto q=measure(sparse.data(),width,height,grid);
    CHECK(q.detailed());CHECK(q.edge_samples>=12);CHECK(q.mean>200);
    auto soft=soft_edge();q=measure(soft.data(),width,height,grid);
    CHECK(q.exposed());CHECK(!q.detailed());CHECK(q.sharpness<310);
    Frame blank(width*height,gray(255));CHECK(!measure(blank.data(),width,height,grid).detailed());
    Frame dark(width*height,gray(0));CHECK(!measure(dark.data(),width,height,grid).detailed());
}
static void full_resolution_sparse_cases() {
    constexpr unsigned w=800,h=640;
    CHECK(gradient_stride(w,h)==6);
    CHECK(gradient_stride(1920,1080)>6);
    auto text=simple(0,0);
    Grid grid{};
    // A short sparse expression on a full-size white sheet, including every
    // x/y phase of the dynamic stride. Two sensor pixels per source pixel.
    for(unsigned dy=0;dy<6;++dy)for(unsigned dx=0;dx<6;++dx) {
        Frame frame(w*h,gray(255));
        for(unsigned y=0;y<14;++y)for(unsigned x=0;x<50;++x)
            for(unsigned yy=0;yy<2;++yy)for(unsigned xx=0;xx<2;++xx)
                frame[(200+dy+2*y+yy)*w+300+dx+2*x+xx]=text[y*width+x];
        auto q=measure(frame.data(),w,h,grid);
        CHECK(q.stride==6);CHECK(q.gradient_points==13992);
        CHECK(q.gradient_points<=max_gradient_points);CHECK(q.mean>240);CHECK(q.detailed());
    }
}
static void window_cases() {
    auto clear=simple();Window w;w.begin(1,0);
    CHECK(w.observe(clear.data(),width,height,0).sampled);CHECK(!w.selected);
    CHECK(!w.observe(clear.data(),width,height,20).sampled);
    stable(w,clear,120,480);CHECK(!w.selected);
    stable(w,clear,480,1800);CHECK(w.selected);CHECK(w.selected_elapsed_ms>=warmup_ms);
    CHECK(w.fresh(1800));CHECK(!w.fresh(2000));
    CHECK(!w.observe(clear.data(),width,height,1800).sampled);
    // A new request cannot inherit either its prior candidate or stability.
    w.begin(2,2000);CHECK(!w.selected);CHECK(w.samples==0);
    w.observe(clear.data(),width,height,2000);CHECK(!w.selected);
    auto soft=soft_edge();w.begin(3,0);stable(w,soft,0,1800);CHECK(!w.selected);
    CHECK(w.samples>2);CHECK(std::string(w.reason)=="blurred");
}
static void final_scene_cases() {
    auto a=simple(10,20),b=simple(90,80);Window w;w.begin(4,0);
    stable(w,a,0,720);CHECK(w.selected);
    auto changed=w.observe(b.data(),width,height,720);
    CHECK(changed.discard && !w.selected);
    stable(w,b,840,1800);CHECK(w.selected);CHECK(w.selected_elapsed_ms>=960);
    // Continued movement after a selected A invalidates A until the deadline.
    w.begin(5,0);stable(w,a,0,720);CHECK(w.selected);
    for(unsigned t=720;t<1800;t+=sample_ms) {
        const auto &frame=((t/120)&1)?a:b;
        w.observe(frame.data(),width,height,t);CHECK(!w.selected);
    }
    // Loss of focus without large movement is also not permission to reuse A.
    w.begin(6,0);stable(w,a,0,720);CHECK(w.selected);
    auto soft=soft_edge();stable(w,soft,720,1800);CHECK(!w.selected);
    // Audio-priority skips / lost frames cannot make an early clear candidate
    // count as a current frame at the end of the window.
    w.begin(7,0);stable(w,a,0,720);CHECK(w.selected);CHECK(!w.fresh(1800));
}
static void candidate_age_cases() {
    CHECK(!fresh_frame_times(1800,1580,1420)); // fresh score, expired retained image
    CHECK(fresh_frame_times(1800,1580,1560)); // both within the inclusive240ms bound
    CHECK(!fresh_frame_times(1800,1559,1559));
    CHECK(!fresh_frame_times(1800,1580,1559));
    CHECK(!fresh_frame_times(1800,1801,1800));
    CHECK(!fresh_frame_times(1800,1580,1600));
    CHECK(final_frame_ready(true,false,1800,1600,1600));
    CHECK(!final_frame_ready(true,true,1800,1600,1600)); // a new motion sample is still pending
    CHECK(!final_frame_ready(false,false,1800,1600,1600));
    CHECK(!final_frame_ready(true,false,1841,1600,1600)); // draining does not refresh frame time
    // Actual Window sequence: both images are detailed and stable, but the
    // lower score at1580ms must replace the1420ms candidate. Then frames stop.
    auto lower=simple(),higher=lower;
    for(unsigned y=1;y+1<height;++y)for(unsigned x=1;x+1<width;++x) {
        unsigned v=(4*luma(lower[y*width+x])+luma(lower[y*width+x-1])+luma(lower[y*width+x+1])+
                    luma(lower[(y-1)*width+x])+luma(lower[(y+1)*width+x]))/8;
        higher[y*width+x]=gray(v);
    }
    Grid grid{};auto hi=measure(higher.data(),width,height,grid),lo=measure(lower.data(),width,height,grid);
    CHECK(hi.detailed() && lo.detailed());
    CHECK(hi.sharpness+hi.sharp_fraction>lo.sharpness+lo.sharp_fraction);
    Window w;w.begin(8,0);stable(w,higher,0,1320);
    CHECK(w.observe(higher.data(),width,height,1420).replace);CHECK(w.selected_elapsed_ms==1420);
    auto last=w.observe(lower.data(),width,height,1580);
    CHECK(last.sampled && last.replace && !last.discard);CHECK(w.selected_elapsed_ms==1580);
    CHECK(w.fresh(1600));CHECK(w.fresh(1800));CHECK(!w.fresh(1821));
    // Realistic non-ideal200ms cadence: a lower-scoring1600ms frame must not
    // leave the1400ms candidate to expire at the1800ms window boundary.
    w.begin(9,0);
    for(unsigned t=0;t<=1400;t+=200)w.observe(higher.data(),width,height,t);
    CHECK(w.selected_elapsed_ms==1400);
    CHECK(w.observe(lower.data(),width,height,1600).replace);
    CHECK(w.selected_elapsed_ms==1600 && w.fresh(1800));
    auto moved=simple(90,80);
    CHECK(w.observe(moved.data(),width,height,1720).discard);
    CHECK(!w.selected && !w.fresh(1800));
}
int main(int argc,char **argv) {
    if(argc==5 && std::string(argv[1])=="--measure-rgb565") {
        unsigned w=unsigned(std::stoul(argv[2])),h=unsigned(std::stoul(argv[3]));
        CHECK(w>=10 && h>=10 && w<=4096 && h<=4096);
        std::ifstream input(argv[4],std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        CHECK(bytes.size()==size_t(w)*h*2);
        Frame pixels(size_t(w)*h);
        for(size_t i=0;i<pixels.size();++i)pixels[i]=uint16_t(bytes[2*i]|(unsigned(bytes[2*i+1])<<8));
        Grid grid{};auto q=measure(pixels.data(),w,h,grid);
        std::cout<<"{\"mean\":"<<q.mean<<",\"range\":"<<q.range<<",\"edge_samples\":"<<q.edge_samples
                 <<",\"sharp_edges\":"<<q.sharp_edges<<",\"sharpness\":"<<q.sharpness
                 <<",\"sharp_fraction\":"<<q.sharp_fraction<<",\"detailed\":"<<(q.detailed()?"true":"false")
                 <<",\"gradient_points\":"<<q.gradient_points<<",\"gradient_stride\":"<<q.stride
                 <<",\"scope\":\"decoded_local_image_not_P4_timing_validation\"}\n";
        return 0;
    }
    CHECK(argc==1);
    metrics_cases();full_resolution_sparse_cases();window_cases();final_scene_cases();candidate_age_cases();
    std::cout<<checks<<" capture-quality checks passed (synthetic host frames only)\n";
}
