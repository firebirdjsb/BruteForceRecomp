# FSR, scene export and widescreen checkpoint — 4 November 2026

**Work stopped at the user's request.** Do not resume implementation without a
new instruction. This is a partial implementation checkpoint, not a completed
DLSS/FSR/widescreen release. No candidate was installed or merged into main.

## Repository and boundaries

Actual checkout: `C:/Users/kolby/Documents/ChatGPT/BFRecomp`, branch
`codex/texture-profile-followup`. AMD SDK checkpoint `6773892` is already pushed.
This follow-up records the renderer export and native D3D12 handoff.
Unrelated texture-profiling changes in the checkout are deliberately excluded
from these commits, including their STATUS/performance/handoff notes and fixture.
Private original game translations, SDK binaries, captures and DLC ZIP are not
committed. Original game assets, permanent saves, installed player and pinned
runtime sources were not changed. Gameplay runs used scratch copies of saves.

## Implemented

- Optional official AMD SDK adapter: real SR, jitter queries, provider selection,
  FG configure/prepare/generate, input validation, source IDs and history resets.
  Default builds have no AMD SDK dependency. See [FSR details](FSR_TEMPORAL.md).
- Scene renderer exports its actual color and depth attachments immediately
  before the first native UI boundary for a color surface/source generation.
  It tracks the depth target actually written by that color surface in the same
  generation. Active occlusion queries prevent capture; mismatched dimensions
  are rejected. Later UI draws and duplicate boundaries cannot replace a snapshot.
- `scene_snapshot.h/.cpp`: render-thread consumer, GPU color snapshot and actual
  D24/D32 device-depth conversion to R32_FLOAT, with compute state restoration.
  It does not assert that device depth matches recovered game camera parameters.
- `scene_device.h/.cpp`: D3D11On12 resource checkout, actual scene device/queue,
  native D3D12 recording callback, COMMON-state contract, fence-backed resource
  return and GPU completion. Native access does not copy images through the CPU.
  This synchronous prototype waits for completion and is not a performance claim.
- Optional bounded diagnostic capture, disabled by default. No temporal option
  was added to the launcher/F1 and no fabricated motion vectors were introduced.

## Verified on this RX 7600

Driver `32.0.32015.7029`, Shader Model 6.6. Official AMD SDK v2.4.0 commit
`7539ae8b6568ad29ed78a753404351daf3b5cca7`; loader/upscaler/FG DLL signatures valid.

Five AMD hardware tests passed, zero skips: `fsr3_hardware`, `fsr4_hardware`,
`framegen_hardware`, `fsr4-framegen_hardware`, `fsr4-bgra_hardware` (5.62 s).
Selected SR providers were 3.1.6 and 4.1.2; supported FG was 3.1.6. The BGRA
case uses the actual game color format. These use a synthetic analytic scene,
exact object motion/depth and applied jitter; they do not exercise the game.
FG tests check RGB changes and a foreground position between source endpoints.
Existing FSR 1 graphics and custom-interpolation smoke checks passed earlier.
Windows' optional GPU debug layer was unavailable; no debug-clean claim is made.

Seven final renderer tests passed, zero skips (10.02 s):
`scene_snapshot_d3d11`, `scene_snapshot_d3d12`, `nv2a_gpu_scene_parity`,
`nv2a_d3d12_scene_parity`, `scene_aa_ui_preservation`,
`ordered_scene_snapshot_d3d11`, `ordered_scene_snapshot_d3d12`.
Direct tests verify red/blue color, .25/.75 device depth, compute state, later
source overwrite and rejection. The D3D12 test reads matching native color/depth
through the fenced handoff, then successfully reads them again through D3D11.
Executor tests submit real NV2A fixture commands and confirm green UI is absent
from the exported red/blue scene while present in the completed frame.

Local evidence root: `C:/Users/kolby/Documents/Codex/2026-11-04/task`:

- `fsr-five-hardware-detail.log`, `fsr-bgra-hardware-tests.log`.
- `scene-seven-final-detail.log`, `scene-seven-final-tests.log`.
- `fsr-test-build/Release`, `fsr-sdk`; dependency-off `fsr-default-check`.
- Public runtime test build: checkout `.local/public-runtime-smoke`.
- Private player: checkout `.local/scene-export-player/Release/bruteforce_recomp.exe`.
- `widescreen-probe/original-640-control/result.json`: existing player reached
  campaign at 90.74 s; captured 640x480 image. Scripted, not manual gameplay QA.
- `widescreen-probe/campaign-scene-export-original`: final export candidate
  rendered 640x480, but the window closed at 15.36 s before campaign navigation
  or the capture trigger. **No real campaign color/depth capture was verified.**
- The first export-candidate attempt accidentally reused stale 720p objects:
  restoring source with copy2 retained older timestamps. Sources were touched,
  rebuilt and the corrected run above was 640x480. Do not treat that first
  run as an original-resolution control failure.

## Blockers and unimplemented features

Native 16:9, gameplay temporal SR/FG and DLSS remain unfinished. SDK tests are
not evidence of gameplay integration or a gameplay frame-rate improvement.

A private, unpromoted 1280x720 allocation/viewport probe failed before its first
frame: original CreateDevice returned `80004005`, followed by
HalReturnToFirmware routine 2. A private corrected AV-capability profile still
failed with the same HRESULT. Neither probe modified canonical generated files.
Logs: `widescreen-probe/native-720p` and `native-720p-av-profile`.
The original device path is `0x0006A750 -> 0x0036EC60 -> 0x003764C0`.
Further diagnosis must identify the failing original device/mode branch instead
of bypassing failure or assuming the presenter can stretch a 4:3 image.

Authenticated camera work inspected `0x0006AD00/0x0006AD90` projection updates,
`0x0006C840` frustum construction, `0x0006E450/0x0006E4A0` FOV/aspect setters,
`0x00172CA0/0x00172CF0` script resets and `0x00068340` initial framebuffer setup.
Projection .14/.18 fields are half angles; .24/.28 are their tangents; .2c is
aspect. Initial values are 1/.75/4:3. These observations were not promoted into
speculative camera/HUD patches. Native camera, frustum, targets, depth aliases,
HUD and authored effects must agree before claiming widescreen.

Temporal work still needs validated projection/depth mapping, stable instance
and skinned-vertex history, previous/current object transforms, real motion
(including actors/particles), scene projection jitter, reset identification,
transparent/reactive handling, HUD composition and actual presentation/pacing.
The scene handoff is available but not connected to FSR in the player.
The RX 7600 cannot validate NVIDIA DLSS; no DLSS implementation is claimed.

## Reproduction and next steps when authorized

Use the existing MSVC CMake binaries under Visual Studio 2022 BuildTools
`Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin`; Python is checkout
`.local/build-venv/Scripts/python.exe`. Original simulation timing remains 30 Hz.

1. Re-run the documented SDK build/CTest commands in FSR_TEMPORAL.md.
2. Build `bf_gpu_scene_test` and `bf_scene_snapshot_test` in the existing public
   runtime cache; run CTest with `-R "scene_snapshot|nv2a_.*scene_parity|scene_aa_ui_preservation"`.
3. Validate real campaign exports with the private candidate and scratch saves.
   Set `BF_SCENE_CAPTURE_DIR`, `BF_SCENE_CAPTURE_COUNT=4` (maximum 8),
   `BF_SCENE_CAPTURE_MIN_DEPTH_DRAWS=64`, optional `BF_SCENE_CAPTURE_START_FRAME`
   and `BF_SCENE_CAPTURE_TRIGGER` path. Create the trigger only after reaching
   gameplay. Outputs are color PPM, little-endian float32 depth and JSON metadata.
   Metadata explicitly says projection unverified, no motion/jitter, not SDK-ready.
4. Recover and validate scene/camera/instance history before wiring the adapter.
   Keep menu/movie/pause/resizing/device loss/HUD transitions in the validation.
5. Diagnose widescreen mode creation separately; then validate matched camera
   positions, additional horizontal world visibility, correct geometry and HUD.

No implementation is resumed by this handoff. No requests were denied or left
pending by automatic approval review. The remaining issues are technical and QA
blockers. Revert these source commits and rebuild to roll back; preserve saves.
