# AMD FSR temporal integration checkpoint — 4 November 2026

**Gameplay temporal FSR remains unavailable.** The engineering branch now has
real AMD FSR 3.1.6/4.1.2 processing, shared-device GPU presentation, separate
HUD/host layers, SDK-jitter control, and request/settings plumbing. These are
not evidence of a completed Brute Force temporal source. No candidate from this
work has replaced the installed player or launcher, and main has not received
this unfinished integration. Native 16:9/HUD work was separately accepted by
the user; DLSS and gameplay temporal frame generation remain unfinished.

## Native integration continuation

The SDK-enabled processor executes both explicitly selected providers on the
RX 7600 through the actual D3D11On12/native-D3D12 bridge. Its GPU fixture checks
real rendered synthetic color/depth/motion, issued and applied jitter,
source/provenance rejection, three retained output leases, immutable HUD copies,
cross-thread source tickets, scene resets and presentation after processor
shutdown. SDK-absent builds reject configuration without constructing substitute
inputs. The F1/launcher additions represent requests; actual provider/status is
reported separately and is never inferred from a selected menu value.

The scene and presenter share one device/native queue. Translation uses a
different queue with its own fence: reusing the translation queue for native
unwrapping deadlocked the Windows 2022 D3D11On12 implementation. The corrected
two-queue bridge passed hosted CI and local depth/return-ownership tests; no
completion wait was removed or replaced with a fabricated signal.

Native HUD capture uses a separate premultiplied FP16 target and preserves
RT0/depth/stencil/alpha/scissor behavior. Host F1/keycaps use a transparent
BGRA layer after reconstruction. Synchronizing the original surface before
starting HUD capture fixed an observed false source-change rejection. Genuine
same-target texture feedback still rejects independent HUD composition.

Bounded real-title captures now carry ordered view/object/part records and
original vertex IDs. A separate GPU history component retains actual post-VSH
unjittered positions rather than reconstructing skinning on the CPU. The
default-off MRT diagnostics produce motion/reactivity/canonical-depth outputs
only for independently supported submissions; unknown opaque geometry keeps
the whole source incomplete.

The projection replay executed 85 captured title batches (32,871 indexed
vertices) through the unchanged interpreter and generated HLSL on hardware.
Validity agreed, clip W was bit-identical, and normalized depth differed by at
most 1.2e-7. Ordinary primary shaders implement `D=A+B/W` with near 0.1/far 250.
The background uses a different large-far projection; 512x512 secondary passes
are orthographic. Those exceptions must not inherit the primary depth mapping.
Seven captured generations also matched held original world depth and later
pre-HUD color by frame, color/depth VA, view and context. This does not validate
complete motion coverage or enable the SDK on a title frame.

### Blocking ownership result

The final stock-shader capture, `motion-d`, observed eight main color-writing
actor parts per frame. Another 302-388 main world draws per frame lacked proven
ownership, including 114-172 opaque draws and 222-297 depth writers. Constructor
observation and original FIFO command addresses work, but do not establish the
remaining geometry lifetime/correspondence.

The attempted static adapter incorrectly interpreted retail `scene+0x2C` as a
pushbuffer byte size. Actual original code feeds that changing word to a
synchronization routine. That interpretation has been withdrawn: retail static
adapters always reject temporal identity, even when the raw word happens to be
aligned. Observed constructor generation, raw resource words and command VAs
remain diagnostics only. Generic synthetic static-history tests do not prove
this retail resource layout.

Two ownership repair attempts were exhausted under the workspace's bounded
engineering rule. No third offset/layout repair was attempted. A new authorized
cycle must first establish the actual retail resource allocation/header bounds
and owners of draws outside the observed static scope. Do not reinterpret the
synchronization word, use draw order/VB hashes as lifetime identity, or enable
gameplay FSR with this coverage gap.

Local evidence is retained under `.local/temporal-fsr/` (`motion-b` through
`motion-d`, `projection-proof`) and
`.local/temporal-motion-check/ownership-final-diagnosis.json`. These include
private title inputs and are not distributable source. Scripted title runs used
copied saves and owned timeout termination; they are not user acceptance or a
normal-close smoke test. The installed EXE remains SHA256
`9b2c79bdd8e0f28140b1caa274913dff1a6b528a641cf3ffdedefd0cc7f2563e`.

## Verified on this computer

AMD Radeon RX 7600 (PCI vendor 1002, device 7480), Windows driver
32.0.32015.7029, Shader Model 6.6. The official AMD FSR SDK v2.4.0,
commit `7539ae8b6568ad29ed78a753404351daf3b5cca7`, supplied the loader,
upscaler and frame-generation DLLs. All three Authenticode signatures were valid.

| Hardware test | Selected upscaler | Selected frame generation | Result |
| --- | --- | --- | --- |
| `fsr3_hardware` | 3.1.6 | Off | Pass |
| `fsr4_hardware` | 4.1.2 | Off | Pass |
| `framegen_hardware` | 3.1.6 | 3.1.6 | Pass |
| `fsr4-framegen_hardware` | 4.1.2 | 3.1.6 | Pass |
| `fsr4-bgra_hardware` | 4.1.2, BGRA input | Off | Pass |

The SDK reported only FSR 3.1.6 frame generation on this RX 7600. The test does
not relabel that as ML frame generation. FSR 4.1.2 upscaling actually executed;
it was explicitly selected from the supported provider IDs. Automatic provider
selection is also supported by the adapter, with the selected name queryable.

Each test processes 24 synthetic scene frames at 320x180 -> 640x360. The scene
contains a static checker plane and an opaque translating foreground rectangle.
Depth comes from their known view-space distances; motion is the exact previous
minus current foreground displacement and zero for the static background.
Subpixel jitter is obtained from AMD's API and applied to scene sampling.
These are valid **synthetic scene inputs**, not invented inputs for Brute Force.

GPU readbacks check foreground placement. Frame-generation tests require its
centroid to fall near the midpoint, away from both endpoints, and RGB output to
differ from both source images (alpha differences alone cannot pass). For the
FSR 4.1.2 + FG test, frame 11 measured previous/generated/current centers of
175.50 / 177.52 / 179.79 output pixels, with 9,368 changed RGB pixels versus
the current source. Missing motion and repeated source IDs are rejected.
Scene-cut reset paths execute. This is not proof of artifact-free resets or
full image quality. No performance or gameplay-FPS claim follows from this test.

Existing `bf_graphics_smoke` and `bf_interpolation_smoke` also passed on the RX
7600: FSR 1 GPU output, color/alpha, aspect-preserving letterboxing, live content,
resize/downscale, fixed scales, motion interpolation, static HUD and resets.
The optional Windows graphics debug layer was unavailable on this computer;
readback and API success checks ran without it.

## Build and reproduce

Obtain the official [AMD FSR SDK v2.4.0](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v2.4.0).
Keep it outside public source. The option is empty by default; ordinary builds
retain their existing dependencies and behavior. Windows x64/DX12 is required
for this adapter. No Vulkan/Linux/Android integration is claimed.

```powershell
cmake -S . -B .local/fsr-checks -A x64 -DBF_GRAPHICS_ONLY=ON -DBF_FSR_SDK_DIR=C:/path/to/FidelityFX-SDK
cmake --build .local/fsr-checks --config Release --parallel 4
ctest --test-dir .local/fsr-checks -C Release --output-on-failure
.local/fsr-checks/Release/bf_graphics_smoke.exe
.local/fsr-checks/Release/bf_interpolation_smoke.exe
```

The smoke executable copies the three official DX12 DLLs and SDK license beside
it. It writes PPM readbacks in the current working directory. Unsupported
explicit versions return CTest skip code 77, rather than claiming success with
an unrelated fallback. On this machine all five tests passed without skips.
No AMD driver update or system setting change was performed.

Local evidence from this task:

- SDK: `C:/Users/kolby/Documents/Codex/2026-11-04/task/fsr-sdk`.
- Build: `C:/Users/kolby/Documents/Codex/2026-11-04/task/fsr-test-build`.
- Full output: `Testing/Temporary/LastTest.log` beneath that build.
- Summary: `fsr-hardware-ctest.log` and `hardware-result.json` beneath that build.
- Existing-path tests: `bf_graphics_smoke.log`, `bf_interpolation_smoke.log`.

## Adapter contract

`src/graphics/fsr_temporal.h/.cpp` wraps AMD's actual `ffxCreateContext`,
`ffxQuery`, `ffxConfigure` and `ffxDispatch` API. It implements upscaling,
frame-generation configure/prepare-V2/generate, version enumeration and explicit
selection, subpixel jitter, history resets, dimension/format/device/alias checks,
and queue completion before context destruction/reconfiguration.

The caller supplies scene color, normalized R32_FLOAT depth and RG floating-point
motion, plus camera parameters/basis and a monotonically increasing source ID.
The adapter supports optional reactive and composition masks; the synthetic
opaque fixture needs neither. Inputs use NON_PIXEL_SHADER_RESOURCE and outputs
use UAV state; AMD's API restores these states. Resources must remain alive
through command-list completion. Calls belong to the render thread; descriptor
heaps and pipelines must be rebound afterward. Reconfigure only after submitting
pending command lists. This adapter's standalone frame generation records GPU
work without a swapchain; game frame pacing/presentation still requires integration.

## Scene export follow-up

The renderer exports color/device depth and supports fenced native D3D12 access
to D3D11On12 snapshots. The later provenance guard rejects depth attachments
which were cleared, replaced or reused after their recorded world draws.

The 4 November local continuation identified the missing capture point:
another color target writes the shared depth attachment before the old UI
boundary. Optional `BF_DEPTH_PROBE_DIR` diagnostics now copy the actual color
and D24 attachments **before** that overwrite, on the owning render thread.
They own separate resources, limits and output; they neither invoke nor replace
the normal snapshot consumer. `BF_DEPTH_PROBE_COUNT` defaults to4 (maximum8),
`BF_DEPTH_PROBE_MIN_DRAWS` defaults to64, and optional
`BF_DEPTH_PROBE_TRIGGER` delays capture until a host file exists. Output labels
the exact stage and sets `before_native_ui=false`, projection unverified and
`temporal_sdk_ready=false`. No production upscaler consumes these diagnostics.

Actual local tutorial frame2625 produced214 recorded draws and240,741 finite
depth samples strictly between zero and one,66,459 clear-one pixels, no zero
pixels and162,063 distinct values. The range was approximately .971743..1.
Visual inspection confirms the accompanying color is the world/player without
native HUD. This is substantial scene depth, unlike the late export's306,081
zero pixels. Near/far reconstruction, orientation/projection correspondence and
per-instance motion still require validation; plausible distance calculations
are not source proof. Seven consecutive tutorial captures retained similar
distributions. Private evidence is
`.local/render-followup/integration-8b5e187/wide-view-a/depth-analysis.json` and
its `depth-probes` directory.

Focused D3D11/D3D12 tests cover pre-clear/reuse/recreation copies, their .75/.25
depth and blue/red color, resource/consumer independence, subsequent overwrite,
compute state, bounded budgets and trigger behavior. The original validated
export continues rejecting stale attachments. The actual 16:9 output path can
use existing spatial FSR1 EASU+RCAS; this does not enable temporal FSR3/4.
See [native aspect work](NATIVE_ASPECT_RATIO.md) for integrated camera/HUD QA and
the [earlier scene handoff](FSR_SCENE_HANDOFF.md) for original SDK evidence.

## Remaining game integration

The installed player's path receives completed CPU color, including interface
content. The engineering branch also has real GPU texture/ticket presentation,
but no complete title source is submitted to it. Custom image-motion
interpolation remains separate from AMD FSR Frame Generation and from the
title's motion vectors.

Before activating the new adapter in the player:

1. Begin a newly authorized, bounded ownership investigation. Prove the retail
   resource layout and the remaining render-pass owners from original code and
   allocation/submission observations. The withdrawn synchronization-word
   interpretation is not a usable span or an offset to adjust speculatively.
2. Verify complete primary-world motion and depth outputs against actual title
   frames, including deformation, expected new history, background projection,
   transparent coverage and disocclusion. Known transparent effects can use
   actual fragment reactive coverage under AMD's documented contract; missing
   opaque/world history must not be replaced by guessed vectors.
3. Connect a complete, coherent source and its matching native HUD to the
   tested processor and completed-frame ticket. Query and apply SDK jitter
   before geometry, latch it through the whole source, and keep original
   simulation/physics timing unchanged. Unknown or rejected jittered sources
   must not fall back to a shifted CPU image.
4. Validate live size/aspect/mode changes, menus, movies, pause, focus, scene cuts
   and GPU failure. The existing normal source path remains the fallback.
5. Compare real in-game output and timing, install a backed-up candidate, and
   obtain the user's smoke confirmation before any main push. Synthetic SDK
   tests and a mode request do not satisfy this gate. Temporal frame generation
   and DLSS are separate remaining integrations.

FSR 4.1 is an ML temporal upscaler, not a blanket upgrade of the game's shaders.
Existing low source FPS may be CPU/runtime limited; adding an upscaler does not
establish an improvement. Do not advertise a 30/60-FPS gameplay result from these
SDK conformance runs.

The current widescreen continuation uses authenticated original projection and
frustum helpers to widen the consumed view while preserving the original
640x480 guest allocations. Its presentation, FSR1 path and complete native HUD
grouping passed bounded actual-title tutorial QA and the user's smoke test.
Broader campaign/split-screen/cinematic acceptance remains pending. See
`NATIVE_ASPECT_RATIO.md` for the current installation and acceptance state.
This aspect transaction does not supply temporal motion or validate the depth
projection. The older separate camera/depth samples do not prove temporal
readiness; the newer paired captures and shader replay also leave whole-frame
ownership incomplete. Keep `temporal_sdk_ready=false` until the actual title
motion, projection, jitter and composition gates pass together.

References: [AMD SDK](https://gpuopen.com/amd-fsr-sdk/),
[FSR 4.1.2 integration](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-ml/),
[frame-generation API](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-api/).
