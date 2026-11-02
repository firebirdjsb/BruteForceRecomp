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

## Other upstream components (not incorporated)

The other repository also uses separately licensed GPL-2.0-or-later xemu DSP
code, LGPL xemu/APU integration, and Blood Wake-derived MIT renderer sources.
Those components **have not** been copied into BruteForceRecomp by this
integration. Before any later audio/renderer port, inspect and retain the
specific original copyright/license headers and their third-party notices.
