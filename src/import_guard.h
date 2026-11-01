#pragma once
#include <stdint.h>

/* An XBE kernel import is bit 31 plus an ordinal. The pinned runtime's
 * export catalog ends at ordinal 378, including its debug exports. Merely
 * having bit 31 set does not make an arbitrary DWORD an import. */
static inline int bf_valid_extra_kernel_thunk(uint32_t value) {
    return value >= UINT32_C(0x80000001) && value <= UINT32_C(0x8000017A);
}
