# BruteForceRecomp: upstream feature integration and DirectX 12 readiness

Reference: [Tzeentchnet/BruteForce](https://github.com/Tzeentchnet/BruteForce)
commit `24d2e58687c976390efa23ab8b83a6b80078f1ec`.
The review branch is integrated into main with the native launcher and campaign fixes.
The upstream main branch was rechecked on 2026-11-02; it remains at the commit above.

[Every numbered upstream patch is accounted for here](UPSTREAM_PATCH_AUDIT.md).

## What is implemented and what still needs integration

| Priority | Feature | Implemented in main | Not yet complete |
| --- | --- | --- | --- |
| Highest | GPU renderer and Xbox vertex programs | Optional D3D11 scene backend executes actual NV2A clears/draws, combiners, vertex programs and readback. Synthetic CPU/GPU parity passes on WARP and RTX 3060 Ti. A bounded tutorial run renders Tex/environment at about 30 presented frames/s late in the run. | Full game-frame parity, control/progression QA, effects/shadows and all missions. The separate native DX12 presenter/FSR path does not execute NV2A scene draws; native DX12 scenes need their own pipeline. |
| High | Frame synchronization/presentation | Windows framebuffer lock and DX12 completed-fence bridge; optional D3D11 real EVENT-query completion before guest signals. DMA GET/CALL/payload survives original swap-interval waits. | GPU-resident shared presentation, full flip/readback equivalence and lifecycle QA. Current GPU scene frames are read back for the native presenter. |
| High | Texture decoding / shader caching | CPU texture math, GPU texture/sampler/BC staging, bounds-checked P8 decode, exact per-use source/palette validation and persistent DXBC cache at pixel-shader compile sites. | Depth texture 0x2E/shadows, full mutation/parity coverage and persistent vertex-shader caching. DXC/DXIL for native DX12 needs its own compiler-key validation. |
| High | Positional audio/HRTF | Upstream mono-source HRTF fix applied **in the private pinned xbox_apu build copy**. Actual APU test harness verifies left and right channels retain different HRTF results, plus ADPCM and gain checks. | Full end-to-end speaker/HRTF listening and mixed soundtrack tests. No GPL DSP interpreter or unreviewed audio subsystem transplant. |
| Medium | Render scaling | Verified portable render-size, native-output-height and aspect-preserving viewport math, including 1440p/4K cases. | FSR 1 output factors and window presets exist in the native F1 overlay. Higher-resolution scene targets, native widescreen projection/HUD and GPU guest readback remain unfinished. 4:3 output is **not** artificially stretched. |
| Medium | USB | Existing original USB/IRQ/dispatch gate patches remain intact. Upstream patch `0006` audited: it changes thread startup, TIB ownership, MMIO protection lifetime, IRQ safe points and shutdown API. | **Not applied** until dependent kernel calls and Windows/POSIX startup/shutdown tests can be verified without invalidating our working controller paths. |
| Medium | Benchmarking | Opt-in real frame CSV/hashes, game-run captures including GPU readback, distinct/unknown counts, draw/vertex counters, frame parser and synthetic comparison gates. | Independently matched game scenes/inputs, controlled A/B/A speedup tests, sustained all-mission minimum FPS and world-progression validation. |

## Using the opt-in frame benchmark

In the normal game configuration, leave `RECOMP_FRAME_STATS` unset to avoid
frame hashing and disk writes. For an explicitly selected test run:

```powershell
$env:RECOMP_FRAME_STATS = "C:\\temp\\bruteforce-cpu-frames.csv"
# Run the existing launcher with your locally owned original game files.
python tools/frame_bench.py C:\\temp\\bruteforce-cpu-frames.csv
```

The CSV fields come from upstream patch 0014:
`flip,t_us,draws,program_vertices,hash,distinct,constants`. Hardware-backend
frames whose pixels are absent from guest memory are **unknown**, not assumed
distinct or identical. The parser reports descriptive flip and distinct-image
interval rates, never claims two runs matched scene/input automatically.
With a CPU renderer, a changed image can still be UI rather than game-world
progress; interpret these rates with matched input/frame captures.

Use `tools/compare_frames.py` for still images captured at equal resolution,
viewpoint, input state and flip phase. Its RGB diff is *not* an in-game matcher.

## DirectX compatibility rules

- The common original-Xbox/NV2A register-combiner and vertex-program HLSL
  generation logic can be shared by a DX11 path and a future DX12 path.
- The CPU texture helpers and guest completion semantics are graphics-API
  independent. Cache formats and compiled bytecode **are not** automatically
  interchangeable between DXBC (D3DCompiler) and DXIL (DXC).
- DX12 fences, descriptor heaps, PSOs, resource transitions, allocator
  reuse, upload/readback, and presentation remain DX12-specific.
- The Windows CPU/GDI SRW lock fix is already applied to the pinned runtime
  and continues to protect the old presenter. A GPU-resident swap chain needs
  completion fencing, not a copied CPU lock.
- Existing Linux SDL and future Android Vulkan paths must not be forced
  through DirectX-specific interfaces. See `docs/DX12_COMPATIBILITY.md`.

## Fidelity and merge gates

1. Pinned-runtime Windows synthetic build/test must pass; game-specific XBE
   checksum gate and original callback provenance stay unchanged.
2. Boot the real title using the approved original XBE on Windows Debug and
   Release. Confirm the existing startup/menu/mission loading and GPU semaphore.
3. Collect matched CPU versus experimental backend captures, then validate
   depth/stencil, surfaces, texture updates, readback, HUD, FMV, clip, and audio.
4. Check Linux asynchronous IO, USB lifecycle, and input; Android remains a
   separate compile/device target. Test actual controllers before USB patch 0006.
5. Keep third-party attribution. No original title/demo executables or assets
   are committed. This is not a finished DX11/DX12 GPU game port.

The current software renderer stays the reference; the optional GPU path has
bounded tutorial evidence described in [the backend report](GPU_SCENE_BACKEND.md).
**Experimental renderer code should never overwrite our
existing original-XBE-derived patches to make a build green.**
