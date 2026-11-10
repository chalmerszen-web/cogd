#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define P4_DIAG_PAGE 4096u
#define P4_DIAG_HEADER 64u
#define P4_DIAG_END 4092u
#define P4_DIAG_MAGIC 0x31474c44u
#define P4_DIAG_COMMIT 0x43474c44u
#define P4_DIAG_INTERVAL_MS 2500u
#define P4_DIAG_TEXT 704u
/* Little-endian on disk; fixed offsets keep USB exports portable. */
typedef struct {
    uint32_t magic; uint16_t version,header;
    uint64_t sequence;
    uint32_t boot,used;
    uint64_t uptime_ms,unix_ms;
    uint32_t dropped,truncated,reset_reason,flags,reserved,crc;
} p4_diag_header_t;
#if defined(__cplusplus)
static_assert(sizeof(p4_diag_header_t)==P4_DIAG_HEADER,"journal header");
#else
_Static_assert(sizeof(p4_diag_header_t)==P4_DIAG_HEADER,"journal header");
#endif
static inline uint32_t p4_diag_crc_step(uint32_t crc,const void *data,size_t size) {
    const uint8_t *p=(const uint8_t *)data;
    while(size--) {crc^=*p++;for(unsigned i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    return crc;
}
static inline uint32_t p4_diag_crc(const uint8_t *page) {
    uint32_t crc=p4_diag_crc_step(~0u,page,60);
    return ~p4_diag_crc_step(crc,page+64,P4_DIAG_END-64);
}
static inline int p4_diag_valid(const uint8_t *page,p4_diag_header_t *header) {
    uint32_t commit;memcpy(&commit,page+P4_DIAG_END,4);memcpy(header,page,64);
    if(commit!=P4_DIAG_COMMIT || header->magic!=P4_DIAG_MAGIC || header->version!=1 ||
       header->header!=64 || !header->sequence || header->used<64 || header->used>P4_DIAG_END ||
       header->crc!=p4_diag_crc(page))return 0;
    size_t at=64;
    while(at<header->used) {
        if(header->used-at<12)return 0;
        uint16_t n;memcpy(&n,page+at+8,2);
        if(n>P4_DIAG_TEXT || n>header->used-at-12)return 0;
        at+=12+n;
    }
    return at==header->used;
}
static inline int p4_diag_append(uint8_t *page,uint32_t *used,uint64_t at,
                                uint8_t kind,uint8_t flags,const char *text,uint16_t n) {
    if(n>P4_DIAG_TEXT || *used<64 || *used>P4_DIAG_END || 12u+n>P4_DIAG_END-*used)return 0;
    uint8_t *p=page+*used;memcpy(p,&at,8);memcpy(p+8,&n,2);p[10]=kind;p[11]=flags;
    memcpy(p+12,text,n);*used+=12u+n;return 1;
}
static inline void p4_diag_seal(uint8_t *page,p4_diag_header_t *header) {
    header->magic=P4_DIAG_MAGIC;header->version=1;header->header=64;header->crc=0;
    memcpy(page,header,64);header->crc=p4_diag_crc(page);memcpy(page,header,64);
    uint32_t commit=P4_DIAG_COMMIT;memcpy(page+P4_DIAG_END,&commit,4);
}
