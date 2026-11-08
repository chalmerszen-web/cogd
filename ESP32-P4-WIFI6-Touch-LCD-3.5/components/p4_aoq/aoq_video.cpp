#include "aoq_video.h"
namespace aoq_wire {
namespace {
constexpr uint64_t MaxVarint=(uint64_t(1)<<62)-1;
void vint(std::vector<uint8_t>& out,uint64_t value){
    const unsigned n=value<64?1:value<16384?2:value<(uint64_t(1)<<30)?4:8;
    const uint8_t prefix=n==1?0:n==2?64:n==4?128:192;
    for(unsigned i=n;i;--i)out.push_back(uint8_t(value>>((i-1)*8))|(i==n?prefix:0));
}
}
Track jpeg_track(uint32_t width,uint32_t height,uint16_t fps){
    MediaParam p;p.codec=1;p.schema.clear();p.video_width=width;p.video_height=height;p.video_fps=fps;
    return {"model_video_in",0,101,{p}};
}
bool encode_jpeg_properties(uint64_t timestamp,uint64_t picture,std::vector<uint8_t>& out){
    out.clear();if(timestamp>MaxVarint||picture>MaxVarint)return false;
    vint(out,6);vint(out,timestamp);vint(out,32-6);vint(out,1);vint(out,34-32);vint(out,picture);return true;
}
bool jpeg_dimensions_match(const uint8_t* p,size_t n,uint32_t width,uint32_t height){
    if(!p||n<4||n>65536||p[0]!=255||p[1]!=216||p[n-2]!=255||p[n-1]!=217||!width||!height)return false;
    size_t at=2;bool sof=false;
    while(at+1<n){
        if(p[at++]!=255)return false;
        while(at<n&&p[at]==255)++at;
        if(at>=n)return false;
        const uint8_t marker=p[at++];
        if(marker==0xda)return sof; // Entropy validation is the JPEG decoder's job.
        if(marker==0||marker==0xd8||marker==0xd9||(marker>=0xd0&&marker<=0xd7)||at+2>n)return false;
        const size_t len=(size_t(p[at])<<8)|p[at+1];
        if(len<2||len>n-at)return false;
        if(marker>=0xc0&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc){
            if(marker!=0xc0||sof||len!=17||p[at+2]!=8||p[at+7]!=3)return false;
            if(((uint32_t(p[at+3])<<8)|p[at+4])!=height||((uint32_t(p[at+5])<<8)|p[at+6])!=width)return false;
            sof=true;
        }
        at+=len;
    }
    return false;
}
bool encode_jpeg_stream(const uint8_t* p,size_t n,uint32_t width,uint32_t height,uint64_t timestamp,uint64_t group,std::vector<uint8_t>& out){
    out.clear();if(group>MaxVarint||!jpeg_dimensions_match(p,n,width,height))return false;
    SubgroupHeader h;h.track_alias=101;h.group=group;h.priority=128;h.subgroup_id_mode=2;
    std::vector<uint8_t> props,encoded;ObjectCursor cursor;
    if(!encode_subgroup_header(h,out)||!encode_jpeg_properties(timestamp,0,props)||
       !encode_object(0,0,true,props,p,n,cursor,encoded)){out.clear();return false;}
    out.insert(out.end(),encoded.begin(),encoded.end());
    // Explicit EndOfGroup object followed by QUIC FIN, including for the last
    // frame. Each all-intra JPEG owns its own bounded group/stream.
    props.clear();if(!encode_object(1,3,true,props,nullptr,0,cursor,encoded)){out.clear();return false;}
    out.insert(out.end(),encoded.begin(),encoded.end());return true;
}
}
