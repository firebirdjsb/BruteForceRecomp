# Next-agent handoff prompt — 3 November 2026

Continue the native Brute Force recompilation in
`D:\Documents\ChatGPT\bfRecomp` (Windows/PowerShell). Read the user's AGENTS
instructions, current source, STATUS.md and docs/FRAME_RATE.md before editing.
The user authorizes pushing source to private firebirdjsb/BruteForceRecomp.
Do not commit game files, generated/recovered game C, captures, saves or binaries.
Keep original saves and permanent preferences. Do not reintroduce a website.
Use at most two focused repair loops per issue and report remaining failures.

## Latest requirement and correction

The game must retain its original 30-FPS timing. The user rejected the previous
native-60 experiment and wants interpolated 60–120 visual FPS. They also reported
black Loading (GAME 0, GEN 0, draws 0 despite DISPLAY 180) and want compatibility
across renderers. Playable campaign remains priority 1.

The former interval-2-to-1 override is removed from both normal and diagnostic
paths. src/gpu/frame_interval.h and its test are retired. Actual runtime
flip-hold tests now prove the original TWO_OR_IMMEDIATE interval is retained in
Off/60/120 visual modes. Legacy native-60 preferences migrate to 30 while
preserving other settings. Do not change guest clocks, physics or vblank.

## Latest startup fix and evidence

A disabled control run stalled at original 0x00265B80/0x00265B87 in
sub_00265AD0's file-completion busy loop, repeatedly calling original
SleepEx(0) at 0x002F4B0E (kernel ordinal 99 KeDelayExecutionThread). The
original file object byte +5 stayed busy. ReadFile/GetOverlappedResult wrappers
are 0x002F33A0/0x002F5084; completion 0x00265910 clears it at 0x00265992.
These evidence addresses belong only to the authenticated retail XBE.

The pinned public bridge already has RECOMP_ASYNC_IO=1: existing read, completed
IOSB/event, but STATUS_PENDING for tracked asynchronous handles. A paired run
with that mode got through loading into the first mission. src/main.cpp now
defaults it on; native_launcher/process.cpp passes it explicitly. A hash-gated
private-copy edit fixes upstream's presence-only parser so explicit 0 works for
direct diagnostic controls. No speculative physics, guest callback or
completion-port implementation was added. Do not overstate Windows API rules;
synchronous completion of an asynchronous request can be legal. This is a
measured title/runtime compatibility profile.

New tests/async_io_test.c drives the actual patched NtCreateFile/NtReadFile
bridge with synthetic local data, both async/synchronous handles, 0/1 profile,
final IOSB/data, event transitions, handle cleanup and exact stdcall stack.
Public-runtime-only CI links actual host UI code, not presenter function stubs.
BF_TRACE_FILE_IO can log millions of tiny reads; leave it OFF during normal QA.

## Visual interpolation implementation

- src/graphics/interpolation_timeline.h: source sequence/timestamp history and
  2x/4x phases, one source-frame latency, resets on skips/stalls/invalid clocks.
- frame_interpolation.{h,cpp} and interpolation.hlsl: real DX12 bidirectional
  image block matching plus motion warping, confidence fallback and stationary
  pixel preservation. This is experimental image motion, NOT FSR 3 or DLSS.
- cmake/frame_handoff.cmake publishes timestamps/IDs with locked framebuffer
  copies. Repeat window presents do not advance source sequence.
- dx12_presenter/present_dispatch run interpolation before existing spatial FSR1.
  F1, verified tutorials and recently observed native menus bypass it.
- Launcher Graphics and F1 > Renderer share Visual interpolation Off/60/120.
  Off is default. Direct DX11/GDI presentation retains original frames; the
  feature currently needs DX12 presentation regardless of scene renderer.
- Title GAME = source flips, DISPLAY = presents including repeats, GEN = new
  computed phases (not physics updates or proof all output pixels changed).
  BF_PRESENT_STATS=1 logs these separately. Monitor refresh limits output.

Real GPU readback reconstructs translated quarter/half/three-quarter synthetic
frames with measured mean error 0 versus about 85 for unwarped pixels; stationary
HUD, Off, skipped frames and stalls pass. Hardware test is not artifact acceptance.
Fast motion, animated FX, occlusion/HUD transitions and one-frame visual latency
remain risks. Higher internal resolution/native 16:9/temporal upscalers are not
implemented by interpolation.

Bounded visible runs with separate scratch saves:

- work/interpolation/interval-original-3i0p3nxa: disabled control, startup stall.
- interval-original-e3xgaior: D3D11 scene/120; first mission, scripted input;
  late reports about 30 source + 90 generated phases per second.
- interval-original-v9iwi1b4: D3D12/11On12 scene/60, default async profile;
  first mission, HUD, movement/camera/fire, ammo35; about 30 source +30 generated.
- interval-original-_uy4k0oj: CPU scene/120; reached mission but scene2–3FPS;
  stale-history safeguard correctly stopped interpolation. No performance claim.

Monitor was 180 Hz / RTX3060Ti, so successful presents include repeated pictures.
Runs ended at their bounded watchdogs without native/invalid-code failures.
Manual mission completion, all-level/physics/audio/controller acceptance remains
open. Inspect final native window output; sparse report-time BMPs can contain
intermediate targets and are not all presented pictures.

## Build, installation and checks

Normal player: work/diagnostic-build/Release/bruteforce_recomp.exe.
Launcher: workspace-root BruteForceLauncher.exe plus SDL2.dll/assets; legacy copy
under work/native-launcher/build/native_launcher/Release is also kept current.
Use Launch Brute Force.cmd. Latest interpolation candidates/build logs and
install backups/manifest are under work/interpolation. Check actual installed
hashes and current processes before replacing anything; never kill user gameplay.
BF_HOST_OUTPUT_DIR currently selects work/interpolation/player for isolated
builds. Launcher candidate output is work/interpolation/launcher.

24 Windows native tests, both public GPU scene parity suites, all3 launcher
tests on Windows/Linux, 17 Linux native tests, and 70 Python tests (3skipped)
passed in this work. Standalone bf_interpolation_smoke and bf_graphics_smoke
pass on hardware with D3D12 debug validation. GitHub checks should be verified
against the exact pushed commit; local passes are not remote CI evidence.

Pinned clean toolkit:
C:\Users\fireb\Documents\Codex\2026-10-31\le\work\xboxrecomp
commit f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b. Never modify that checkout.
Private generated C is sibling bf-analysis/generated; recovered bodies are
work/recovered. Supported default.xbe SHA256:
B6EA5D4DD6500AE86C3F5E3E4618BC9B1E3FACDDCB483170DFEE7A787087C721.
python scripts/verify_offsets.py --target <default.xbe> verifies its XBE layout,
not host PE offsets or runtime equivalence. No code injection/caves are used.

WSL distro BruteForceBuild; source /mnt/d/Documents/ChatGPT/bfRecomp;
/var/tmp/bruteforce-build and /var/tmp/bf-native-launcher hold Linux builds.
Linux game remains CPU/SDL only. NDK r30 is installed under work/tools, but
Android CMake intentionally rejects unsupported integration; there is no APK.

Default interactive library is
C:\Users\fireb\AppData\Roaming\Firebird\BruteForceRecomp.
Another QA library is work/native-launcher/core-qa. Both have permanent
player-data: never reset or overwrite it. Tests use disposable scratch saves.
Launcher defaults currently select D3D12 scene / auto presentation / FSR1;
preserve user volume, sensitivity and other preferences.

## Existing working fixes to preserve

CPU/Bink color ownership (black movies), zero-depth HUD compatibility,
asynchronous zpass completion, real FIFO DMA CALL/JUMP/RETURN/fences,
original 1-MiB guest stack, native MKB/raw mouse, full authored mip chains,
and confirmed depth/stencil alias bloom-mask synchronization. The additive
clip heuristic was rejected because it removed valid tutorial text; do not
reintroduce it. Basic Controls artwork switches with input device. F1 is the
only settings/capture hotkey. User confirmed visible input/bloom candidate on
2 November. Remote AnyDesk requires Gaming Mode for relative mouse aiming.

Launcher hides throughout validation/startup/game and restores on normal/error
exit. A per-library OS lock prevents duplicate owners across installed paths;
reopening while game runs does not expose the hidden launcher. Lifecycle tests
cover actual children, failures, HWND visibility, stale metadata and abrupt exit.
Keep the finite-ancestor player-discovery fix that resolved gray Start.

## Ordered remaining work

1. Interpolation artifact/latency/pacing QA across hardware/refresh rates and
   HUD/FX/cuts; direct DX11/GDI/non-Windows generation remains separate.
2. True internal-resolution color/depth/alias integration + native16:9 and HUD;
   temporal FSR/DLSS with valid scene motion/depth/jitter/history.
3. Linux NV2A GPU integration and native ARM64 Android lifecycle/package/APK;
   target Canadian Samsung Galaxy S24 Ultra. Object audits are not an APK.
4. Complete every mission/multiplayer map, character/ability, crisp audio and
   real controller/hotplug/shutdown matrix. USB overhaul remains unapplied.
5. Authenticated live cheats and real peer host/join lifecycle; audit original
   fixed arrays, replication/UI/mission assumptions before raising player limits.

No production service, original disc assets or permanent progress was changed.
