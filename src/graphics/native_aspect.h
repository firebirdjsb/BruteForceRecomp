#ifndef BF_NATIVE_ASPECT_H
#define BF_NATIVE_ASPECT_H
#include <stdint.h>

/* Host-stack camera transaction only. Every address remains a dynamically read guest
 * uint32 pointer. No guest allocation is introduced by the private experiment. */
typedef struct BFAspectTransaction {
    uint32_t camera, projection, entry_stack, return_address;
    uint32_t lock_held, changed;
    /* Projection +14,+24,+2C,+38,+40/+44/+48: seven owned 4-byte words. */
    uint32_t owned_projection[7];
    /* +18 vertical angle, +28 vertical tangent, +3C vertical pixel scale. */
    uint32_t vertical[3];
    /* +1C/+20 clip distances and +30/+34 half viewport must stay unchanged. */
    uint32_t clip[2], viewport[2];
    /* Six original 16-byte planes at camera +0x58 through +0xB7. */
    unsigned char frustum[0x60];
    /* Helper-only scratch below the incoming return-address/argument block. */
    unsigned char scratch[0x40];
} BFAspectTransaction;

#ifdef __cplusplus
extern "C" {
#define BF_ASPECT_NOEXCEPT noexcept
#else
#define BF_ASPECT_NOEXCEPT
#endif
int bf_native_aspect_prepare(uint32_t stack, BFAspectTransaction *transaction) BF_ASPECT_NOEXCEPT;
int bf_native_aspect_helpers_done(BFAspectTransaction *transaction, int helpers_ok) BF_ASPECT_NOEXCEPT;
int bf_native_aspect_restore(BFAspectTransaction *transaction) BF_ASPECT_NOEXCEPT;
void bf_native_aspect_state_failure(const char *phase) BF_ASPECT_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef BF_ASPECT_NOEXCEPT
#endif
