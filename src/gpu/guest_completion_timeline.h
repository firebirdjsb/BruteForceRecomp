// Backend-neutral GPU completion ordering for future D3D11/D3D12/Vulkan.
// This tracker is NOT connected to the guest runtime yet. The backend must
// report real GPU completion before the caller writes a guest semaphore.
// No host pointers, D3D handles, or API-specific types are stored here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>

namespace bf::gpu {

enum class RetireResult { ok, invalid_fence, device_lost, guest_write_failed };

struct GuestSignal {
    std::uint64_t fence_value;
    std::uint32_t guest_address;
    std::uint32_t guest_value;
};

// Single-submission-stream contract. Call on the GPU command thread or guard
// with an external mutex. One instance per independently ordered GPU queue.
// A queue signal must actually be submitted successfully before register_submit.
class GuestCompletionTimeline {
public:
    bool register_submit(std::uint64_t fence) {
        if (lost_ || !fence || fence == max_fence ||
            fence <= submitted_)
            return false;
        submitted_ = fence;
        return true;
    }

    bool enqueue_signal(std::uint64_t fence, std::uint32_t address,
                        std::uint32_t value) {
        if (lost_ || !fence || fence > submitted_ ||
            fence < last_enqueued_ || (address & 3u) != 0u)
            return false;
        pending_.push_back({fence, address, value});
        last_enqueued_ = fence;
        return true;
    }

    // completed must come from actual queue completion observation,
    // e.g. ID3D12Fence::GetCompletedValue, not Queue::Signal's HRESULT.
    // A failed guest write leaves that entry queued for a safe retry.
    template<class WriteGuestDword>
    RetireResult retire(std::uint64_t completed, WriteGuestDword writer,
                        std::size_t *retired = nullptr) {
        if (retired) *retired = 0;
        if (lost_ || completed == max_fence) {
            lost_ = true;  // D3D12 uses UINT64_MAX for device removal.
            return RetireResult::device_lost;
        }
        if (completed < completed_ || completed > submitted_)
            return RetireResult::invalid_fence;
        completed_ = completed;
        while (!pending_.empty() && pending_.front().fence_value <= completed_) {
            const auto &p = pending_.front();
            if (!writer(p.guest_address, p.guest_value))
                return RetireResult::guest_write_failed;
            pending_.pop_front();
            if (retired) ++*retired;
        }
        return RetireResult::ok;
    }

    std::size_t pending() const { return pending_.size(); }
    std::uint64_t submitted() const { return submitted_; }
    std::uint64_t completed() const { return completed_; }
    bool device_lost() const { return lost_; }
private:
    static constexpr std::uint64_t max_fence =
        std::numeric_limits<std::uint64_t>::max();
    std::deque<GuestSignal> pending_;
    std::uint64_t submitted_ = 0;
    std::uint64_t completed_ = 0;
    std::uint64_t last_enqueued_ = 0;
    bool lost_ = false;
};
}  // namespace bf::gpu
