# Third-party source attribution

## Tzeentchnet/BruteForce — generic NV2A texture math

`src/bf_texture_sample.h` adapts the portable coordinate fast path and DXT
cache-slot calculation from
[Tzeentchnet/BruteForce patch 0013](https://github.com/Tzeentchnet/BruteForce/blob/24d2e58687c976390efa23ab8b83a6b80078f1ec/patches/xboxrecomp/0013-optimize-texture-sampling.patch).
The source repository states its project-owned files are MIT licensed. This
adaptation renames the functions to avoid a collision with the pinned runtime.
It does not include any original game assets or generated game code.

Further MIT-licensed adaptations from the same upstream repository:
- The primitive-draw environment lookup portion of patch
  `0019-cache-hot-getenv.patch`, implemented in our private runtime transform.
- The Windows framebuffer publication synchronization from
  `0005-synchronize-presented-frames.patch`, adapted into our private
  build-copy transformation and preserving the Linux SDL path.
- `tools/compare_frames.py` and `tests/test_compare_frames.py` from
  upstream commit `24d2e58687c976390efa23ab8b83a6b80078f1ec`, to
  compare externally captured CPU/GPU/xemu frames using synthetic tests.
  Images and native game assets are never committed.


MIT License

Copyright (c) 2026 the Brute Force recompilation contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Additional GPU, audio and benchmarking components

The following generic sources come from MIT-licensed
[Tzeentchnet/BruteForce](https://github.com/Tzeentchnet/BruteForce)
(commit `24d2e58687c976390efa23ab8b83a6b80078f1ec`) and, for
specific files, from the MIT-licensed
[bloodwake-recomp](https://github.com/minimalice92/bloodwake-recomp)
(commit `e631edddfe56d22344e9f696923aeb6d78a9e7ba`).
Their game-specific code, shaders, demo assets and Xbox hooks are not included.

- `src/gpu/nv2a_backend_ext.h`: the extended GPU batch and vertex-program
  contract extracted from upstream patches 0016–0021, derived from
  Blood Wake's `bw_backend_ext.h`; the optional D3D11 backend registers it.
- `src/gpu/vsh_hlsl.{c,h}` and `src/gpu/rc_hlsl.{c,h}`: generic NV2A
  vertex-program and register-combiner HLSL emitters derived from Blood Wake
  `bw_vsh_hlsl` and `bw_rc_hlsl`. The Windows fixture compiles synthetic
  shader programs; the optional D3D11 backend uses them for real draws.
- `src/gpu/shader_cache.{c,h}`: persistent DXBC cache based on Blood Wake
  `bw_pscache.c` with content/compiler hashes, validated entries and atomic
  file publication, exercised by synthetic tests.
- `src/gpu/render_scale_policy.h`: portable mathematical subset of the
  upstream scaling policy, which derives from Blood Wake's MIT
  `bw_vfx.c`/`bw_vfx_post.c`. No visual effects or actual GPU scaling
  passes have been imported.
- `src/gpu/nv2a_frame_stats.{c,h}`: generic per-flip pixel hashing and
  CSV reporting from upstream patch 0014; its integration transforms only
  the private, original-verified NV2A executor.
- `src/gpu/nv2a_p8_decode.{c,h}`: adapted paletted texture decode from
  upstream `texture_decode.c`, using the pinned framework's MIT swizzle
  functions. The CPU decoder does not itself install a DX11/DX12 texture.
- The optional upstream `0035-preserve-mono-hrtf-stereo.patch` was adapted
  into our private `apu_vp.c` build copy. The underlying APU files are
  inherited from xboxrecomp and retain **their existing xemu-derived LGPL
  terms**; our project license does not relicense the original APU.
- `tests/apu_vp_hrtf_test.c` was adapted from upstream's MIT additions
  atop xboxrecomp's existing APU test harness; original framework rights
  and notices still apply.
- `src/gpu/d3d11_backend.{c,h}` and `tests/gpu_scene_test.c` adapt the MIT
  generic backend/fixture at upstream commit
  `6527667105fa1ad693415537d2cd56db3ff3a180`. `texture_decode.{c,h}`,
  `d3d11_device_status.{c,h}` and `tests/gpu_scene_textures.c` use commit
  `24d2e58687c976390efa23ab8b83a6b80078f1ec`. Blood Wake's original
  backend provenance and MIT notice are retained above/below.
  The CPU/GPU guest-color snapshot/ownership protocol is adapted from that
  same upstream commit; shared/scaled presentation is not imported.
- `src/gpu/runtime/*` and `patches/nv2a-d3d11/executor.patch` adapt generic
  MIT xboxrecomp runtime changes in public patches 0008–0021 (excluding
  census 0015), plus GPU clear offload equivalent to 0022. They preserve
  our independently verified title-specific integration. No generated
  game body or original game data is included.

The optional benchmark tools and tests use only synthetic test inputs. No
original executable data or separately licensed GPL DSP interpreter is copied.

## Blood Wake project-owned source — MIT License

MIT License

Copyright (c) 2026 the bloodwake-recomp contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Other upstream components (not incorporated)

The other repository also uses separately licensed GPL-2.0-or-later
xemu DSP code and LGPL xemu/APU integration, and includes a complete
Blood Wake-derived MIT D3D11 renderer. **We have not imported the GPL DSP,
DSP integration or game-specific hooks.** The optional generic D3D11 backend,
HLSL emitters and shader cache are identified above. Full APU and renderer
licensing must continue to respect the pinned runtime's own notices.

## Native application dependencies

- AMD FidelityFX FSR 1 headers retain AMD's MIT license in
  `third_party/fsr1/LICENSE.txt`; their pinned origin is recorded in
  `third_party/fsr1/README.md`.
- The native launcher fetches SDL2, Dear ImGui and XboxRecomp at exact commits
  in `native_launcher/CMakeLists.txt` and copies their licenses beside the
  built application.
- Rajdhani fonts retain the SIL Open Font License in
  `native_launcher/assets/fonts/OFL.txt`. Original game artwork and icons are
  extracted only from a user's local disc and are not included in source.

## Optional AMD FSR SDK 2.4

The optional temporal adapter uses AMD's public API headers at build time and
loads the SDK's signed DX12 binary distribution for local hardware tests.
The SDK is obtained separately and is not vendored in this repository.
Binary packages must retain the official SDK license, copied beside the test
executable as AMD-FSR-SDK-LICENSE.md. See
[AMD SDK license](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.4.0/Kits/FidelityFX/docs/license.md)
and [integration notes](docs/FSR_TEMPORAL.md).
