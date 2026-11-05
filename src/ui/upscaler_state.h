#pragma once
#include <stdint.h>

enum {
    BF_UPSCALER_OFF = 0,
    BF_UPSCALER_FSR1 = 1,
    BF_UPSCALER_FSR3 = 3,
    BF_UPSCALER_FSR4 = 4
};

#ifdef __cplusplus
extern "C" {
#endif
unsigned bf_upscaler_mode(void);
void bf_upscaler_initial(unsigned mode);
int bf_upscaler_select(unsigned mode);
/* A negative capability leaves that bit unchanged; zero clears and one sets. */
void bf_upscaler_capabilities(int spatial,int fsr3,int fsr4);
void bf_upscaler_spatial_supported(int spatial);
/* Bit N is set when upscaler major N is available; bit zero is always set. */
unsigned bf_upscaler_capabilities_mask(void);
void bf_upscaler_status(unsigned actual_major,const char *provider,const char *reason);
void bf_upscaler_status_copy(char *buffer,unsigned capacity);
#ifdef __cplusplus
}
#endif
