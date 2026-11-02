# Current state - 2 November 2026

This remains an experimental native recompilation. It is not a completed
Windows/Linux/Android release.

## Campaign rendering checkpoint

The tutorial now loads and renders Tex, terrain, vegetation and sky after the intros. The original GPU software methods were missing: operation 5 signals the D3D fence event; operations 3/4 apply the original reusable-pushbuffer fixup records and return jump; operation 10 updates the original byte accounting and continuation. The DMA walker now shares the actual CALL-state register with those operations. DMA walking is the default, with the CPU renderer retained. Original physics and simulation timing were not changed.

The 238-second `work/gameplay-priority/fixups-live-9a0natba/` run and 298-second visible `campaign-window-yf3rily3/` run reached their watchdog limits without the earlier command corruption/native crash. `gameplay-controls-oskkh3yh/frame.bmp00054.bmp` and `frame.bmp00067.bmp` show Tex moving forward in response to the runtime controller-report fixture. This establishes tutorial rendering and a movement path, not mission completion or physical-device QA. The visible run also exercised camera, jump and fire reports; their complete effects need further inspection. Nonzero game PCM was captured, but host audio reported starvation/underruns. The visible scene fell to about 3–4 presented frames/s. Solid 30 FPS, complete effects, audio quality, all levels and characters remain unverified.

The CPU campaign candidate is `work/diagnostic-build/campaign-test/Release/bruteforce_recomp.exe`, SHA-256 `C5BB4D4121CDE7F7EC1ADDE37F5AB41738D426FCA1BB97DA73314C14EA5333DB`. The user's latest code-21 screenshot came from the obsolete normal player with an 8-MB main stack, not this candidate. Its session log confirmed the same tutorial allocation failure. After that process exited naturally, the normal launcher player was updated to this candidate, with the old file backed up under `work/gpu-integration/rollback-old-launcher-player/`. Permanent saves and original assets were not edited; no production service was touched.

## Optional GPU scene checkpoint

Actual NV2A draws now reach the experimental Windows D3D11 scene backend.
The 238-second scratch-save run `gpu-paced-dma-30oz_ib7` rendered Tex and the
tutorial environment and ended at its watchdog without the native crash.
Late scene reports measured 29.95–30.07 presented frames/s with distinct pixel
hashes. Startup/loading dips remain, and this is not proof of mission completion
or fully working controls/effects. Audio had no reported starvation and one
underrun; listening QA remains pending.

The GPU candidate is `work/diagnostic-build/gpu-paced/Release/bruteforce_recomp.exe`.
The CPU reference remains the source default. The GPU backend honors the title's
swap interval by retaining actual DMA GET, pending packet and CALL state across
a held flip. Original physics and timestep code are unchanged. Synthetic WARP
and RTX 3060 Ti CPU/GPU scene comparisons pass; complete game-frame parity,
depth texture 0x2E/shadows and all-level tests remain open.

The native launcher and F1 now share persistent graphics/input/audio preferences.
F1's Renderer page selects scene backend, GPU vertex programs and presentation
for the next launch. The app hides only after successful game-process creation
and returns after normal exit or failure. A real-child/SDL lifecycle fixture
covers those paths. See [GPU evidence, provenance and limits](docs/GPU_SCENE_BACKEND.md).

Changed source: `src/gpu_completion.cpp`, `src/gpu_pushbuffer_fixups.cpp`, `src/gpu_event_bridge.inc`, `src/main.cpp`, `cmake/runtime_fixes.cmake`, `cmake/memory_statistics.cmake`, `cmake/pushbuffer_stream.cmake`, CMake wiring and focused GPU tests. Runtime transformations remain hash-pinned private build copies. The original reusable-buffer helper at 0x00377F00..0x00377F80 has SHA-256 `0ab8a4519944a61257e9f882992fb03c21555d692abd0353bf26c2fc3eaefe06`; its software dispatch and fence behavior were checked against the authenticated XBE and disassembly. GPU CALL register semantics were checked against xemu's public PFIFO implementation. No external binary injection was used.

## Repository integration

The native SDL2/Dear ImGui launcher replaces the removed Python HTTP server,
HTML/CSS/JavaScript UI and browser launch commands. Source builds fetch pinned
public dependencies. Original artwork/icons stay private. New imports retain
497 core-game files and omit the 188 identified ancillary files.

All review-branch commits are incorporated into main. Upstream
Tzeentchnet/BruteForce was checked at `24d2e58687c976390efa23ab8b83a6b80078f1ec`.
Its different XBE checksum means its title-specific hooks are not interchangeable.
See [the feature matrix](docs/UPSTREAM_INTEGRATION_PROGRESS.md) and
[the reuse assessment](docs/TZEENTCHNET_REUSE.md).

The texture math and HRTF fixes are applied to private runtime copies.
HLSL emitters, P8 decode, DXBC cache, scaling policy, real-frame telemetry and
completion contracts compile and pass focused checks. The optional D3D11
scene backend now executes hardware draws and reads back completed frames.
Native DX12 presentation and spatial FSR 1 consume those or CPU-reference
frames; DX12 still needs its own scene pipeline.

The USB lifecycle overhaul remains unapplied pending physical-controller and
cross-platform startup/shutdown tests. Existing USB, IRQ, typing and mouse-menu
patches are retained. No physics, timestep or arbitrary coordinate patch was
introduced. Complete input, audio/effects, all missions, characters, multiplayer
and sustained 30-FPS gameplay across missions remain open.

## Merge validation

The merged Windows Release passes 17 CTests; native Linux passes 14. Python
validation passes 14 checks, and the independent synthetic frame-comparison
suite passes 53 pytest cases. The public-only runtime configuration passes its
nine focused tests without generated game code. The native launcher rebuilds.
Third-party license spacing is preserved, source links resolve and the tracked
file audit contains no original game or compiled payloads.

The isolated merged game candidate is
`work/diagnostic-build/integration-test/Release/bruteforce_recomp.exe`, SHA-256
`467FCC4B4CA1E87A52D40A522F30471FA0494335ECCF1165C20DF24884C07F0A`.
A fresh 238-second scratch-save run under
`work/gameplay-priority/merged-campaign-z2g7ube2/` reached tutorial rendering,
produced real frame CSV records and ended at the watchdog without the earlier
native crash or bad-target diagnostics. Inspected merged-run frames show
terrain/sky but do not establish complete actor/effect rendering. Controller
reports were exercised; merged-build full character visibility and control QA
remain open. Audio starvation/underruns and low scene FPS persist. This is not
an all-level or perceptual-quality pass.

GitHub CI exposed VS 2022's raw-string token-size limit in the FSR headers.
Shader embedding now uses separate 8-KB appends. The reconstructed normalized
source matches both original headers, and the native DX12/FSR GPU smoke passes
color, alpha, letterbox, changing images, live scale, resize and shutdown.

## Preserved bring-up fixes

- GPU import guard resolves 147/147 original kernel imports without rewriting
  ordinary instructions. Local native media validation replaces the approved
  hardware-only DVD check; it verifies real local file hashes.
- Guest RAM statistics no longer report an artificial 32-MB budget. The main
  stack reserves 1 MB instead of 8 MB; the original XBE requests a 128-KB commit.
  The original 2,907,252-byte tutorial geometry buffer then allocates and its
  original relocation works. Empty movie planes caused the earlier green menu;
  the original movie buffer now allocates and renders the blue animation.
- Missing startup callback and five original CRT branch bodies are recovered
  from authenticated bytes into private files, with exact hashes. Other
  unresolved/unsupported translations remain diagnostic failures.
- Native menu mouse selection and original name-entry handlers are connected.
  F1 controls process-local presentation, prompts and audio settings. Common
  gamepad legends switch with keyboard/mouse activity. Native 16:9 scene
  projection, cheats and complete physical-device QA remain unfinished.

## Platform and rollback boundaries

Windows and native Linux builds are checked separately. Linux interactive
mission/input parity is not established by build success. NDK r30 compiled
227 ARM64 translation units; it is not an integrated Android app or S24 Ultra
APK. Android game lifecycle, rendering, input and audio remain incomplete.

Integration builds stay under ignored `work/` folders. Local player updates
are backed up separately; source integration does not migrate saves. Permanent saves,
original ISO/XBE/assets and the pinned toolkit are untouched. No production
service was touched. Git history retains the pre-integration source checkpoint
`9b780bf`; rebuild it for source rollback, preserving all player-data folders.
