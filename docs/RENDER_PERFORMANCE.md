# Rendering performance and remaining graphics work

## Follow-up: bounded texture lookup hints

The GPU scene cache now has an 8-KiB, 4,096-bucket descriptor lookup hint table.
Each bucket holds a cache slot index plus one. A candidate must pass the same
complete descriptor comparison as the original linear search. Collisions,
empty buckets and stale LRU slots fall back to that search. Hints never replace
source/palette byte comparisons, overlapping-surface synchronization, decoding
or original mip/alpha handling. Only descriptor integers are mixed for indexing;
guest texture data is still checked exactly on every use.

`BF_PROFILE_RENDER=1` now includes successful hint and fallback-search counts.
Use those counts and the texture timer to evaluate this change in the same
mission position; no minimum-FPS improvement has yet been established. The
Windows player links and all 33 checks pass, as do public DX11/DX12 NV2A parity
and scene-AA preservation (3/3). Those fixtures exercise same-frame texture and
palette rewrites, mip chains, format changes and render-target aliases. No
GPU wait, simulation timing, raw mouse, save data or launcher option was changed.

## Current optimization checkpoint

The D3D11 and D3D12/11On12 scene cache no longer computes texture/palette
hashes that were stored but never read. Exact source and palette byte comparisons
still run at every texture use, including writes inside one frame. Surface
alias/readback rules, original mip chains and texture reconstruction are unchanged.
`BF_PROFILE_RENDER=1` adds a bounded ten-second texture lookup/validation/upload
timing report. The byte counter describes requested comparison spans, including
comparisons that exit early; it is not measured memory bandwidth.
The same switch now reports total/count/maximum time in the real blocking scene
completion function, without changing its fence/event/query behavior.

DX12 presentation now retains an exact CPU copy of the last uploaded color image.
Identical images avoid both another GPU upload and another EASU/RCAS pair. Source
sequence IDs alone are insufficient: the F1 compositor can change pixels while
an ID repeats. Every byte is checked, so same-ID UI edits still refresh.

Visual interpolation also reuses a generated image when a high-refresh display
requests the same quantized phase. FSR results are invalidated by changed source
pixels, a different sampled history/output resource, a new source sequence, a new
generated phase, resize, or a live scale change. Resource-state transitions remain
explicit even on cached frames. No present, guest update, fence, original vblank
interval or original GPU wait was removed.

`BF_PRESENT_STATS=1` reports real source images separately from display presents,
new generated phases, uploads and FSR evaluations. A reported 180 display
presents per second does **not** mean 180 game frames or 180 unique images.

## Validation and limits

- Windows player links; 33/33 player checks pass. Linux player links and 22/22
  checks pass. These do not establish complete gameplay or Linux GPU support.
- Real NV2A CPU/D3D11/D3D12 comparisons pass, including same-frame texture and
  palette changes, original mip chains and render-target aliases.
- The separate HUD/text AA preservation fixture passes. This change adds no
  final-image AA and does not change the accepted raw mouse implementation.
- Hardware DX12 checks on an RTX 3060 Ti pass for bilinear/FSR1, resize, scale
  changes, downscale fallback, repeated frames and same-ID one-pixel UI edits.
  Three repeated images cause zero additional uploads/FSR evaluations.
- Real quarter/half/three-quarter interpolation checks pass, including repeated
  phases with FSR enabled, stalled/skipped history, disabled interpolation and
  shutdown. Repeated phases do not add generated-frame or FSR counts.
- Disposable tutorial gameplay reached the actual level, movement and firing
  inputs without an invalid/unresolved guest call; runs ended at their watchdog.
  A busy view still produced roughly 15-19 real source FPS. In one 180-Hz output
  interval, about 15 uploads/s served 180 presents/s. That run had FSR disabled;
  FSR reuse was verified separately by the hardware fixture.

The scripted runs entered the level at different times and used different
interpolation modes. They are **not** a controlled before/after FPS comparison.
No improvement in minimum mission FPS is claimed. The user identified missions
3 and 4 as slow; those exact scenes still need a repeatable profile. No mission
completion, multiplayer, sound-quality or controller acceptance claim is made.

Live user mission 4 was subsequently inspected without changing controls or
guest memory. Its resource path is `M02_a`, not `M04_a`. The scene produced
roughly13-20 source FPS; representative intervals showed0.77-0.82ms readback per
flip,85-98ms/s in texture lookup/validation/upload and stable60-Hz vblank with
no late/given-up acknowledgements. Actual FSR evaluations were about71-73/s
for180 presents/s, with16-18 uploads/s. A10.13-second read-only method-counter
sample found5,863 semaphore releases,23/100 PFB-busy samples and38/100 samples
with DMA_GET behind PUT. These are observations, not measured wait durations or
proof that changing semaphore handling will recover30FPS. The follow-up build
adds explicit completion-wait duration telemetry for that comparison.

The follow-up live M02_a session successfully captured a 10.012-second interval:
170 flips (about 17 source FPS), 101.21 ms/s in texture work and 120.16 ms/s across
5,555 ordered-completion waits (maximum 2.367 ms), with 0.80 ms/flip readback.
About 180 display presents/s included about 51 new generated phases/s; FSR ran
about 73 times/s. Wait cost alone is not proven to explain the entire deficit.
Some timers overlap; these numbers must not be added as independent costs.
Unsupported 0x2E texture and CPU-write/pending-color warnings also remain open.

## Next performance investigation

A bounded read-only sample of a disposable QA process found substantial main
thread time in original `0x00372180` (45-byte PFB flush kickoff/acknowledgement
loop) and consumer time in clock/polling routines. This is evidence for tracing
producer/consumer wait durations, not permission to skip flushes, fake DMA_GET,
clear pending reports, remove waits, or change simulation timing. The original
TWO/TWO_OR_IMMEDIATE interval and 60-Hz vblank remain intact.

Capture the same position/action in missions 3 and 4, recording source-frame
CSV, CPU/GPU work and wait durations. Separate guest work, command decoding,
texture validation, GPU queue/readback and presentation before choosing the next
optimization. Keep the user's saves untouched; use disposable clones for QA.

## Native widescreen and DLSS

Neither is implemented by this checkpoint. Keep the launcher/F1 descriptions
accurate. Larger windows and spatial reconstruction do not change scene geometry.

The authenticated game has fixed 4:3 camera-aspect writes in the camera
constructor/update paths around `0x0006B78B` and `0x0006BA87`. Helpers
`0x0006AD00`/`0x0006AD90` derive horizontal/vertical frustum data from camera
fields. Other camera paths also use 4:3 constants. The original video-flags getter
`0x002F2AF6` shifts EEPROM video data by sixteen bits, whereas the pinned runtime
currently returns low-bit flags. Correcting that alone has not been demonstrated
to produce a wider view. Do not enable anamorphic stretching and label it native
widescreen. Verify a horizontal view expansion, matching culling, cinematics,
HUD aspect/layout and mouse/glyph coordinate transforms before exposing a mode.

Genuine temporal DLSS requires render-resolution color, depth, motion vectors,
camera/jitter/history/reset inputs and an output resource. See NVIDIA's
[Streamline DLSS integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md).
The current presenter receives completed CPU color only; scene depth remains in
the separate D3D11/11On12 renderer, and engine motion vectors/jitter/camera history
are not exposed. Export and validate that data before integrating/evaluating the
SDK. Existing image-motion interpolation and FSR1 are separate features. Preserve
non-NVIDIA fallback paths; no new mandatory GPU requirement was introduced.
