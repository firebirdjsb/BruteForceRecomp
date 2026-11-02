#pragma once
#include <stdint.h>

/* Xbox memory statistics count 4096-byte pages, independent of host RAM.
 * Round reservations up and saturate before subtraction: a constrained guest
 * must report zero available pages rather than wrap into a huge allocation. */
static uint64_t bf_pages_for_bytes(uint64_t bytes) {
    return bytes / 4096u + (bytes % 4096u != 0);
}
static uint32_t bf_memory_available(uint32_t total, uint64_t image,
                                    uint64_t runtime, uint64_t heap,
                                    uint64_t contiguous) {
    const uint64_t used = image + runtime + heap + contiguous;
    return used < total ? total - (uint32_t)used : 0;
}
