#include "kws_fusion.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    size_t bytes=kws_fusion_size();void *memory=malloc(bytes),*saved=malloc(bytes);assert(memory && saved);
    kws_fusion_t *state;
    assert(!kws_fusion_init(memory,bytes,&kws_trained_model,&kws_secondary_model,&state));
    memcpy(saved,memory,bytes);assert(!kws_fusion_prime(state));assert(!memcmp(memory,saved,bytes));
    kws_fusion_threshold(state,INT16_MIN);
    const int16_t zero[512]={0};kws_event_t event;
    for(unsigned block=1;block<=64;++block) {
        assert(!kws_fusion_feed_512(state,zero,(uint64_t)block*512,&event));
        assert(event.detected==(block==64));
    }
    puts("other pair remains cold; exact 64-block guard retained");free(memory);free(saved);return 0;
}
