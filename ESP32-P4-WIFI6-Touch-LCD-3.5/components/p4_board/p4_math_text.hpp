#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace p4_math {
inline uint32_t next(const char *&p) {
    unsigned c = static_cast<unsigned char>(*p++);
    if (c < 128) return c;
    unsigned n = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
    if (!n) return 0xfffd;
    c &= (1u << (6 - n)) - 1;
    while (n--) {
        unsigned t = static_cast<unsigned char>(*p);
        if ((t & 0xc0) != 0x80) return 0xfffd;
        ++p; c = (c << 6) | (t & 63);
    }
    return c;
}
inline char script(uint32_t c, bool &sub) {
    sub = c >= 0x2080 && c <= 0x208e;
    if (sub) return "0123456789+-=()"[c - 0x2080];
    if (c == 0xb9) return '1';
    if (c == 0xb2) return '2';
    if (c == 0xb3) return '3';
    if (c == 0x2070) return '0';
    if (c == 0x2071) return 'i';
    if (c == 0x207f) return 'n';
    if (c >= 0x2074 && c <= 0x207e) return "456789+-=()"[c - 0x2074];
    // Unicode 16.0 compatibility decompositions whose <super>/<sub> base is
    // one ASCII Latin letter. Preserve case; never strip accents or guess at
    // phonetic/Greek symbols. Ordinal indicators stay unchanged when present
    // in the font, as do all other fully supported script runs.
    struct Latin {uint32_t code;char base;bool sub;};
    static constexpr Latin latin[] = {
        {0xAA,'a',false}, {0xBA,'o',false}, {0x2B0,'h',false}, {0x2B2,'j',false},
        {0x2B3,'r',false}, {0x2B7,'w',false}, {0x2B8,'y',false}, {0x2E1,'l',false},
        {0x2E2,'s',false}, {0x2E3,'x',false}, {0x1D2C,'A',false}, {0x1D2E,'B',false},
        {0x1D30,'D',false}, {0x1D31,'E',false}, {0x1D33,'G',false}, {0x1D34,'H',false},
        {0x1D35,'I',false}, {0x1D36,'J',false}, {0x1D37,'K',false}, {0x1D38,'L',false},
        {0x1D39,'M',false}, {0x1D3A,'N',false}, {0x1D3C,'O',false}, {0x1D3E,'P',false},
        {0x1D3F,'R',false}, {0x1D40,'T',false}, {0x1D41,'U',false}, {0x1D42,'W',false},
        {0x1D43,'a',false}, {0x1D47,'b',false}, {0x1D48,'d',false}, {0x1D49,'e',false},
        {0x1D4D,'g',false}, {0x1D4F,'k',false}, {0x1D50,'m',false}, {0x1D52,'o',false},
        {0x1D56,'p',false}, {0x1D57,'t',false}, {0x1D58,'u',false}, {0x1D5B,'v',false},
        {0x1D62,'i',true}, {0x1D63,'r',true}, {0x1D64,'u',true}, {0x1D65,'v',true},
        {0x1D9C,'c',false}, {0x1DA0,'f',false}, {0x1DBB,'z',false},
        {0x2090,'a',true}, {0x2091,'e',true}, {0x2092,'o',true}, {0x2093,'x',true},
        {0x2095,'h',true}, {0x2096,'k',true}, {0x2097,'l',true}, {0x2098,'m',true},
        {0x2099,'n',true}, {0x209A,'p',true}, {0x209B,'s',true}, {0x209C,'t',true},
        {0x2C7C,'j',true}, {0x2C7D,'V',false}, {0xA7F2,'C',false}, {0xA7F3,'F',false},
        {0xA7F4,'Q',false}, {0x107A5,'q',false}
    };
    for(const auto &entry:latin)if(c==entry.code){sub=entry.sub;return entry.base;}
    return 0;
}

/* Used only for the display copy. A missing script run keeps its grouping and
 * sign in ASCII; the retained mathematical source is never normalized. */
template<class Available> std::string display(const char *text, Available available) {
    std::string result;
    while (*text) {
        const char *start = text;
        uint32_t cp = next(text); bool sub = false;
        char value = script(cp, sub);
        if (value) {
            std::string ascii(1, value); bool missing = !available(cp);
            while (*text) {
                const char *after = text; uint32_t more = next(after); bool next_sub = false;
                char digit = script(more, next_sub);
                if (!digit || next_sub != sub) break;
                ascii += digit; missing |= !available(more); text = after;
            }
            if (missing) result += std::string(sub ? "_(" : "^(") + ascii + ")";
            else result.append(start, text - start);
        } else if (cp == '\n' || cp == '\r' || cp == '\t' || available(cp)) result.append(start, text - start);
        else {
            char unknown[20]; std::snprintf(unknown, sizeof(unknown), "[U+%04X]", unsigned(cp)); result += unknown;
        }
    }
    return result;
}
}
