# Visual interpolation, startup fix and remaining milestones

## Player controls and timing

Launcher **Graphics > Visual interpolation** and **F1 > Renderer > Visual
interpolation** share Off / 60 / 120. Off is the default. The old native-60
option is retired: it changed the title's presentation waits, which does not
meet the user's requirement to retain original game updates. Both normal and
diagnostic interval overrides are removed. Original TWO/TWO_OR_IMMEDIATE
commands, 60-Hz vblank and guest timing/physics remain unchanged in all modes.

Interpolation operates on completed scene color images in Windows DX12
presentation. It accepts CPU, D3D11 and D3D12/11On12 scene images; it does not
require that the scene itself use D3D12. Direct D3D11/GDI presentation and Linux
SDL output retain original frames. Interpolation is not integrated on Android.
The D3D12 scene option still uses D3D11On12, not a separate native scene pipeline.

Version-1 preferences keep the legacy frame-limit field after sensitivity,
normalize its old 60 value to 30, and append an interpolation field (0/60/120).
Other valid old settings are retained. Missing interpolation means Off.

## What the implementation does

A shared framebuffer lock publishes each actual source flip's sequence and
monotonic timestamp with its image. Repeated window presents do not create new
source frames. The presenter keeps two GPU image histories, estimates forward
and backward block motion, then warps intermediate phases. At an original
30-FPS source, 60 mode has one intermediate phase and 120 mode has three.

It displays the previous/current pair one source frame late (about 33 ms at
30 FPS). This is additional visual latency, not reduced input latency. Motion
is estimated from final color, without engine motion vectors or depth. Low
confidence, inconsistent motion and occluded/out-of-bounds samples use current
source pixels; stationary pixels remain exact. Fast motion, disocclusion,
animated effects and HUD transitions can still show artifacts. This is an
experimental image-motion implementation, not AMD FSR 3 or NVIDIA DLSS.

F1, verified tutorial screens and recently observed native menus bypass
interpolation. Source gaps over 100 ms, skipped sequence IDs, invalid clocks,
resizes and mode changes reset history. It cannot make a slow CPU scene into
smooth gameplay. Existing FSR 1 spatial scaling can run after interpolation.
Original 4:3 scene resolution/projection remains unchanged.

The title reports GAME (source flips), DISPLAY (successful presents, including
repeats) and GEN (newly computed intermediate phases). GEN is not a count of
new simulation updates or a proof that every output pixel changed. Output is
limited by monitor refresh, source pacing and GPU time. For example, a 180-Hz
monitor repeats images even when the visual target is 120.

## Black Loading screen: diagnosis and correction

The paired disabled control stopped before the movies with GAME 0, GEN 0 and
no draws while the presenter repeated Loading at 180 Hz. Read-only sampling
found original sub_00265AD0 waiting at 0x00265B80/0x00265B87 through original
SleepEx(0) (0x002F4B0E, kernel KeDelayExecutionThread ordinal 99). The original
file object's byte at +5 remained busy. Its ReadFile/GetOverlappedResult path
uses original 0x002F33A0/0x002F5084 and completion 0x00265910, with the busy
byte cleared at original 0x00265992. These are evidence addresses for the
exact authenticated retail XBE, not portable host pointers or injected hooks.

Enabling the pinned public runtime's existing RECOMP_ASYNC_IO=1 mode on the
same build got through loading and into the mission. The native profile now
enables it by default, and the launcher passes it explicitly. The bridge still
performs its existing read, writes final IOSB and signals the supplied event;
its tracked asynchronous handles return STATUS_PENDING. No speculative physics,
resource-pointer, title callback or completion-port patch was applied.
This is a measured title/runtime compatibility profile, not a claim that every
Windows asynchronous read must always return pending.

Only the private hash-checked runtime copy is changed. Its environment parser
now treats an explicit RECOMP_ASYNC_IO=0 as disabled (upstream treated presence,
including string "0", as enabled). Use that only for direct diagnostic controls;
the launcher deliberately enables the working title profile. Successful read
payload logging remains opt-in via BF_TRACE_FILE_IO=1 because it can issue
millions of host writes and severely distort loading/performance.

## Evidence from 3 November 2026

All title runs used the user's existing core-game files with separate disposable
saves. They ended at bounded watchdogs, not native crash diagnostics. Scripted
movement/camera/fire and visible HUD do not establish complete manual gameplay.

| Scene / visual target | Observed result |
| --- | --- |
| D3D11 / 120, async disabled control | Loading stall; no source draws; display repeats only |
| D3D11 / 120, async enabled | First mission; late reports about 30 source + 90 generated phases/s |
| D3D12/11On12 / 60, native default profile | First mission, visible HUD; movement/camera/fire; ammo 35; about 30 source + 30 generated phases/s |
| CPU / 120, native default profile | Passed loading/reached mission; scene about 2-3 FPS; stale-history safeguard disables interpolation |

The display was 180 Hz on RTX 3060 Ti. These measurements are a first-mission
smoke test, not a 60/120 guarantee, complete physics comparison, all-level test
or subjective smoothness/artifact acceptance. The source keeps the game's
original waits; title cutscenes/loading may publish at different rates.
Private logs are under work/interpolation/interval-original-3i0p3nxa (stalled
control), interval-original-e3xgaior (D3D11/120), interval-original-v9iwi1b4
(D3D12/60), and interval-original-_uy4k0oj (CPU/120). Captures and game code
are not tracked. Arbitrary report-time BMPs can contain intermediate render
targets; native-window screenshots inspect the final presented output.

Focused verification includes:

- GPU readback reconstructs translated synthetic pixels at quarter, half and
  three-quarter phases (mean RGB error 0 in the measured test versus about 85
  for an unwarped endpoint), with stationary HUD, stall/reset and Off checks.
- API-neutral timeline checks cover 60/120 phases, gaps, skips and invalid clocks.
- Actual runtime flip-hold checks prove interval 2 is retained for every visual mode.
- Real patched file-bridge fixtures exercise pending versus synchronous status,
  completed data/IOSB, unsignalled-to-signalled event, exact stdcall cleanup and
  both explicit environment modes, using synthetic local files.
- Windows native tests, CPU/D3D11/D3D12 synthetic scene parity, Windows/Linux
  launcher lifecycle/discovery/ownership tests and Linux native checks pass.

Build the standalone GPU checks without private game data:

```powershell
cmake -S . -B .local/graphics-test -DBF_GRAPHICS_ONLY=ON
cmake --build .local/graphics-test --config Release
.local/graphics-test/Release/bf_interpolation_smoke.exe
.local/graphics-test/Release/bf_graphics_smoke.exe
```

BF_PRESENT_STATS=1 enables source/display/generated-rate logs. Optional read-only
BF_FRAME_TIMING_DIAGNOSTICS probes remain hash-gated and OFF in the normal
player. BF_TEST_FRAME_INTERVAL no longer changes timing. The original XBE and
section layout were checked with scripts/verify_offsets.py; no code injection
or allocated guest memory cave is used.

## Ordered remaining work

1. Broaden 60/120 visual testing: camera pans, motion boundaries, HUD/effects,
   cutscene transitions, latency, stable pacing on 60/120/144-Hz displays and
   slower GPUs. Direct DX11/GDI and non-Windows interpolation remain separate.
2. Add actual higher-resolution color/depth targets and coherent aliases, then
   16:9 projection with correct HUD/menu coordinates. Current FSR 1/window
   sizes only scale the finished 4:3 image. Temporal FSR/DLSS need scene depth,
   engine motion vectors, jitter/camera history, reset handling and HUD handling;
   they are not implemented by this image-motion prototype. See AMD's official
   [frame-interpolation inputs](https://gpuopen.com/manuals/fidelityfx_sdk/reference_documentation/structs/ffx_frame_interpolation_dispatch_description/).
3. Connect NV2A draws to a Linux GPU backend. Android additionally needs runtime,
   SDL/activity lifecycle, storage import, input, audio, ARM64 linking/packaging
   and actual APK testing on the Canadian Galaxy S24 Ultra. NDK r30 is installed;
   existing NDK/object audits are not an APK.
4. Validate every campaign mission/multiplayer map, all characters/abilities,
   audio transitions, physical controllers, hotplug and shutdown. Preserve
   permanent saves and use disposable QA progress. The invasive upstream USB
   lifecycle overhaul remains unapplied pending real-device testing.
5. Implement authenticated runtime cheats and live F1 controls, then real-peer
   host/join/disconnect/rejoin. Raising player limits requires auditing original
   fixed arrays, replication, UI, allocation and mission scripts first.

No expanded player limit, P2P implementation, cheat suite, Linux GPU, Android
APK, temporal reconstruction or native widescreen is claimed here.
