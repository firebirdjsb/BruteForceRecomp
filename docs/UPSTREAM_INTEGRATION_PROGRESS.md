# BruteForceRecomp: upstream feature integration and DirectX 12 readiness

Reference: [Tzeentchnet/BruteForce](https://github.com/Tzeentchnet/BruteForce)
commit `24d2e58687c976390efa23ab8b83a6b80078f1ec`.
All work is staged on the review branch. **It has not been merged into main.**

## What is implemented and what still needs integration

| Priority | Feature | Implemented on review branch | Not yet complete |
| --- | --- | --- | --- |
| Highest | GPU renderer and Xbox vertex programs | Imported MIT NV2A extended batch **staging interface**, register-combiner HLSL and vertex-program HLSL emitters. Synthetic pixel and vertex shaders compile with Windows D3DCompiler. Shared semantics are intended for DX11 and DX12. | The full D3D11 backend's surfaces, shader invocation, vertex streams, GPU draw dispatch and full-frame readback are **not connected** to our pinned executor. Neither DX11 nor DX12 game rendering is enabled. |
| High | Frame synchronization/presentation | Existing Windows GDI framebuffer SRW lock + DX12 completed-fence bridge (tested with WARP). Original GPU semaphore patch is preserved. | DX11 GPU surface presentation and native DX12 swap-chain/resource barriers must be connected to real game draws and verified. |
| High | Texture decoding / shader caching | Runtime CPU texture fast path and DXT slot improvement; portable, bounds-checked P8 swizzled-paletted decode; Windows persistent compiler/content-keyed DXBC cache, corruption validation. | Complete GPU texture/sampler/BCn staging and cache lookup at actual D3D11/DX12 shader compilation sites. DXC/DXIL for native DX12 requires its own compiler-key validation. |
| High | Positional audio/HRTF | Upstream mono-source HRTF fix applied **in the private pinned xbox_apu build copy**. Actual APU test harness verifies left and right channels retain different HRTF results, plus ADPCM and gain checks. | Full end-to-end speaker/HRTF listening and mixed soundtrack tests. No GPL DSP interpreter or unreviewed audio subsystem transplant. |
| Medium | Render scaling | Verified portable render-size, native-output-height and aspect-preserving viewport math, including 1440p/4K cases. | Actual GPU-sized render targets, resolve/downsample passes, HUD, widescreen scene-geometry proof and guest CPU readback. 4:3 output is **not** artificially stretched. |
| Medium | USB | Existing original USB/IRQ/dispatch gate patches remain intact. Upstream patch `0006` audited: it changes thread startup, TIB ownership, MMIO protection lifetime, IRQ safe points and shutdown API. | **Not applied** until dependent kernel calls and Windows/POSIX startup/shutdown tests can be verified without invalidating our working controller paths. |
| Medium | Benchmarking | Opt-in `RECOMP_FRAME_STATS=<path>` CSV emitted by private NV2A runtime (visible framebuffer hash, distinct/unknown status, draw counts, program vertices). Synthetic hashing tests and `tools/frame_bench.py` metrics parser. `tools/compare_frames.py` reports pixel differences on matched images. | Game-run captures, verified scene/input equivalence, controlled A/B/A tests, GPU-presented-frame hashes and real FPS claims. |

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

The current software renderer stays the only gameplay-backed reference until
those gates succeed. **Experimental renderer code should never overwrite our
existing original-XBE-derived patches to make a build green.**
