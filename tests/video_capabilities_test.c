/* Real private-build AV function, without a running title or retail assets. */
#include "kernel.h"
#include <stdio.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
int main(void) {
    ULONG pack=0,caps=0,mode=0;
    xbox_AvSendTVEncoderOption(NULL,AV_OPTION_QUERY_AVPACK,0,&pack);
    CHECK((pack&0xFFu)==AV_PACK_HDTV);
    CHECK((pack&0xFF00u)==(AV_STANDARD_NTSC_M<<AV_STANDARD_SHIFT));
    CHECK((pack&0x00C00000u)==AV_REFRESH_60Hz);
    CHECK((pack&0x000A0000u)==0x000A0000u);
    CHECK(!(pack&0x00040000u)); /* No new 1080i capability is advertised. */
    xbox_AvSendTVEncoderOption(NULL,AV_OPTION_QUERY_AV_CAPABILITIES,0,&caps);
    CHECK((caps&AV_FLAGS_HDTV_480p)&&(caps&AV_FLAGS_HDTV_720p)&&(caps&AV_FLAGS_WIDESCREEN));
    xbox_AvSendTVEncoderOption(NULL,AV_OPTION_QUERY_MODE_CAPS,0,&mode);
    CHECK(mode==caps);
    xbox_AvSendTVEncoderOption(NULL,AV_OPTION_SET_MODE,0x12345678u,&mode);
    CHECK(mode==0);
    xbox_AvSendTVEncoderOption(NULL,AV_OPTION_QUERY_MODE,0,&mode);
    CHECK(mode==0x12345678u);
    xbox_AvSendTVEncoderOption(NULL,AV_OPTION_QUERY_AVPACK,0,NULL);
    puts("Actual AV encoder preserves pack, region, refresh, mode state and low-bit caps; includes 480p/720p mode flags.");
    return 0;
}
