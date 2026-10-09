/* Export pixels from the firmware's actual C row renderer for visual QA. */
#include "display.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc!=3 || strlen(argv[1])!=8) return 2;
    FILE *f=fopen(argv[2],"wb");if(!f) return 3;
    const agent_display_config_t c={.mode=AGENT_DISPLAY_CLOCK,.utc_offset=480,.foreground=65535};
    fprintf(f,"P6\n160 80\n255\n");
    uint8_t row[320];
    for(unsigned y=0;y<80;++y) {
        agent_display_row(&c,argv[1],"UTC+08:00",y,row);
        for(unsigned x=0;x<160;++x) {
            unsigned v=(unsigned)row[x*2]<<8|row[x*2+1];
            uint8_t rgb[3]={(uint8_t)(((v>>11)&31)*255/31),
                (uint8_t)(((v>>5)&63)*255/63),(uint8_t)((v&31)*255/31)};
            if(fwrite(rgb,1,3,f)!=3) {fclose(f);return 4;}
        }
    }
    return fclose(f)?5:0;
}
