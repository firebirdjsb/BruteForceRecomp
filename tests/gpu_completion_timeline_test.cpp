#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>
#include "gpu/guest_completion_timeline.h"

using bf::gpu::GuestCompletionTimeline;
using bf::gpu::RetireResult;
#define REQUIRE(expr) do { if (!(expr)) { std::fprintf(stderr, "Failure at line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main() {
    GuestCompletionTimeline timeline;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> writes;
    auto accept = [&](std::uint32_t address, std::uint32_t value) {
        writes.emplace_back(address, value);
        return true;
    };
    std::size_t retired = 999;
    REQUIRE(!timeline.register_submit(0));
    REQUIRE(!timeline.enqueue_signal(1, 0x80001000u, 1));
    REQUIRE(timeline.register_submit(1));
    REQUIRE(!timeline.register_submit(1)); // no duplicate submission
    REQUIRE(!timeline.enqueue_signal(2, 0x80001000u, 1)); // not submitted
    REQUIRE(!timeline.enqueue_signal(1, 0x80001001u, 1)); // alignment
    REQUIRE(timeline.enqueue_signal(1, 0x80001000u, 101));
    REQUIRE(timeline.enqueue_signal(1, 0x80002000u, 202));
    REQUIRE(timeline.retire(0, accept, &retired) == RetireResult::ok);
    REQUIRE(retired == 0 && writes.empty());
    REQUIRE(timeline.register_submit(2));
    REQUIRE(timeline.enqueue_signal(2, 0x80001000u, 303));
    REQUIRE(timeline.pending() == 3);
    REQUIRE(timeline.retire(1, accept, &retired) == RetireResult::ok);
    REQUIRE(retired == 2 && writes.size() == 2);
    REQUIRE(writes[0] == std::make_pair(0x80001000u, 101u));
    REQUIRE(writes[1] == std::make_pair(0x80002000u, 202u));
    REQUIRE(!timeline.enqueue_signal(1, 0x80004000u, 0)); // no out of order
    REQUIRE(timeline.retire(0, accept, &retired) == RetireResult::invalid_fence);
    REQUIRE(writes.size() == 2);
    REQUIRE(timeline.retire(3, accept, &retired) == RetireResult::invalid_fence);
    REQUIRE(writes.size() == 2);
    // Failing the guest-memory write must not lose or prematurely acknowledge it.
    REQUIRE(timeline.retire(2, [](auto, auto) { return false; }, &retired) ==
            RetireResult::guest_write_failed);
    REQUIRE(retired == 0 && timeline.pending() == 1 && writes.size() == 2);
    REQUIRE(timeline.retire(2, accept, &retired) == RetireResult::ok);
    REQUIRE(retired == 1 && timeline.pending() == 0 && writes.back().second == 303);
    REQUIRE(!timeline.register_submit(std::numeric_limits<std::uint64_t>::max()));

    GuestCompletionTimeline lost;
    REQUIRE(lost.register_submit(5));
    REQUIRE(lost.enqueue_signal(5, 0x80003000u, 404));
    REQUIRE(lost.retire(std::numeric_limits<std::uint64_t>::max(), accept, &retired)
            == RetireResult::device_lost);
    REQUIRE(lost.device_lost() && lost.pending() == 1);
    REQUIRE(!lost.register_submit(6));
    REQUIRE(!lost.enqueue_signal(5, 0x80003000u, 405));
    REQUIRE(writes.size() == 3); // no false guest completion after device loss
    std::puts("Backend-neutral guest GPU completion tests passed");
    return 0;
}
