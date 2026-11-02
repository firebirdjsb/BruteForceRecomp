# Tzeentchnet/BruteForce reuse assessment

Reviewed source: [Tzeentchnet/BruteForce](https://github.com/Tzeentchnet/BruteForce)
at `24d2e58687c976390efa23ab8b83a6b80078f1ec` (2026-10-31).
Our reference base: `cc5d1ea92bc5381abe959966ddde76b765db713d` (2026-11-01).

## Critical input boundary

Both repos pin `sp00nznet/xboxrecomp` to baseline `f0f8a461`, but their
North American `default.xbe` SHA-256 values are different:

- **Our supported executable**: `b6ea5d4dd6500ae86c3f5e3e4618bc9b1e3facddcb483170dfee7a787087c721`.
- **Their supported executable**: `25b1389b9593f280f1ab0382a776a050977ccb1e4b90611c59e2938286292e16`.

Title ID and entry-point address match, but that is not enough to make every
guest address, byte sequence, hook or function body interchangeable. Retain
our checksum gates, private generated output, proven callback, live semaphore
target, kernel import guard, boot tracing, native media, launch UI, POSIX
device model and Android ARM64 compile audit. Do not copy an upstream
`recomp_manual.c` hook or its seed addresses without our original-XBE check.

## Adopted on this **review branch**

- A small, framework-generic CPU texture fast path from upstream patch
  `0013-optimize-texture-sampling.patch`: wrap/mirror/clamp in-range early
  return and division-to-shift for 8/16-byte DXT block cache hashing.
- Synthetic test comparing the helper with the pinned framework's original
  scalar expression on edge cases, randomized coordinate samples and one
  million DXT-slot samples. Isolated host C11 build and UBSan run passed.
- Apply only inside our hash-verified **private build copy** of
  `nv2a_pb_exec.c`, after the existing GPU semaphore fix. The original
  `xboxrecomp` checkout remains clean. The compiler/test changes are
  independent of the XBE revision.
- No claim of native runtime build, game visual fidelity, or FPS improvement
  until our pinned-toolkit build and matched-scene checks run.

## Additional implemented changes on this review branch

- Adapted the primitive-draw part of upstream `0019-cache-hot-getenv.patch`
  into the existing pinned private build-copy transform. Our shared
  executor already caches the per-method environment check; the cached
  draw-path read is the only relevant uncached anchor in the pinned base.
  The upstream `nv2a_pb_scan.c` addition is not portable as-is because
  our pinned base has a different scanner implementation. No benchmark
  claim is made yet.
- New disc imports omit the independently identified ancillary demo
  payload trees `fx/` (Tao Feng) and `Mech/` (MechAssault) and the root
  `demos.xbe`. They continue to import Brute Force's `data/`, `music/`,
  `media/`, and other unclassified paths. This decision is based on
  upstream `docs/ARCHITECTURE.md` and does **not** modify the game
  executable, resources already imported in old libraries, or the
  original ISO. Reimport locally to obtain a demo-excluded library.
  A runtime boot/replay of a filtered disc has not yet been confirmed.
- Added `.github/workflows/recomp-source-checks.yml` for safe,
  proprietary-free Python, path policy and C11/UBSan test checks.

## Windows framebuffer publication correction

The upstream `0005-synchronize-presented-frames.patch` identifies a
producer/consumer race: two presentation buffers alone do not prevent the
producer from filling the same buffer that the Windows window thread is
reading. Our hash-pinned private build-copy adaptation adds one Win32 SRW
lock around the producer's copy/publication and the presenter's snapshot
read. It also unlocks correctly on allocation failure. The existing
no-flip/pinned framebuffer fallback is retained verbatim. Linux uses
`fb_present_sdl.c` and is deliberately not modified by this patch.

All five replacement spans were checked against the exact pinned
`xboxrecomp` base. **Windows compilation, startup and matched-frame
validation remain necessary**; the CMake smoke workflow uses synthetic
generated files and contains no proprietary XBE or game asset content.

## CPU/GPU frame comparison (implemented tool)

`tools/compare_frames.py` and its synthetic pytest fixtures were imported
from upstream, with MIT attribution. The tool compares two **already-captured,
same-resolution** PNG/BMP frames and reports exact RGB differences,
per-channel tolerance counts, regional statistics and an optional difference
mask. It never generates frames, launches game code, or claims to establish
matching simulation states.

Install optional, non-runtime analysis dependencies with
`python -m pip install -r tools/analysis-requirements.txt`. Example:

```sh
python tools/compare_frames.py reference.png candidate.png --reference-label cpu --candidate-label d3d11 --tolerance 8 --diff mismatch.png
```

Only compare captures whose scene, viewpoint, input, HUD phase and flip
identity were independently verified to align. This supplies a validation
gate for the next D3D11 renderer work without copying title-specific hooks.

## DX12 compatibility foundation

We are retaining the shared GPU correctness rules for future DX11 and DX12
renderers, not copying DX11-specific handles or synchronization into DX12.
`src/gpu/guest_completion_timeline.h` is a C++17, backend-independent,
monotonic fence/guest-write model. `src/gpu/d3d12_fence_bridge.h` adapts
real D3D12 command-queue fence completion. Optional D3D12 WARP and
D3D11On12 interoperability tests run separately from the current game.
No change to the original live Xbox semaphore integration was required.

See [DX12 compatibility and phased bring-up](DX12_COMPATIBILITY.md) for
the precise boundary and unmet full-renderer, resource-transition and
frame-matching gates. DX11 can optionally be driven through D3D11On12,
but using that bridge is not the same as implementing a native DX12
renderer. The CPU texture fast path does not automatically accelerate
DX12's hardware texture samplers.

## Follow-up candidates, ordered by risk and payoff

| Priority | Upstream files / patches | Reuse path | Conditions |
| --- | --- | --- | --- |
| 1 | `0019-cache-hot-getenv.patch`, `0014-frame-statistics.patch`, tests and `tools/bench.py` | Import small profiling/telemetry pieces first, not all title-specific scripted inputs | Check runtime drift; preserve our boot tracer, watchdog and deterministic scratch saves; count distinct **world** frames separately from flips |
| 2 | `0005-synchronize-presented-frames.patch`, `0026`–`0027` frame/presenter captures | Port presentation locks/capture identifiers by backend | The reference implementation uses Windows SRW locks; design Linux/Android equivalents and compare synchronized frames before enabling |
| 3 | `0016-extended-render-backend.patch`, `0017-gpu-vertex-programs.patch`, `src/gpu/d3d11_backend.c`, `rc_hlsl.c`, `vsh_hlsl.c`, `texture_decode.c`, `shader_cache.c` | Stage a Windows D3D11 backend **behind a CPU fallback** while retaining our GPU semaphore plumbing | Needs full backend interface, vertex, surface, stencil, render-to-texture, texture invalidation, and CPU/GPU frame comparisons. Not a standalone source-file copy |
| 4 | `0024-gpu-shared-presentation.patch`, `0028-scaled-gpu-presentation.patch`, `src/gpu/render_scale.*` | Add GPU-resident presentation and render scaling **after** D3D11 correctness | Keep guest logical image size and guest readback semantics; cover Bink/movie CPU writes, UI and HUD, both original and widescreen aspect modes |
| 5 | `0006-harden-usb-lifecycle.patch` and related interrupt tests | Compare specific USB cleanup and TIB lifetime fixes against our USB/POSIX changes | Existing device-fault, guest IF/IRQL, event, and input fixes must not regress |
| 6 | `0030`–`0035` DSP/audio patches, `src/audio/recomp_apu_xdsp.c`, `src/xemu_dsp` | Evaluate simple stereo and HRTF fixes before optional accurate DSP | Full xemu interpreter adds GPL-2.0-or-later linked-binary obligations; some integration files are LGPL; audio listening and synchronization validation remain necessary |

**Platform note:** D3D11 is Windows-specific. It cannot replace our Linux
SDL2 path or the planned Android Vulkan/SDL2 integration. The upstream
renderer is also experimental: its own notes document movement FPS
regressions, pixel mismatches and unmatched real-scene measurements.
A working tutorial image is not a verified playable release.

**Demo-only disc payloads:** upstream `docs/ARCHITECTURE.md` identifies
`fx/` (Tao Feng demo), `Mech/` (MechAssault demo), and `demos.xbe`.
Our importer currently extracts the whole disc for compatibility. Excluding
those items from future *runtime imports* should be a separate, tested change
after confirming no core-game dependency. Never import another game's code
into the recompiled Brute Force executable; leave original game menus intact
until their logic has been authenticated. Do not remove the original disc
itself or any user's imported copies implicitly.

## Required merge gates

1. Run `cmake --build`, `ctest`, and Python tests with our pinned clean
   toolkit on Windows; reproduce the existing Debug Start/Campaign menu
   trace and semaphore progression. Check a Release build separately.
2. Repeat import, framebuffer, input, and USB regression checks on Linux;
   the current Linux asynchronous read wait must not be bypassed by
   fabricating completion.
3. Compare matched original-size frames from the CPU reference and any GPU
   path, including HUD, movies, depth/stencil, and readback. A/B/A benchmark
   changes on identical scenes and inputs rather than equating flips with
   rendered world frames.
4. Preserve the existing host patch markers, original-XBE SHA-256 checks,
   generated-code provenance, decomp/recomp separation, and private saves.
   Keep Android compile-only claims distinct from actual APK and device tests.
5. Track third-party license notices for every adapted source. Do not vendor
   original game assets, executable bytes or generated guest code.

Upstream sources are engineering references, not evidence that their
different supported XBE is semantically interchangeable with ours.
