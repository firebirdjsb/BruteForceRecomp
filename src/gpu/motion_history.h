#pragma once
#include <cstdint>
#include <memory>
#include <d3d11.h>
#include <wrl/client.h>

namespace bf::gpu {
enum class MotionIdentityKind : uint32_t { actor,static_pushbuffer };
/* Every field comes from an authenticated original submission. Program and
 * topology only distinguish variants of that object/part; they never replace
 * object lifetime, model-node or original mesh-part identity. */
struct MotionIdentity {
    uint32_t object{},generation{},live_slot{},view{},context{},node{},part{},subdraw{};
    uint64_t program{},topology{};
    // Static compiled geometry has an observed scene-object generation and
    // exact original BEGIN command VA in part/subdraw, with no actor live slot.
    MotionIdentityKind kind=MotionIdentityKind::actor;
    bool operator==(const MotionIdentity &) const noexcept = default;
};
enum class MotionReset : uint32_t {
    none,first_frame,frame_gap,slot_reused,domain_changed,duplicate_submission,
    budget_exhausted,invalid_identity,invalid_domain,resource_failure,frame_not_open
};
/* Byte layout shared with HLSL StructuredBuffer (32 bytes). Clip coordinates
 * must be the ACTUAL post-VSH, unjittered float4. No matrix/bone reconstruction
 * or CPU-transformed approximation is performed by this component. */
struct MotionPosition {
    float clip[4];
    uint32_t original_id,frame,valid,padding;
};
static_assert(sizeof(MotionPosition)==32);
struct MotionPrevious {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> positions;
    uint32_t count{},expected_frame{};
    bool frame_valid{};
    MotionReset reason=MotionReset::first_frame;
};
struct MotionHistoryStats {uint64_t retained_bytes{},entries{},evictions{},invalidations{};};

/* Single scene-producer thread, one D3D11 immediate context. complete_frame
 * publishes source-frame submission completion, not GPU fence completion:
 * commands stay ordered on that context and this class never waits/readbacks.
 * Returned SRVs retain their resource while a caller binds/uses them. The
 * 128 MiB cap applies to buffers retained by this map; D3D11 may defer physical
 * reclamation while earlier GPU commands or caller-held SRVs still reference
 * an evicted buffer. Release a MotionPrevious after submitting its draw. */
class MotionHistory final {
public:
    static constexpr uint64_t max_budget=128ull<<20;
    static constexpr size_t max_entries=4096;
    MotionHistory(ID3D11Device *,ID3D11DeviceContext *,uint64_t budget=max_budget) noexcept;
    ~MotionHistory();
    MotionHistory(const MotionHistory &)=delete;
    MotionHistory &operator=(const MotionHistory &)=delete;
    bool ready() const noexcept;
    bool begin_frame(uint32_t frame) noexcept;
    MotionPrevious prepare(const MotionIdentity &,const uint32_t *original_ids,uint32_t count) noexcept;
    /* Source is a typed R32_UINT SRV of packed SO records: float4 clip then
     * uint original_id, stride 20 bytes. Caller must unbind SO before capture.
     * This is a GPU scatter; no source positions are read on the CPU. A second
     * capture of the same semantic key in one frame is ambiguous and rejected. */
    bool capture(const MotionIdentity &,ID3D11ShaderResourceView *source,uint32_t records) noexcept;
    bool complete_frame(uint32_t frame) noexcept;
    void reset() noexcept;
    MotionReset last_reason() const noexcept;
    MotionHistoryStats stats() const noexcept;
    static const char *reason_name(MotionReset) noexcept;
    /* Register-independent SM5 helper for later VS integration. Every lookup
     * checks the exact original ID, previous frame stamp, finite clip and valid
     * bit. A false return requires invalid-history/reactivity treatment. */
    static const char *lookup_hlsl() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
