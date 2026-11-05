#include "../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_board/p4_document_state.h"
#include "../ESP32-P4-WIFI6-Touch-LCD-3.5/components/p4_board/p4_math_text.hpp"
#include <cassert>
#include <climits>
#include <iostream>

int main() {
    std::atomic<unsigned> generation{9};std::atomic<int> owner{3};int activated=0;
    assert(p4_begin_transaction(generation,owner,5,[]{return false;},[&](unsigned){++activated;})==0);
    assert(generation==9 && owner==3 && activated==0); // failed lock preserves the portrait
    assert(p4_begin_transaction(generation,owner,5,[]{return true;},[&](unsigned g){assert(g==10);++activated;})==10);
    assert(generation==10 && owner==5 && activated==1);
    generation=UINT_MAX;
    assert(p4_begin_transaction(generation,owner,5,[]{return true;},[](unsigned){})==1);
    bool ready=true;unsigned frames=4;
    assert(!p4_refresh_transaction(ready,frames,1,generation,owner,[]{},[]{return true;}));
    assert(!ready); // invalidation without RENDER_READY cannot claim rendered
    assert(p4_refresh_transaction(ready,frames,1,generation,owner,[&]{++frames;},[]{return true;}));
    assert(!p4_refresh_transaction(ready,frames,1,generation,owner,[&]{++frames;++generation;},[]{return true;}));
    assert(!ready); // a newer view wins even if the old frame completed
    assert(!p4_refresh_transaction(ready,frames,2,generation,owner,[&]{++frames;},[]{return false;}));
    assert(!ready); // hidden parent never commits ready
    p4_document_bookmark_t b{12, 3, 2, 5, 431, 31, true};
    assert(p4_bookmark_target(&b,12,3,true,400,1000)==431);
    assert(p4_bookmark_target(&b,12,3,true,450,1000)==481); // reflow keeps anchor offset
    assert(p4_bookmark_target(&b,12,3,true,-1,1000)==431); // unavailable anchor
    assert(p4_bookmark_target(&b,13,3,true,400,1000)==0); // new question
    assert(p4_bookmark_target(&b,12,4,true,400,1000)==0); // new revision
    assert(p4_bookmark_target(&b,12,3,false,400,1000)==0); // explicit initial view
    assert(p4_bookmark_target(&b,12,3,true,900,500)==500);
    b.paragraph_offset=-12;
    assert(p4_bookmark_target(&b,12,3,true,12,1000)==0); // first paragraph padding
    b.paragraph_offset=INT_MAX;
    assert(p4_bookmark_target(&b,12,3,true,INT_MAX,1000)==1000);
    b.valid=false;
    assert(p4_bookmark_target(&b,12,3,true,400,1000)==0);
    assert(p4_document_is_visible(3,3,5,true,false));
    assert(!p4_document_is_visible(0,0,5,true,false));
    assert(!p4_document_is_visible(3,4,5,true,false));
    assert(!p4_document_is_visible(3,3,3,true,false)); // portrait owns display
    assert(!p4_document_is_visible(3,3,5,false,false)); // construction/refresh failed
    assert(!p4_document_is_visible(3,3,5,true,true)); // parent hidden / overlay

    auto all=[](uint32_t){return true;};
    auto old_font=[](uint32_t c){return c < 0x2070 || c > 0x208e;};
    const std::string source="当 x→0⁺ 时，x⁻² 与 a₁₂；π≠∞。";
    assert(p4_math::display(source.c_str(),all)==source);
    assert(p4_math::display(source.c_str(),old_font)=="当 x→0^(+) 时，x^(-2) 与 a_(12)；π≠∞。");
    assert(source=="当 x→0⁺ 时，x⁻² 与 a₁₂；π≠∞。"); // retained source unchanged
    assert(p4_math::display("x⁽ⁿ⁾",[](uint32_t c){return c!=0x207f;})=="x^((n))");
    assert(p4_math::display("xⁿ⁺¹",old_font)=="x^(n+1)");
    assert(p4_math::display("𝑥",[](uint32_t){return false;})=="[U+1D465]");
    auto ascii=[](uint32_t c){return c<128;};
    const std::string selected="xᵃ lnᵇ x";
    assert(p4_math::display(selected.c_str(),ascii)=="x^(a) ln^(b) x");
    assert(p4_math::display(selected.c_str(),all)==selected);
    assert(selected=="xᵃ lnᵇ x");
    assert(p4_math::display("xᵃ⁺¹",ascii)=="x^(a+1)");
    assert(p4_math::display("aₙ₊₁",ascii)=="a_(n+1)");
    assert(p4_math::display("xᵃₙ",ascii)=="x^(a)_(n)");
    assert(p4_math::display("xᴬ⁻ᵇ",ascii)=="x^(A-b)");
    assert(p4_math::display("x𐞥",ascii)=="x^(q)"); // non-BMP Latin superscript
    assert(p4_math::display("xᵦ",ascii)=="x[U+1D66]"); // do not guess an unmapped Greek symbol
    std::string boundary(766,' ');boundary+="xᵃ⁺¹";
    assert(p4_math::display(boundary.c_str(),ascii)==std::string(766,' ')+"x^(a+1)");
    assert(p4_math::display("x\n\t=0",[](uint32_t c){return c>=32 && c<127;})=="x\n\t=0");
    std::cout << "P4 document identity, viewport and math display tests passed\n";
}
