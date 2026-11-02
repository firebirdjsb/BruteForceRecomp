/* Compiled inside the private memory-layout translation unit so the query
 * uses the same locked allocator state as allocations. No guest globals are
 * patched. Runtime scratch/main-stack reservations remain unavailable, and
 * the high-water heap span is conservatively reserved, including freed holes.
 * The contiguous allocator and low-RAM allocator have separate backing stores;
 * their reservations must both be charged against the guest's RAM budget. */
#include "guest_memory_stats.h"
NTSTATUS __stdcall bf_query_guest_memory(PXBOX_MM_STATISTICS stats) {
    uint64_t heap_bytes, contiguous_pages, image_pages, runtime_pages;
    uint32_t total = (uint32_t)(g_xbox_total_ram / 4096u);
    if (!stats) return STATUS_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&g_heap_lock);
    heap_bytes = g_heap_next > XBOX_HEAP_BASE ? g_heap_next - XBOX_HEAP_BASE : 0;
    ReleaseSRWLockExclusive(&g_heap_lock);
    contig_lock();
    /* Page zero and the GPU instance area are permanently reserved. */
    contiguous_pages = g_contig_pages_in_use + 1u +
                       bf_pages_for_bytes(XBOX_GPU_INSTANCE_DEFAULT);
    contig_unlock();
    image_pages = g_xbox_image_hi > g_xbox_image_lo
        ? bf_pages_for_bytes(g_xbox_image_hi) - g_xbox_image_lo / 4096u : 0;
    runtime_pages = bf_pages_for_bytes(XBOX_HEAP_BASE - g_xbox_region_base);
    memset(stats, 0, sizeof(*stats));
    stats->Length = sizeof(*stats);
    stats->TotalPhysicalPages = total;
    stats->AvailablePages = bf_memory_available(total, image_pages, runtime_pages,
        bf_pages_for_bytes(heap_bytes), contiguous_pages);
    stats->VirtualMemoryBytesCommitted = (ULONG)heap_bytes;
    stats->VirtualMemoryBytesReserved = (ULONG)heap_bytes;
    stats->PoolPagesCommitted = (ULONG)contiguous_pages;
    stats->StackPagesCommitted = (ULONG)bf_pages_for_bytes(XBOX_STACK_SIZE);
    stats->ImagePagesCommitted = (ULONG)image_pages;
    if (getenv("BF_TRACE_MEMORY"))
        fprintf(stderr, "[MEMORY] total=%u available=%u image=%llu runtime=%llu heap=%llu contiguous=%llu pages\n",
            total, stats->AvailablePages, (unsigned long long)image_pages,
            (unsigned long long)runtime_pages,
            (unsigned long long)bf_pages_for_bytes(heap_bytes),
            (unsigned long long)contiguous_pages);
    return STATUS_SUCCESS;
}
