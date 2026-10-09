/* Offline proposal only: exact existing C high-pass, raw LE PCM16 in/out. */
#include "tonal.h"
#include <stdio.h>

int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    FILE *in=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");
    if(!in || !out)return 3;
    agent_biquad_t state={0};unsigned count=0;
    for(;;) {
        int lo=fgetc(in);if(lo==EOF)break;
        int hi=fgetc(in);if(hi==EOF || count==160000)return 4;
        int16_t input=(int16_t)((unsigned)lo|(unsigned)hi<<8);
        int16_t output=tonal_highpass_sample(&state,input);
        fputc((uint8_t)output,out);fputc((uint16_t)output>>8,out);++count;
    }
    if(!count || ferror(in) || ferror(out))return 5;
    if(fclose(in) || fclose(out))return 6;
    printf("samples=%u state_bytes=%zu\n",count,sizeof(state));return 0;
}
