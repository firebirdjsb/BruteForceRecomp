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
  staging contract extracted from upstream patches 0016/0017, derived from
  Blood Wake's `bw_backend_ext.h`; it is **not** registered into runtime.
- `src/gpu/vsh_hlsl.{c,h}` and `src/gpu/rc_hlsl.{c,h}`: generic NV2A
  vertex-program and register-combiner HLSL emitters derived from Blood Wake
  `bw_vsh_hlsl` and `bw_rc_hlsl`. The Windows fixture compiles synthetic
  shader programs; no game GPU backend is installed.
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
complete GPU back end, DSP integration or game-specific hooks.** Our staged
HLSL emitters and shader cache are identified above. Full APU and renderer
licensing must continue to respect the pinned runtime's own notices.
