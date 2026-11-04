# Current state - 4 November 2026

This remains an experimental native recompilation. It is not a completed
Windows/Linux/Android release.

## Latest player fixes and handoff

### HUD artwork, prompt labels and pause-menu coordinates - 4 November

AA's font boundary was too late for native HUD images. An additional ordered
boundary now runs before the authenticated original image primitive 0xBA950.
The CPU/D3D11/D3D12 test covers a HUD panel drawn before text: all 384 panel
pixels and 256 text pixels remain identical with AA On/Off, while scene edges
change. Full live HUD/FX coverage still needs player acceptance.

Gameplay HUD C/D glyph labels now show R/MMB in the corrected order; physical
bindings and the accepted raw mouse path remain unchanged. English literal
Start-button instructions display ESC. Trigger instructions describing weapon
fire display LMB, including the left-trigger firing variant confirmed by the
user; inventory/throw instructions display G. Rendering uses a bounded temporary
guest-stack argument/string copy, keeping original fonts and stored strings.
Unknown/localized strings and controller input retain original behavior.

The pause view overrides base GetPosition with 0x1012E0, which selects the
current player's viewport/safe area and centers the panel. The former local
coordinate sum missed this origin for keycaps and hit testing. The adapter now
calls that original getter with register/stack preservation. Aiming values
also accept mouse focus and normal Right input to cycle their original options.
A disposable four-minute D3D12 campaign run reached the actual pause widgets,
hovered Restart, returned to Resume and dispatched Resume clicks. It ended at
the watchdog without a guest-call failure. Framebuffer captures from that
headless run were solid-color and are not visual acceptance evidence.

Both player links and all 32 Windows / 21 Linux checks pass. The updated player
is installed at the canonical and previous-prototype paths; content validation
passes, and all 64 permanent save/settings files checked remained byte-identical.
No original game data or production service changed. See the handoff for the
new hash, backup and remaining manual mouse/HUD checks. Earlier counts and
installation notes below describe prior checkpoints.

### Scene-only AA and keyboard prompt checkpoint

The earlier uncommitted AA prototype filtered completed frames and softened
game text. That path has been removed. AA now runs at an ordered NV2A boundary
before original font/UI-quad commands, once per render surface and frame.
CPU color and Windows D3D11/D3D12-through-11On12 scenes share the boundary;
CPU 16-bit surfaces are left unchanged. Launcher Graphics and F1 > Renderer
share the optional AA preference. This is spatial edge AA, not temporal AA,
DLSS or a higher internal scene resolution.

A real NV2A synthetic draw test changes 123 CPU / 127 GPU scene-edge pixels
while keeping all 256 subsequently drawn UI pixels byte-identical to AA Off,
including repeated UI boundaries. A disposable D3D12 game run reached the
Basic Controls view and logged scene-before-UI execution. It ended at the
watchdog, without verifying pause-menu clicks, complete gameplay or every HUD
layer. The corrected player was installed on 4 November after the user closed the old
prototype; see the installation correction below.

Prompt labels now share the actual primary desktop/menu key tables. The
authenticated gameplay controller-font draw can replace inline Xbox X with R
on Windows desktop input, with controller fallback and ordinary text preserved.
Eight identified non-basic tutorials continue through the original input path
only for keyboard/mouse; Basic Controls retains the mouse guide. These changes
pass synthetic tests. Full in-game glyph placement, longer keycap fit and squad
command artwork/text replacement remain open. Pause-menu diagnostics are added;
the reported click failure is not yet fixed.

Current checkpoint validation: full Windows/Linux player links; 32 Windows
and 21 Linux native checks; four focused public-runtime checks including both
GPU scene-parity suites and AA UI preservation; four Windows launcher checks;
17 Python unittest cases (one skipped) and 59 pytest cases. The exact evidence,
local build paths, rollback notes and continuation order are in
[NEXT_AGENT_HANDOFF.md](docs/NEXT_AGENT_HANDOFF.md). Previous checkpoint test
counts and installation statements below describe their historical builds.

### AA installation correction - 4 November

The new AA screenshots came from the obsolete full-frame-AA executable selected
by launcher.settings, not the scene-only source checkpoint fd27458 (whose CI
passed). After the user closed both apps, the scene-only build replaced the
canonical player and the old prototype path, both installed launchers/DLLs were
updated, and the launch target was corrected. F1 now says `AA: SCENE ONLY / TEXT
EXCLUDED`. Native content validation passed; 63 permanent save/settings files
retained identical hashes. AA remains Off as the user left it. Fresh AA-On/Off
screenshots still need runtime acceptance. Backup/build hashes and rollback are
recorded in the handoff. No production service or original game data was touched.

### Accepted raw mouse and new crash repair

The latest screenshot/log stops at unresolved **0x0002E490**, reached from the
original script-member dispatch at 0x00204159. Its omitted aligned-stack entry
is now recovered from the exact authenticated 787-byte body through the original
RET at 0x0002E7A2; all direct callees were already translated. The recovery test
executes a neutral script path at both stack alignments and checks output,
calls, arguments, callee-saved registers and stack cleanup on Windows/Linux.
The captured log is preserved privately; the original long combat sequence has
not been replayed to completion. This repair does not establish crash-free play.

The earlier post-clamp mouse callback still passed through a second binding
clamp and the camera's analog response. Windows mouse input now reaches the
camera after the curve/ramp/aim-assistance stages, with no virtual right-stick
report. Dynamic ownership includes the original virtual-pad proxy to physical
port0 chain. Live traces show valid ownership and actual relative counts.
The user confirmed “this is the fix” and “this feels good.” The exact accepted
candidate is retained under work/crash-mouse-fix/verified-player. See
[RAW_MOUSE.md](docs/RAW_MOUSE.md) for implementation, tests and latency limits.

The framebuffer thread now retains its handle and is stopped/joined before
guest-memory teardown and presenter static destruction. Internal window
destruction during that shutdown does not terminate the process prematurely.
Its regression test checks join-before-unmap ordering, duplicate starts, restart
rejection and handle release. This addresses the identified shutdown race;
the exact fatal gameplay/shutdown sequence still needs a runtime replay before
claiming the reported DX12 command-list error can no longer occur.

Current local validation: 29 Windows native checks, 18 Linux checks, 17 Python
unittest checks (one skipped) and 59 frame/timing pytest cases passed. A
four-minute D3D12 campaign run reached HUD/movement/fire without a guest failure;
that scripted run did not initialize the real mouse adapter. A separate real
mouse run exercised the accepted proxy ownership path. Permanent saves and
preferences remain outside disposable test profiles. Broader mission, multiplayer,
audio, Linux GPU, Android, native widescreen and temporal-upscaler work remains open.

### Combat, mouse, texture and DLC checkpoint

The captured combat crash stopped at unresolved guest target **0x0001D8F0**,
after running and killing enemies. Original authenticated bytes identify an
eight-byte vtable tail thunk, not a physics routine. Recovery now translates
that exact body with the pinned translator and registers its original address.
A fixture executes the recovered body and checks dispatch, stack cleanup and
preserved registers on Windows and Linux. A ten-minute first-mission scripted
run ended at its watchdog without a guest-call failure; the user's exact enemy
kill sequence has not been reproduced, so complete combat acceptance is pending.

The original controller update clamped right-stick values after the former
mouse callback. Native mouse aim now applies after that original clamp, using
validated controller/state memory. The test executes the real translated
controller update: an original 4/-3 input clamps to 1/-1, while native mouse aim
retains 4/-3; other ports, invalid/read-only memory and stack cleanup are checked.
Real mouse feel and end-to-end input latency still need player testing.

GPU sampled textures can now use optional 2x/4x bounded cubic reconstruction.
Original mip images are retained below the added levels, with a 256-MiB extra
cache and original-resource fallback. Eligible HUD/color/cube textures are
included; movie, depth and render-target aliases are excluded. Launcher Graphics
and F1 > Renderer share the preference. Real GPU readback passed RGBA/BGRA,
non-square/cube, alpha and exact-original-mip cases on D3D11 and D3D11On12,
with WARP and hardware. Native UI inspection confirmed the live 2x-to-4x control
and rendered mission/HUD. This is not an HD asset pack or higher scene resolution.

The supplied BruteForceDLC.zip contains three packs and 51 data files. Local
launcher packaging now installs them automatically into the original title's
TDATA/4d53001e/$c directory, authenticates all sizes/SHA-256 hashes, stages new
packages before publishing, and preserves existing differing content and saves.
The actual launcher installed and verified the three packs in a scratch library;
the game opened all three ContentMeta.xbx files, then reached first-mission
gameplay with HUD and scripted movement/fire. DLC map selection/playthrough is
not yet verified. Bundled Xbox installers and regional title-update XBEs are
excluded: their entry point/code layout differs from the supported retail image.
See [DLC.md](docs/DLC.md).

The startup issue is **intermittent, not completely fixed**: one DLC-seeded
D3D12 run stalled before movies at the previously observed file-completion
loop even with the async profile enabled. A D3D11 retry and a subsequent
D3D12 run progressed. Do not attribute that stall to DLC or claim reliable boot
from these runs. A separate DX12 interpolation command-list error after runtime
shutdown remains to investigate; it follows the combat failure in the captured
log and is not its original cause.

Checkpoint validation: 28 Windows native tests, 18 Linux native tests, four
launcher tests on each platform, 17 Python unittest checks (one skipped), and
59 frame/timing pytest cases passed. Installer tests cover corruption, duplicate
inventory, traversal, cancellation after staging, link rejection where permitted,
idempotence and save preservation. Build/fixture evidence does not validate all
missions, DLC maps, audio, controllers, multiplayer or visual quality.

### Launcher visibility and duplicate-instance recovery

Three interactive launchers were left open from earlier testing, with two
different executable paths pointing at the same library. Only the instance
that started the game hid its window; the other windows remained visible.
The old main loop also waited until game-process creation before hiding,
leaving the launcher visible during validation and preflight.

The launcher now hides for the whole launch session, beginning at Start,
and restores after normal exit, a nonzero child exit, validation failure or
process-start failure. Verification/import operations on their own stay visible.
An OS file lock allows one interactive owner per library across executable
copies. Duplicate Windows starts activate a visible owner but do not reveal
an owner hidden for the game. Process exit releases ownership, including an
abrupt exit that bypasses C++ destructors. The lock contains only a local PID;
no game data or save schema changes are involved.

All three launcher tests pass on Windows and Linux. Coverage includes
startup/failure visibility, actual child exit behavior, duplicate processes,
independent libraries, stale PID metadata and abrupt-exit lock release.
A separate run with SDL's Windows driver additionally checked real HWND
visibility with IsWindowVisible. The three old launcher windows were closed
normally for installation. Opening the upgraded legacy executable while the
root launcher was open left only that same root process/window alive, verifying
the duplicate guard across installed paths. This follow-up changes launcher behavior only;
it does not change game code, rendering, performance or mission validation.

### Loading stall and original-timing visual interpolation

The reported black Loading screen showed GAME 0, GEN 0 and no draws despite
180 display presents/second. A paired run traced the stall to the original
pack-reader completion loop, not to GPU rendering or physics. Its asynchronous
file object stayed busy while the main thread repeatedly executed SleepEx(0).
Enabling the pinned runtime's existing RECOMP_ASYNC_IO mode got the same player
through loading and into the first mission. This is now the native title
profile's default and is passed explicitly by the launcher. Direct diagnostic
runs can set RECOMP_ASYNC_IO=0; the hash-gated private bridge now parses 0 as
disabled. The public toolkit checkout is unchanged.

The former native-60 interval override is removed, including the diagnostic
BF_TEST_FRAME_INTERVAL override. Legacy 60 preferences migrate to original 30
without discarding other settings. A separate Off/60/120 visual interpolation
control is in launcher Graphics and F1 > Renderer. It performs GPU image-motion
estimation and warping after the completed scene, with one source-frame delay;
it does not change guest timing, vblank, physics, input or simulation updates.
This experimental implementation is separate from temporal FSR/DLSS.

D3D11 and D3D12/11On12 scene runs reached the first mission with HUD, scripted
movement/camera input and ammo reduced to 35 after firing. Representative late
reports measured 30 source + 90 generated phases/s for the D3D11 scene at 120
visuals, and 30 + 30 for D3D12 at 60 visuals on RTX 3060 Ti. The 180-Hz display
also repeats pictures; its present count is not the game's update rate. This
is bounded first-mission evidence, not complete campaign/physics/audio QA.
The CPU scene reached the mission but fell to about 2-3 source FPS; the history
safeguard then disabled interpolation. It is not a viable high-FPS game renderer.

Interpolation currently requires Windows DX12 presentation, independently of
CPU/D3D11/D3D12 scene selection. Direct DX11/GDI output retains original frames.
F1 and recognized menus bypass interpolation; missed frames, long stalls,
resizes, mode changes and invalid timestamps reset history. Fast motion and
occlusion can still produce artifacts or fall back to original pixels. Off
remains the default. See [frame-rate evidence](docs/FRAME_RATE.md).

### Launcher Start recovery

The normal SDL application path ends in a separator. The launcher's fixed
six-parent search spent one step on that empty final component and stopped
before reaching this checkout's native player. The default AppData library
therefore had verified game files but an empty executable setting, leaving
Start disabled. Player discovery now walks the finite ancestor chain, and
startup retries discovery when a persisted executable no longer exists.

Missing application paths now have an explicit status and Locate action;
disc verification no longer claims the player is ready independently of its
presence. A synthetic native-discovery regression reproduced the old failure
and passes after the fix, along with the launcher lifecycle test on Windows
and Linux. Game code, original files and permanent saves are unchanged.

### Conditional Basic Controls artwork — user-confirmed native candidate

The English Basic Controls lesson now replaces its original controller diagram,
thumbstick icons and ten instructional labels with authored WASD/mouse artwork
when keyboard/mouse is active. It shows camera-relative movement, mouse look,
LMB fire, RMB zoom, G item/grenade and F1 sensitivity/settings. Physical controller
activity restores the original artwork. Original title, panel chrome and
Accept/Skip objects and callbacks are preserved.

Recognition requires the live managed view, exact lesson title, both thumbstick
instruction substrings and the observed image/text hierarchy. Only the fifteen
verified art/text nodes have their four-byte visibility flags masked during the
original synchronous Draw; flags are restored immediately. String pointers are
bounded to guest RAM, and flag writes reject inaccessible/protected regions.
Other tutorials and localized text retain original visuals. This covers the
observed Basic Controls page; it does not establish replacement of every lesson.

The new integration fixture checks original flag restoration, input-device
switching, original Accept/Skip reports, unrelated/hidden/unmanaged lessons,
invalid string pointers and a flag spanning writable/read-only pages. It and
the existing host UI test pass. A visible title run showed the replacement in
the original panel, with the original heading/footer and no clipping. Clicking
Accept resumed gameplay. A second, larger authored alphabet makes W/M distinct;
its final preview and focused tests pass. On 2 November the user reported
"everything seems to be working" and explicitly authorized pushing to main.
That confirms the visible gameplay candidate; the final font readability
adjustment additionally passed focused UI tests and visual preview inspection.

### Brightness/blur source found — user-confirmed depth/stencil alias fix

The supplied recording was inspected as 41 frames at 2 fps. HUD/menu sharpness
stays stable while the world alternates between darker rendering and a broad
bright blurred wash. A per-draw trace found the final additive composite uses
a 160x120 blurred image derived from a 320x240 luminance-mask pass.

That mask samples the live Z24S8 allocation as linear R8G8B8A8 (format 0x41),
reading stencil through alpha. The baseline bound stale guest memory instead
of GPU depth/stencil: the captured GPU stencil was 255 at all 307,200 pixels,
but the actual sampled mask alpha was zero everywhere. This reverses the
shader's luminance rejection and passes the whole scene into bloom.

Exact initialized linear D24S8/0x41 aliases now resolve GPU data back into Xbox
byte order before the existing decoder. Both depth and stencil must have been
initialized; incompatible/uninitialized aliases retain the existing fallback.
The general experimental 0x2E depth mode is unchanged. This is a synchronization
fix, with no bloom-disable, darkening gain or camera-angle heuristic.

Six depth-alias fixtures and eight color-ownership fixtures pass on D3D11 and
D3D11On12, WARP and the RX 7600. Baseline positive depth cases failed before the
fix. Movie/mip/visibility regressions pass; nine existing 11On12 hardware images
are byte-identical to baseline. Its pre-existing CPU-reference edge mismatch
remains, with unchanged tolerance.

In the matching real-title scene, the traced foliage pixel changed from
(255,255,69) to (99,86,8), matching its original pre-composite value. The bright
blurred wash and clipped yellow halos were absent there. Visible title QA also
reached and dismissed Basic Controls. The user subsequently confirmed the
candidate was working and authorized the push. Complete camera/mission/effects
coverage remains open; this does not establish full campaign validation.
Private evidence is under `.local/flicker-qa/traced-mask-v6` and
`.local/flicker-qa/tutorial-visible-qa`. All 20 full-build native CTests passed;
the final font-only update passed both focused UI tests. The final executable
is installed at the workspace root with a private rollback copy of its predecessor.

### F1-only controls and immediate mouse input — user-confirmed candidate

The user requires all in-game settings under F1. F2/F3/F4 shortcuts and the raw
F9 capture toggle are removed. F1 > Controls has a draggable logarithmic
0.1x–10x sensitivity slider and keyboard/mouse guide. F1/menu ownership, focus
loss and minimization release capture automatically. The launcher shares gain.

Authenticated retail input code 0x001F3250 applies an adaptive stick deadzone
starting at 0.24 and then a radial filter. The local Windows candidate supplies
raw mouse deltas after those filters at successful port-0 update 0x001F33D4.
The constructor at 0x001E7450 establishes port index at device+0xBC; the original
0x24-byte temporary input report begins at ESP+4 and processed right X/Y are
report+0x1C/+0x20. The generated chunk is SHA-256 gated. Separate raw-motion
accumulators avoid losing deltas to USB polls; gain is normalized to elapsed
time and can exceed one stick throw. Other device activity, typing, F1 and
foreground checks preserve ownership. No camera/physics memory offsets or
simulation timing changed. The user confirmed the visible candidate was working
before authorizing the push to main.

Focused input/cursor/UI tests and all 19 native CTests pass; the native launcher
lifecycle passes. Slider drag/release, endpoint selection, old preferences,
physical-device ownership and removal of extra function-key behavior are covered.
The authored F1 preview is `.local/f1-controls.bmp`. Original disc data and
permanent saves are untouched. The user authorized publishing these source changes.

The user supplied a matching tutorial capture and clarified that the defect is
excess brightness mixed with blur across the image, varying with camera angle.
In the renewed bounded cycle, enabling the existing D24 texture path produced
valid depth aliases but retained the glow. A depth-test-aware near-plane
experiment also retained it in matched A/B/A captures and was reverted. Both
diagnostics stayed private; neither is in the normal executable. Their captures
are in `.local/flicker-qa/depth-on` and `depth-near`. Stop repeating near-plane
and depth-enable repairs. Subsequent per-draw tracing isolated the stale
depth/stencil mask described above. The earlier color-feedback hypothesis did
not explain the real-title glow, although its independent ownership fixtures
identified and verified separate renderer correctness fixes.

Native 16:9 and remaining tutorial pages remain open. Static analysis has
identified camera FOV/aspect setters 0x0006E450/0x0006E4A0, frustum construction
0x0006C840, UI quad emission 0x000EE6E0 and glyph emission 0x000CCD20. Any native
mode needs matched world/frustum, HUD/text proportions, hit testing and movie
captures; no aspect toggle has been shipped without those checks.

DLSS and later FSR support are now requested too. The current machine reports
AMD Radeon RX 7600, driver 32.0.32015.7029. Existing spatial FSR 1 receives color
only. Temporal upscalers need correctly owned depth, object/camera motion
vectors, jitter and history/reset data before integration and runtime QA.
Official references: [AMD FSR SDK](https://gpuopen.com/amd-fsr-sdk/) and
[NVIDIA DLSS integration](https://developer.nvidia.com/blog/how-to-integrate-nvidia-dlss-4-into-your-game-with-nvidia-streamline/).
No temporal SDK or placeholder motion-vector path has been presented as working.

### User-confirmed desktop controls and authored mip sampling

User follow-up: WASD and left-click firing pass. Mouse aim and cursor hiding
also pass when AnyDesk **Gaming Mode** is enabled. Standard AnyDesk control
did not provide working aim; the temporary desktop-pointer fallback was
discarded after this confirmation. Preserve the tested raw-input/cursor path.
RMB previously reached the grenade/inventory trigger. It now reaches right-stick
click for the game's zoom action; G reaches the grenade/inventory trigger.
The user confirmed the final installed build passes right-click zoom, G item use,
movement, aim, fire and cursor capture with AnyDesk Gaming Mode enabled.

Single-axis WASD previously sent 1/100 of the intended movement into mouse
deadzone compensation, while diagonals sent about 70/100 on each axis. Digital
movement now uses full signed 16-bit axes with normalized diagonals and opposing
keys cancel. Movement is independent of mouse sensitivity. Raw mouse button
state is read without consuming it on each USB poll. Mouse motion is accumulated
and held over 60-Hz sampling windows, normalized by elapsed time. F1/menu/text
ownership, F9 release and foreground gates reset pending motion/button state.

The native launcher and F2 controls page expose 0.25x–5x sensitivity. Older
settings files retain their existing preferences and default to 1x. F2 includes
an authored WASD/mouse diagram; original tutorial textures are **not** replaced.
Direct camera input bypassing the game's analog turn-speed limit is unfinished.
Cursor hiding now runs on the framebuffer window thread through WM_SETCURSOR
and its presentation loop. The pointer is confined to the client rectangle,
not the title bar, and released for overlays, menus, F9, focus loss and minimize.
The USB poller no longer changes another thread's ShowCursor counter.

The scene backend uploads authored mip levels for 2D/cube textures, validates
the full chain, decodes levels at their own dimensions, snapshots lower levels
for cache invalidation, and maps independent MIN/MAG/mip filters and LOD bias.
No physics, timing, saved progress, or original disc files were changed.
The user still sees intermittent bright flicker throughout the map at certain
camera angles, both while turning in place and while moving.
Two focused rendering attempts have reached their limit: authored mip sampling
did not resolve the reported flicker, and the reviewed additive-clipping
experiment failed real-title QA despite passing synthetic fixtures. A second
isolated run reached the tutorial with a visible HUD. An A/B/A change of only
the renderer's diagnostic policy removed valid tutorial text in B and restored
it in A2. The original policy was restored and the experiment was removed from
the player/source candidate. Private captures are under
`.local/flicker-qa/window-baseline/near-{A,B,A2}.bmp`; no assets are committed.
The play log also reports unsupported 0x2E depth textures. This is a lead, not a
proven flicker cause. The two-repair limit has been reached. The safest next step
is to capture a reproducible affected camera angle and matched D3D11/D3D12
evidence before another repair cycle; do not declare the flicker fixed.

Local checks: 19 native CTests and launcher lifecycle pass. D3D11 hardware and
D3D11/D3D12 WARP draw comparisons pass. Authored ARGB/P8 mip selection, lower-mip
rewrite invalidation and level-zero filtering pass on the AMD RX 7600 through
both scene APIs. D3D12 hardware still fails the existing CPU-reference image
tolerance: all 12 shared baseline/candidate captures are byte-for-byte identical,
so the mismatch predates this change. The tolerance was not widened. A 35-second
isolated D3D12 boot captured visible movie frames and ended at its configured
watchdog; this isolated run is not a campaign or physical-input pass. The separate
user control smoke test passed on 2 November 2026, satisfying the user's condition
for pushing these source changes to main. The installed player SHA-256 is
`F18E88970073BE98072BCAFDFF329681E21FA4F81D11A8154A98A78130F0F12D`.

Native 16:9 is also requested and remains unfinished. Static inspection of the
authenticated XBE found the game's video-query wrapper reads the upper half of
XC_VIDEO, whereas the pinned runtime advertises low-bit flags; merely toggling
that report is insufficient because camera initialization/update still writes
4:3 constants. The camera viewport/frustum and HUD need matched 4:3/16:9 title
captures before enabling a native mode. No stretched-frame toggle is presented
as native widescreen, and no camera/physics offsets were guessed or patched.

The launch black screen was stale GPU color data overwriting CPU-decoded Bink
frames. Guest/GPU surface ownership is now synchronized; a visible warm-cache
test produced 42 nonblack captures and the user confirmed movies/menus visible.
The apparent mission freeze was an invisible Basic Controls tutorial dialog:
zero-depth programmable UI compatibility restores it and the HUD. The launcher
now enables the raw-mouse adapter with keyboard controls. Full physical input
and mission-progression QA remain open.

D3D12 scene rendering is available through an explicitly labeled D3D11On12
bridge, with a real D3D12 device/queue/fence. It is not a native DX12 scene
pipeline. D3D11/D3D12 synthetic WARP and RTX 3060 Ti tests cover actual draws,
CPU-written movies, zero-depth HUD, GPU programs and async visibility. Windows
passes 17 CTests; Linux passes 14; both native launcher lifecycle tests pass.

Asynchronous visibility is now the GPU default after a bounded 178-second
world/HUD test measured late reports of 29.99–30.01 presented frames/s without
blocking visibility reads. `RECOMP_GPU_ZPASS=sync` retains the diagnostic path.
This is not an all-mission minimum. A longer sync run produced late blue frames;
that defect still needs equivalent-state testing. Coarse textures, excessive
brightness and complete gameplay/audio validation remain unfinished.

See [the self-contained next-agent prompt](docs/NEXT_AGENT_HANDOFF.md) for the
full source map, local applications, private evidence, rollback and next steps.

## Campaign rendering checkpoint

The tutorial now loads and renders Tex, terrain, vegetation and sky after the intros. The original GPU software methods were missing: operation 5 signals the D3D fence event; operations 3/4 apply the original reusable-pushbuffer fixup records and return jump; operation 10 updates the original byte accounting and continuation. The DMA walker now shares the actual CALL-state register with those operations. DMA walking is the default, with the CPU renderer retained. Original physics and simulation timing were not changed.

The 238-second `work/gameplay-priority/fixups-live-9a0natba/` run and 298-second visible `campaign-window-yf3rily3/` run reached their watchdog limits without the earlier command corruption/native crash. `gameplay-controls-oskkh3yh/frame.bmp00054.bmp` and `frame.bmp00067.bmp` show Tex moving forward in response to the runtime controller-report fixture. This establishes tutorial rendering and a movement path, not mission completion or physical-device QA. The visible run also exercised camera, jump and fire reports; their complete effects need further inspection. Nonzero game PCM was captured, but host audio reported starvation/underruns. The visible scene fell to about 3–4 presented frames/s. Solid 30 FPS, complete effects, audio quality, all levels and characters remain unverified.

The CPU campaign candidate is `work/diagnostic-build/campaign-test/Release/bruteforce_recomp.exe`, SHA-256 `C5BB4D4121CDE7F7EC1ADDE37F5AB41738D426FCA1BB97DA73314C14EA5333DB`. The user's latest code-21 screenshot came from the obsolete normal player with an 8-MB main stack, not this candidate. Its session log confirmed the same tutorial allocation failure. After that process exited naturally, the normal launcher player was updated to this candidate, with the old file backed up under `work/gpu-integration/rollback-old-launcher-player/`. Permanent saves and original assets were not edited; no production service was touched.

## Optional GPU scene checkpoint

Actual NV2A draws now reach the experimental Windows D3D11 scene backend.
The 238-second scratch-save run `gpu-paced-dma-30oz_ib7` rendered Tex and the
tutorial environment and ended at its watchdog without the native crash.
Late scene reports measured 29.95–30.07 presented frames/s with distinct pixel
hashes. Startup/loading dips remain, and this is not proof of mission completion
or fully working controls/effects. Audio had no reported starvation and one
underrun; listening QA remains pending.

The GPU candidate is `work/diagnostic-build/gpu-paced/Release/bruteforce_recomp.exe`.
The CPU reference remains the source default. The GPU backend honors the title's
swap interval by retaining actual DMA GET, pending packet and CALL state across
a held flip. Original physics and timestep code are unchanged. Synthetic WARP
and RTX 3060 Ti CPU/GPU scene comparisons pass; complete game-frame parity,
depth texture 0x2E/shadows and all-level tests remain open.

The native launcher and F1 now share persistent graphics/input/audio preferences.
F1's Renderer page selects scene backend, GPU vertex programs and presentation
for the next launch. The app hides only after successful game-process creation
and returns after normal exit or failure. A real-child/SDL lifecycle fixture
covers those paths. See [GPU evidence, provenance and limits](docs/GPU_SCENE_BACKEND.md).

Changed source: `src/gpu_completion.cpp`, `src/gpu_pushbuffer_fixups.cpp`, `src/gpu_event_bridge.inc`, `src/main.cpp`, `cmake/runtime_fixes.cmake`, `cmake/memory_statistics.cmake`, `cmake/pushbuffer_stream.cmake`, CMake wiring and focused GPU tests. Runtime transformations remain hash-pinned private build copies. The original reusable-buffer helper at 0x00377F00..0x00377F80 has SHA-256 `0ab8a4519944a61257e9f882992fb03c21555d692abd0353bf26c2fc3eaefe06`; its software dispatch and fence behavior were checked against the authenticated XBE and disassembly. GPU CALL register semantics were checked against xemu's public PFIFO implementation. No external binary injection was used.

## Repository integration

The native SDL2/Dear ImGui launcher replaces the removed Python HTTP server,
HTML/CSS/JavaScript UI and browser launch commands. Source builds fetch pinned
public dependencies. Original artwork/icons stay private. New imports retain
497 core-game files and omit the 188 identified ancillary files.

All review-branch commits are incorporated into main. Upstream
Tzeentchnet/BruteForce was checked at `24d2e58687c976390efa23ab8b83a6b80078f1ec`.
Its different XBE checksum means its title-specific hooks are not interchangeable.
See [the feature matrix](docs/UPSTREAM_INTEGRATION_PROGRESS.md) and
[the reuse assessment](docs/TZEENTCHNET_REUSE.md).

The texture math and HRTF fixes are applied to private runtime copies.
HLSL emitters, P8 decode, DXBC cache, scaling policy, real-frame telemetry and
completion contracts compile and pass focused checks. The optional D3D11
scene backend now executes hardware draws and reads back completed frames.
Native DX12 presentation and spatial FSR 1 consume those or CPU-reference
frames; DX12 still needs its own scene pipeline.

The USB lifecycle overhaul remains unapplied pending physical-controller and
cross-platform startup/shutdown tests. Existing USB, IRQ, typing and mouse-menu
patches are retained. No physics, timestep or arbitrary coordinate patch was
introduced. Complete input, audio/effects, all missions, characters, multiplayer
and sustained 30-FPS gameplay across missions remain open.

## Merge validation

The merged Windows Release passes 17 CTests; native Linux passes 14. Python
validation passes 14 checks, and the independent synthetic frame-comparison
suite passes 53 pytest cases. The public-only runtime configuration passes its
nine focused tests without generated game code. The native launcher rebuilds.
Third-party license spacing is preserved, source links resolve and the tracked
file audit contains no original game or compiled payloads.

The isolated merged game candidate is
`work/diagnostic-build/integration-test/Release/bruteforce_recomp.exe`, SHA-256
`467FCC4B4CA1E87A52D40A522F30471FA0494335ECCF1165C20DF24884C07F0A`.
A fresh 238-second scratch-save run under
`work/gameplay-priority/merged-campaign-z2g7ube2/` reached tutorial rendering,
produced real frame CSV records and ended at the watchdog without the earlier
native crash or bad-target diagnostics. Inspected merged-run frames show
terrain/sky but do not establish complete actor/effect rendering. Controller
reports were exercised; merged-build full character visibility and control QA
remain open. Audio starvation/underruns and low scene FPS persist. This is not
an all-level or perceptual-quality pass.

GitHub CI exposed VS 2022's raw-string token-size limit in the FSR headers.
Shader embedding now uses separate 8-KB appends. The reconstructed normalized
source matches both original headers, and the native DX12/FSR GPU smoke passes
color, alpha, letterbox, changing images, live scale, resize and shutdown.

## Preserved bring-up fixes

- GPU import guard resolves 147/147 original kernel imports without rewriting
  ordinary instructions. Local native media validation replaces the approved
  hardware-only DVD check; it verifies real local file hashes.
- Guest RAM statistics no longer report an artificial 32-MB budget. The main
  stack reserves 1 MB instead of 8 MB; the original XBE requests a 128-KB commit.
  The original 2,907,252-byte tutorial geometry buffer then allocates and its
  original relocation works. Empty movie planes caused the earlier green menu;
  the original movie buffer now allocates and renders the blue animation.
- Missing startup callback and five original CRT branch bodies are recovered
  from authenticated bytes into private files, with exact hashes. Other
  unresolved/unsupported translations remain diagnostic failures.
- Native menu mouse selection and original name-entry handlers are connected.
  F1 controls process-local presentation, prompts and audio settings. Common
  gamepad legends switch with keyboard/mouse activity. Native 16:9 scene
  projection, cheats and complete physical-device QA remain unfinished.

## Platform and rollback boundaries

Windows and native Linux builds are checked separately. Linux interactive
mission/input parity is not established by build success. NDK r30 compiled
227 ARM64 translation units; it is not an integrated Android app or S24 Ultra
APK. Android game lifecycle, rendering, input and audio remain incomplete.

Integration builds stay under ignored `work/` folders. Local player updates
are backed up separately; source integration does not migrate saves. Permanent saves,
original ISO/XBE/assets and the pinned toolkit are untouched. No production
service was touched. Git history retains the pre-integration source checkpoint
`9b780bf`; rebuild it for source rollback, preserving all player-data folders.
