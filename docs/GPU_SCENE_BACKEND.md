# Experimental hardware scene rendering

The Windows D3D11 scene backend now consumes actual NV2A clear and draw
commands. It is distinct from the DX12/D3D11 **presentation** selector.
The source default remains the CPU reference.

Build with `-DBF_EXPERIMENTAL_NV2A_D3D11=ON`, then select **D3D11 GPU
(experimental)** in the launcher's Graphics section. Builds advertise support
through `bruteforce_recomp --capabilities`; the launcher rejects an unavailable
selection before hiding. Direct runs use `RECOMP_GPU=d3d11` and
`RECOMP_FLIP_PACING=edge`. `RECOMP_GPU=cpu` retains the software path.

The backend handles fixed-function batches, register combiners, raw vertex
streams and generated GPU vertex programs, color/depth surfaces, clears,
render-to-texture and visibility queries. The completed color surface is read
back into guest memory on each flip. The existing native presenter, FSR 1,
captures and pixel hashes then consume those real pixels. This is not a native
DX12 scene renderer or a zero-copy shared presentation implementation.

Texture and palette snapshots are compared in full on every use, including
changes within one frame. Cached pixel-shader compilation uses the validated
persistent DXBC cache. Vertex shaders compile on the existing asynchronous
worker and have an in-process cache; their persistent caching is unfinished.
GPU completion is awaited with a real D3D11 EVENT query before the original
guest semaphore or software fence event is signaled. Submission/Flush alone
does not publish completion.

## Original frame pacing

The title requests a two-vblank swap interval in the tutorial. The old walker
could not pause inside a reusable pushbuffer CALL. The DMA reader now preserves
the pending method payload, actual GET and shared CALL state when FLIP_STALL
holds execution. Its consumer resumes even if PUT has not changed. A focused
regression checks a held split packet inside CALL, its remainder and RETURN.
The experimental backend permits that wait in DMA mode; the legacy segment
walker retains its old restriction. Physics, timesteps and original guest
simulation code are not patched.

## Native settings and launcher behavior

F1 has **Live Settings** and **Renderer** pages. Tab/Page Up/Page Down or the
page buttons switch pages. Mouse, prompts, FSR output, window size and audio
remain live. Scene backend, GPU vertex programs and presentation device apply
on the next launch. They never replace a GPU device while draws are in flight.
Settings launched through the app persist in `graphics.settings` beside
`launcher.settings`; F1 writes the same preferences and the app reloads them
after exit. Direct runs without `BF_UI_SETTINGS` keep session-only settings.

The launcher hides only after successful child creation, stops rendering while
hidden, and shows/raises its existing window on normal exit or failure. Failed
validation/creation leaves it visible. It neither embeds the game window nor
terminates the game to close the launcher. Permanent player-data is separate.

## Evidence and limits, 2 November 2026

- Synthetic CPU/GPU fixed-function, CPU-transformed and GPU vertex-program
  scenes pass on WARP and RTX 3060 Ti. RGB differences are within 2/255 except
  42 known rasterizer-edge pixels; the gate allows at most 64 such pixels.
  Eleven GPU vertices across three batches match the interpreter with zero
  verification differences. This is not full-game matched-frame parity.
- The 238-second scratch-save run
  `work/gameplay-priority/gpu-paced-dma-30oz_ib7/` rendered Tex, terrain,
  vegetation and sky, and ended at its watchdog without a native crash or
  invalid guest-code target. Its late tutorial reports measure 29.95–30.07
  presented frames/s with distinct pixel hashes, no hold timeouts or stopped
  clocks. These rates include animated scenery and do not establish mission
  completion or a minimum FPS across all situations. Startup/loading have dips.
- Controller-report movement/camera/jump/fire inputs were submitted, but full
  control behavior and mission progression are not verified. No physical-device
  or complete in-game QA is claimed.
- Audio telemetry reported no starvation and one underrun in this run. There
  was no listening test; crisp/glitch-free audio is not established.
- Depth texture format 0x2E remains unsupported with experimental depth aliases
  disabled. Shadows/effects and full title fidelity need further work. Native
  widescreen, all missions/characters, extended multiplayer, DLSS, interpolation
  above the simulation rate and an Android APK remain unfinished.

The public-only GPU test needs no game data:

```powershell
cmake -S . -B .local/gpu-check -DXBOXRECOMP_DIR=PINNED_TOOLKIT -DBF_RUNTIME_CHECKS_ONLY=ON -DBF_EXPERIMENTAL_NV2A_D3D11=ON
cmake --build .local/gpu-check --config Release --target bf_gpu_scene_test
ctest --test-dir .local/gpu-check -C Release -R nv2a_gpu_scene_parity --output-on-failure
```

## Source provenance and rollback

The MIT generic backend and scene fixtures derive from Tzeentchnet/BruteForce
commit `6527667105fa1ad693415537d2cd56db3ff3a180`, before its shared/scaled
presentation changes. Texture decoding/device-status helpers derive from
`24d2e58687c976390efa23ab8b83a6b80078f1ec`. That renderer credits Blood Wake
commit `e631edddfe56d22344e9f696923aeb6d78a9e7ba`.
The runtime contract/interpreter/executor additions adapt public patches
0008–0021, excluding census patch 0015, plus equivalent GPU clear offload from
0022. Clipping, culling and near-plane experiments remain off by default.
Our original software commands, semaphore target, texture math and diagnostics
are preserved. No upstream title hooks, USB overhaul or DSP interpreter were
copied. See `THIRD_PARTY_NOTICES.md` for retained licenses.

`cmake/scene_backend.cmake` accepts only canonical executor input SHA-256
`e439de1f2e5ae9284d4dddfe5b48da64c6f68b4c158394bfe3d7620a5073d265`
and applies `patches/nv2a-d3d11/executor.patch` to a private build copy. The
pinned toolkit and proprietary inputs stay untouched. Disable the CMake option
and choose CPU reference for source rollback. No production service is touched.
