// Generic public-runtime decoders, with one host entry on Linux/Android.
#pragma once
#include "../third_party/xboxrecomp-mmio/mmio_decode.h"
#include "platform/mmio_decode_a64.h"
#if defined(__aarch64__) && !defined(_WIN32)
#define MMIO_HOST_PC(uc) A64_CTX_PC((ucontext_t *)(uc))
#define mmio_emulate_host(uc, off, dev, rd, wr) \
    mmio_emulate_a64((ucontext_t *)(uc), (off), (dev), (rd), (wr))
#endif
