# Upstream patch audit

Reference: `Tzeentchnet/BruteForce` at `24d2e58687c976390efa23ab8b83a6b80078f1ec`, checked 2026-11-02. This is an accounting of every numbered upstream patch, not a claim that every patch is enabled. Our supported XBE and title hooks remain independent.

| Patch | State in main | Integration boundary |
| --- | --- | --- |
| `0001-bound-extra-thunks.patch` | Active equivalent | Import scan guard; cmake/runtime_fixes.cmake |
| `0002-model-wbinvd.patch` | Active equivalent | Authenticated WBINVD service; src/instruction_services.cpp |
| `0003-synchronize-semaphore-target.patch` | Active equivalent | Dynamic semaphore target and original software commands; src/gpu_completion.cpp |
| `0004-gate-read-tracing.patch` | Active equivalent | Opt-in successful-read tracing; cmake/runtime_fixes.cmake |
| `0005-synchronize-presented-frames.patch` | Active equivalent | Single synchronized native framebuffer transform; cmake/frame_handoff.cmake |
| `0006-harden-usb-lifecycle.patch` | Gated | Retain working USB/IRQ paths pending real-device and cross-platform shutdown QA |
| `0007-account-guest-memory.patch` | Active equivalent | Guest RAM accounting and measured stack budget; cmake/memory_statistics.cmake |
| `0008-experimental-pgraph-and-staged-walker.patch` | Independent repair active | DMA traversal, original command fixups and CALL state; no wholesale title-hook transplant |
| `0009-experimental-window-clipping.patch` | Not enabled | Experimental clipping requires matched-frame validation |
| `0010-experimental-face-culling.patch` | Not enabled | Experimental culling requires matched-frame validation |
| `0011-experimental-program-near-plane.patch` | Not enabled | Experimental near-plane policy requires original-XBE evidence |
| `0012-vectorize-color-unpack.patch` | Not imported | Separate local blend SIMD is tested; this color-unpack patch is not applied |
| `0013-optimize-texture-sampling.patch` | Active adaptation | Exact texture wrap and DXT slot math |
| `0014-frame-statistics.patch` | Active adaptation | Opt-in real presented-frame hashing and CSV telemetry |
| `0015-gpu-census-and-ceiling-probe.patch` | Not imported | Local render timing exists; upstream ceiling probes are not game acceleration |
| `0016-extended-render-backend.patch` | Staged | Extended batch contract and register-combiner HLSL; no hardware draw registration |
| `0017-gpu-vertex-programs.patch` | Staged | Vertex HLSL generator; GPU vertex dispatch is not connected |
| `0018-async-visibility-reports.patch` | Not imported | Async visibility completion needs a real GPU backend |
| `0019-cache-hot-getenv.patch` | Active adaptation | Cache the remaining primitive-draw environment lookup |
| `0020-texture-metadata.patch` | Partial staging | Bounds-checked P8 decoder; full GPU texture metadata path is not connected |
| `0021-gpu-window-clip.patch` | Not imported | GPU window clipping depends on backend integration |
| `0022-gpu-depth-clear-offload.patch` | Not imported | Depth offload cannot activate before a backend owns real clears |
| `0023-flip-only-frame-captures.patch` | Independent capture active | Private captures occur on presented flips; upstream patch is not applied |
| `0024-gpu-shared-presentation.patch` | Not imported | GPU-owned shared surfaces require actual scene draws and guest readback |
| `0025-gpu-audit.patch` | Not imported | Upstream GPU request audit depends on extended renderer |
| `0026-flip-id-frame-captures.patch` | Not imported | Explicit upstream capture IDs remain a follow-up |
| `0027-presenter-source-captures.patch` | Not imported | Upstream presenter-source trace remains a follow-up |
| `0028-scaled-gpu-presentation.patch` | Partial independent implementation | Native DX12/FSR presentation and tested sizing policy; higher-resolution scene rendering remains open |
| `0029-hud-safe-near-plane.patch` | Not imported | HUD-specific near-plane changes require validated original title behavior |
| `0030-accurate-apu-dsp-hooks.patch` | Not imported | Accurate DSP interpreter and integration are not part of this merge |
| `0031-external-apu-dsp-lifecycle.patch` | Not imported | External DSP lifecycle depends on the absent DSP integration |
| `0032-run-hooks-before-firmware-exit.patch` | Not imported | Firmware-exit hook needs lifecycle regression coverage |
| `0033-prioritize-apu-frame-worker.patch` | Not imported | APU worker priority change needs controlled audio scheduling tests |
| `0034-raise-apu-queue-low-water.patch` | Not imported | Queue-low-water/latency changes need controlled underrun and listening tests |
| `0035-preserve-mono-hrtf-stereo.patch` | Active adaptation | Mono HRTF source/output channel distinction; compiled regression passes |

The complete source feature boundary is in [the integration matrix](UPSTREAM_INTEGRATION_PROGRESS.md). Source-only CI cannot establish campaign completion, physical controller behavior, audio quality or native Android support.
