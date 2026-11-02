// Windows D3D12 adapter for the API-independent guest completion timeline.
// No GPU work is considered completed until ID3D12Fence reports it.
#pragma once
#if !defined(_WIN32)
#error D3D12 completion bridge is Windows-only
#endif
#include <cstddef>
#include <cstdint>
#include <limits>
#include <d3d12.h>
#include "guest_completion_timeline.h"

namespace bf::gpu {
// Must be called AFTER issuing the draw/copy commands that produce the
// associated guest-visible result to this same D3D12 queue. Queue::Signal
// enqueues the GPU timeline marker; a successful HRESULT is not completion.
inline HRESULT signal_gpu_queue(ID3D12CommandQueue *queue, ID3D12Fence *fence,
                                GuestCompletionTimeline &timeline,
                                std::uint64_t value) {
    if (!queue || !fence || value == 0 ||
        value == std::numeric_limits<std::uint64_t>::max() ||
        value <= timeline.submitted())
        return E_INVALIDARG;
    const HRESULT result = queue->Signal(fence, value);
    if (FAILED(result))
        return result;
    return timeline.register_submit(value) ? S_OK : E_FAIL;
}

// In DX12, UINT64_MAX means device removal, never an infinitely complete
// fence. Neither this adapter nor the timeline writes arbitrary guest memory:
// the caller supplies a validated DWORD writer for its existing memory model.
template<class WriteGuestDword>
RetireResult poll_gpu_queue(ID3D12Fence *fence, GuestCompletionTimeline &timeline,
                            WriteGuestDword writer, std::size_t *retired = nullptr) {
    if (!fence)
        return RetireResult::invalid_fence;
    return timeline.retire(fence->GetCompletedValue(), writer, retired);
}
} // namespace bf::gpu
