# Next-agent handoff prompt — 2 November 2026

Latest follow-up: main now includes newer desktop input, bloom-mask and tutorial
artwork fixes documented at the top of STATUS.md; those supersede the historical
details below. A further launcher fix resolves the native player from SDL paths
ending in a separator and recovers missing saved executable paths. The normal
AppData library's game files were valid but its player path was empty. Its path
is now repaired without moving either library or any saves. Windows/Linux tests
cover player discovery and launcher lifecycle; inspect STATUS.md and current
source before treating an older artifact/hash below as the latest build.

Continue the Brute Force native recompilation in
`D:\Documents\ChatGPT\bfRecomp` on Windows/PowerShell. Read the user's AGENTS.md
instructions and the current repository before editing. Fetch private
`firebirdjsb/BruteForceRecomp`, inspect main and local changes, and preserve
unrelated work. The user explicitly authorized pushing all source changes.
The old review branch was merged into main; the browser/HTTP launcher was removed.
The application is native SDL2/Dear ImGui. Do not reintroduce a website.

## User priorities and latest reports

Playable campaign with reliable input and good frame rate is the first priority.
The latest reports were: launch black screen; add D3D12 GPU support; no mouse
movement; missing HUD; coarse/pixelated image; apparent gameplay freeze.
The user confirmed **movies and menus became visible** after the movie fix.
The missing HUD reference has health/ability bars, ammo, crosshair, radar,
portrait and grenades. Screenshots are private and must not be committed.

The user then asked to push all changes and write this handoff. Be candid about
runtime QA. A build, synthetic fixture or frame counter does not establish
complete gameplay, all missions, physics fidelity or physical-device support.
Use at most two focused repair loops per issue, then report the failure and
safest next step. Never guess physics, timestep or coordinate patches.

## Current fixes and their evidence

1. **Black movies:** the GPU renderer had seeded a color surface once and
   later overwrote CPU/Bink writes with stale black GPU pixels. The backend now
   tracks guest color snapshots, mirrors complete clears, uploads changed
   CPU-owned surfaces before drawing/readback, and records GPU color ownership.
   The warm-cache baseline captured 30 black frames. The fixed visible run
   `work/black-screen-qa/fixed-warm-gfa3z52v/` captured 42 nonblack images with
   the user's DX12/FSR/window settings. Compared cache files matched the disc;
   no cache reset or permanent-save change was performed. The user confirmed
   visibility after the normal player was updated.
2. **Invisible tutorial / HUD:** legacy programmable near clipping discarded
   screen-depth-zero UI against viewport constant c[59].z. The mission was
   actually paused at an invisible **Basic Controls** tutorial dialog. A
   controlled `RECOMP_PROGRAM_NEAR_ZERO=1` run restored that dialog and the HUD.
   The authenticated game's GPU boot path now defaults this existing generic
   option to 1, with an explicit diagnostic override retained. This changes
   rendering, not physics or simulation. Synthetic zero-depth HUD coverage was
   added. Dismiss/skip the tutorial before judging movement.
3. **Mouse launch wiring:** the launcher previously set RECOMP_KEYBOARD but
   never RECOMP_MKB. Its checkbox now enables both and reads “Mouse and keyboard
   controls.” WASD and raw mouse use the existing virtual-controller adapter;
   F9 releases/toggles capture. Typing, F1 and menu ownership still gate that
   adapter. Complete physical mouse/controller QA remains pending.
4. **D3D12 scene option:** `RECOMP_GPU=d3d12` now creates a real D3D12 device,
   direct queue and fence, then drives them through Microsoft's D3D11On12
   bridge. Scene commands, shaders, visibility and readback use that device.
   The separate DX12 presenter/FSR remains in `src/graphics/`. This is an
   explicitly labeled bridge, **not a native D3D12 scene pipeline**. Both
   launcher Graphics and F1 Renderer expose it. Both scene APIs pass synthetic
   WARP and RTX 3060 Ti CPU/GPU comparisons. D3D12's initial real-title modal
   run was roughly 13 FPS with synchronous visibility; do not promise a speedup.
5. **Visibility stalls:** the synchronous path spent seconds waiting for
   hundreds of occlusion results per frame. The existing asynchronous report
   path publishes only after actual GPU query completion and is polled by the
   NV2A consumer. The bounded 178-second
   `work/gameplay-priority/hud-async-5pqd3d11/` run reached a visible HUD/world,
   ammo changed from 48 to 35 after fire input, and late reports measured
   29.99–30.01 presented frames/s with zero blocking visibility-read time.
   It ended at its watchdog without invalid guest-code/native-crash diagnostics.
   Async reports are now the GPU default; `RECOMP_GPU_ZPASS=sync` retains the
   diagnostic reference. Explicit/default async synthetic cases were added.

## Remaining defects: do not call this a finished playable release

- **Overbright/speckled world and low detail remain.** The scene is 640x480
  before presentation scaling. Texture upload/sampling uses level 0 and no
  mip filtering (`td.MipLevels=1`, sampler MaxLOD=0). FSR 2x/4x/8x is output
  scaling, not higher internal resolution or HD textures. Respect original
  texture metadata, palettes, mip/cube layouts and bounds when implementing
  mip filtering. Do not merely blur the framebuffer and claim fidelity.
- Near-zero compatibility also exposes bright additive effects. Upstream
  generic patch `0029-hud-safe-near-plane.patch` is a **candidate to investigate**
  against our exact XBE and matched captures; it is not applied. It distinguishes
  additive draws from normal UI. Do not blindly import a different title build's
  hooks or enable a clipping heuristic without checking the actual result.
- The longer synchronous run
  `work/gameplay-priority/hud-input-live-8c8po0tb/` showed HUD and firing, then
  low FPS and late solid-blue captures (`frame.bmp00087.bmp`, etc.). Its earlier
  frames 60/65/70 show a visible HUD and ammo response. The shorter async run's
  final frame 86 still shows the world/HUD. These are **not matched A/B/A runs**;
  the late blue-frame defect is not conclusively fixed. Reproduce it with
  equivalent state/input and a longer run before declaring it gone.
- Full walking/camera/control progression, physical keyboard/mouse/controller
  behavior, mission completion, all characters/abilities, all levels/multiplayer,
  crisp audio and sustained minimum FPS remain unverified. Depth texture format
  0x2E/shadows remain incomplete. Original prompt glyphs in tutorial artwork
  have not all been dynamically replaced.
- Native widescreen/projection/HUD, high internal resolution, DLSS, interpolation
  beyond the original simulation rate, cheats, expanded P2P multiplayer and a
  native Android APK are unfinished. Canadian S24 Ultra is the Android target.
  NDK r30 is locally approved/installed; an object audit is not an APK.
- The invasive upstream USB lifecycle overhaul remains gated on real devices
  and cross-platform shutdown testing. Preserve the working original USB,
  IRQ, software-command and semaphore fixes.

## Source map

- `src/gpu/d3d11_backend.c`: surfaces/coherence, shaders, texture cache,
  draws, visibility, readback and completion.
- `src/gpu/scene_device.{cpp,h}`: D3D12 device/queue/11On12/fence ownership.
- `cmake/scene_backend.cmake`, `patches/nv2a-d3d11/executor.patch`:
  hash-checked private executor integration. `BF_EXPERIMENTAL_NV2A_D3D11=ON`
  compiles both GPU scene choices; the source default remains OFF/CPU.
- `src/main.cpp`: authenticated startup, capabilities and title HUD default.
- `native_launcher/{app,library,process,main}.cpp`: controls, persistence,
  capability checks, child environment and hide/return lifecycle.
- `src/ui/{host_ui.cpp,graphics_options.h}`: F1 tabs and shared preferences.
- `tests/check_gpu_scene.py`, `tests/gpu_scene_test.c`: real synthetic draws,
  CPU movies, zero-depth HUD, async reports and GPU vertex-program comparisons.
- `tests/launcher_lifecycle_test.cpp`: actual child environment and SDL
  hide/restore for normal/failed exits. `tests/host_ui_test.cpp`: F1 persistence.

## Local applications, data and rollback

- Normal launcher:
  `work/native-launcher/build/native_launcher/Release/BruteForceLauncher.exe`
- Normal game: `work/diagnostic-build/Release/bruteforce_recomp.exe`
- Latest source-built player:
  `work/diagnostic-build/final-handoff/Release/bruteforce_recomp.exe`
  and the normal player now both have SHA-256
  `AB5D540B407A61357D73F2F46418F1B182F6CC5F82F9E169A530BA5749345607`.
  It includes the async default and was copied only after the normal player
  exited. The launcher remains open, with the user's settings preserved.
- Intermediate HUD/input player:
  `work/diagnostic-build/hud-input-fix/Release/bruteforce_recomp.exe`, SHA-256
  `9AD2E23D2F64123063D2A9F56F483D42C7551DD9B452837F8CA177A3C7D34F8B`.
  It was installed before async became the default. Verify the normal player's
  hash against the latest artifact before replacing or making runtime claims.
- Local app preferences: `work/native-launcher/core-qa/launcher.settings` and
  `graphics.settings`. Preserve the user's choices. Renderer changes require
  a restart. F1/live preferences share the latter file. D3D11 is the conservative
  local scene choice; D3D12 bridge is optional.
- Imported filtered game: `work/native-launcher/core-qa/game-core` (497 files).
- **Permanent saves:** `work/native-launcher/core-qa/player-data`. Never reset,
  delete, patch or overwrite them to make tests pass. Use scratch saves.
- Backups: `work/black-screen-qa/rollback-before-movie-fix/`,
  `work/black-screen-qa/rollback-before-hud-input/`,
  `work/black-screen-qa/rollback-before-async/`, and earlier
  `work/gpu-integration/rollback-before-gpu-f1/`.
- Check Get-Process with exact executable paths before replacing files. The
  user may be actively testing. Never kill their session to deploy a build.
  The launcher hides during its child process and returns after exit/failure.

## Rebuild and verify

Pinned clean toolkit:
`C:\Users\fireb\Documents\Codex\2026-10-31\le\work\xboxrecomp`, commit
`f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b`. Do not modify it.
Private generated C:
`C:\Users\fireb\Documents\Codex\2026-10-31\le\work\bf-analysis\generated`.
Private recovered bodies: `work/recovered`.
Supported XBE: `D:\Downloads\BruteForce-Decompilation\default.xbe`, SHA-256
`B6EA5D4DD6500AE86C3F5E3E4618BC9B1E3FACDDCB483170DFEE7A787087C721`.
The other upstream repository supports a different checksum. Never copy its
title-specific addresses/bodies without our original-byte evidence.

Build existing cache with
`cmake --build work/diagnostic-build --config Release --parallel 4`.
Use a separate BF_HOST_OUTPUT_DIR when a player is running. Windows passes
17 CTests, native WSL Linux passes 14, and Windows/Linux launcher lifecycle
tests pass. Public `work/public-runtime-smoke` builds with no proprietary game
code and runs both `nv2a_gpu_scene_parity` and `nv2a_d3d12_scene_parity`.
`python tests/check_gpu_scene.py EXE PUBLIC_TEST_XBE OUTPUT --hardware --d3d12`
uses actual hardware; omit --d3d12 for D3D11. RGB tolerance is 2/255 with 42
known rasterizer-edge pixels in the baseline scenes, not whole-game parity.

WSL distro: BruteForceBuild; source `/mnt/d/Documents/ChatGPT/bfRecomp`;
game build `/var/tmp/bruteforce-build`; launcher `/var/tmp/bf-native-launcher`.
Private state-driven runners/logs are under `work/black-screen-qa/` and
`work/gameplay-priority/`; inspect their exact inputs before comparing runs.
Use the game's own bounded input/frame fixtures. Avoid desktop input automation
that could interfere with the user's session.

After a focused fix, run relevant checks, inspect real captures, update honest
status/provenance, commit scoped source and push main. Never commit XBE/ISO,
assets, generated game code, saves, private dumps, credentials or binaries.
No production service or permanent progress has been modified.
