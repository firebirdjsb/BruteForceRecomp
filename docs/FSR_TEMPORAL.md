# AMD FSR temporal integration checkpoint — 4 November 2026

The optional DX12 SDK adapter and hardware conformance tests are implemented.
**They are not connected to gameplay yet.** No new launcher/F1 toggle is exposed,
and the installed player, preferences, retail data and permanent saves are unchanged.
Native 16:9 and NVIDIA DLSS are still unfinished.

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

## Remaining game integration

The current DX12 presenter receives completed CPU color, including interface
content. Its custom image-motion interpolation is not AMD FSR Frame Generation
and is not a valid substitute for temporal upscaler motion inputs.

The scene renderer uses D3D11 or D3D11On12 and reads completed color back to guest
memory. Its NV2A vertex interfaces expose current shader programs/constants and
current vertices, but do not identify persistent draw instances, retain previous
object/skinned-vertex transforms or emit an engine motion target. Exporting only
camera motion would leave actors, animation, projectiles and effects incorrect.

Before activating the new adapter in the player:

1. Establish stable per-instance/per-vertex correspondence and previous-frame
   transforms for actual game draws. Validate camera, rigid, skinned, particle,
   transparent, disocclusion and reset behavior against captured scene data.
2. Export coherent scene color, normalized depth and unjittered previous-minus-
   current pixel motion from the same source frame. Apply projection jitter to
   the scene, without altering original simulation or UI geometry. Validate depth
   aliases, effects and texture filtering/mip bias at the real render resolution.
3. Resolve the D3D11On12 scene/presenter device and queue ownership boundary.
   Transfer real GPU resources with fences and correct lifetimes, rather than
   supplying a completed CPU image plus fabricated temporal attachments.
4. Run SR before native HUD/text/F1 composition. Present generated frames using
   AMD's swapchain/pacing integration or a separately verified presentation path.
   Handle resizing, device loss, menus, movies, pause, focus changes and scene cuts.
5. Compare matched in-game positions at fixed output resolution and original
   timing, counting rendered, generated and displayed frames independently.
   Only then expose persistent controls and promote a candidate player.

FSR 4.1 is an ML temporal upscaler, not a blanket upgrade of the game's shaders.
Existing low source FPS may be CPU/runtime limited; adding an upscaler does not
establish an improvement. Do not advertise a 30/60-FPS gameplay result from these
SDK conformance runs.

Native widescreen needs a separate coherent camera/frustum/render-target/HUD
implementation. This task inspected the authenticated projection functions
`0x0006AD00`/`0x0006AD90`, frustum construction `0x0006C840`, FOV/aspect setters
`0x0006E450`/`0x0006E4A0`, script resets `0x00172CA0`/`0x00172CF0` and initial
640x480 setup `0x00068340`. No speculative patch was installed. Adjusting only
an aspect constant or stretching presentation would not satisfy native 16:9.

References: [AMD SDK](https://gpuopen.com/amd-fsr-sdk/),
[FSR 4.1.2 integration](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-ml/),
[frame-generation API](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-api/).
