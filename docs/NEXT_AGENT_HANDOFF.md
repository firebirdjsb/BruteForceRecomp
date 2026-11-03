# Next-agent handoff prompt - 3 November 2026

Continue the native Brute Force recompilation in `D:\Documents\ChatGPT\bfRecomp`
(Windows/PowerShell), private `firebirdjsb/BruteForceRecomp`, branch main.
Read the user's AGENTS instructions, STATUS.md, docs/RAW_MOUSE.md,
docs/FRAME_RATE.md and docs/DLC.md before editing. This document accompanies the
scene-only-AA / keyboard-prompt source checkpoint. Use `git log -1` and verify
remote/CI against the exact commit; earlier accepted checkpoint is
`2bc47c9153f8d1c7cb39eeef9f83ea7d78318a39` (CI 41695942892 succeeded).

The user explicitly requested a GitHub push and a handoff with everything needed
to continue. Push source, tests and documentation only. Never commit retail XBE,
ISO/assets/DLC payloads, generated/recovered game C, captures, logs, save data,
private dumps or binaries. The launcher is a native application, never a website.

## Immediate objective and remaining user reports

1. Verify the scene-only AA correction in real gameplay: text, HUD, menus and
   prompts must stay sharp. Do not restore the old completed-frame AA path.
2. Finish mouse hover/click on pause/Options/exit menu, retaining original focus,
   action callbacks and Restart/Quit confirmations. It is NOT yet fixed here.
3. Verify inline gameplay keycaps, e.g. `Hold Xbox X to swap` -> `Hold R to swap`
   with the Windows raw desktop profile. Keep controller ownership/fallback.
   Fit longer labels (SPACE/ENTER) within their allocated space; current host
   keycaps can widen beyond the original glyph rectangle.
4. Replace squad-command controller artwork and D-pad wording for MKB. The
   inline controller-font replacement covers only known font glyphs; round
   action artwork and literal D-pad text are NOT replaced yet.
5. Keep the mouse Basic Controls popup; skip other authored tutorials only for
   MKB. Test actual progression/localization/device switching.
6. Preserve accepted raw mouse feel. User said **"this is the fix" / "this feels
   good"** after the 2bc47c9 camera repair. Do not retune it speculatively.

Broader requests remain: complete combat/all missions/characters/abilities/FX/
audio/controllers/DLC maps/multiplayer validation; native internal resolution
and 16:9; genuine temporal FSR/DLSS; Linux GPU; native Android APK for Canadian
Galaxy S24 Ultra; cheats/F1 settings and expanded peer-hosted multiplayer.
None of those broad milestones is established by this checkpoint.

## Current source changes and exact implementation boundaries

- `src/ui/scene_boundary.{h,cpp}`, `scene_aa_state.cpp`,
  `cmake/scene_antialias.cmake`, `src/game_text_guest.c` and
  `cmake/inline_glyphs.cmake`: enqueue private NV097_NO_OPERATION marker
  `0xBF414100` before authenticated original font entry `0xCCD20` and generic
  UI quad entry `0xEE6E0`. Original device global `0x37DD88`; device+0/+4 are
  push cursor/threshold. Exact 8 command bytes, cursor advance and write-page
  checks. When necessary the wrapper uses original reserve function 0x371E10
  and its audited RET 8 ABI; integer/SSE/x87 guest registers are restored.
- Executor flushes queued scene draws at the marker before subsequent UI,
  invoking an optional backend `before_ui` callback. Each surface is marked
  once per frame even when AA is Off, so toggling On midway through UI cannot
  filter earlier UI. All generated/runtime edits use private copies after
  canonical source authentication; do not modify the pinned toolkit.
- `src/graphics/edge_aa.{h,cpp}`: bounded spatial directional filter, preserves
  alpha. CPU implementation currently accepts 32-bit surfaces only, original
  pixels retained otherwise. CPU filtering has measurable cost (~10 ms at
  640x480 on one synthetic host case); do not claim a performance improvement.
- `src/gpu/scene_antialias.{h,cpp}`, `src/gpu/d3d11_backend.c`: color snapshot
  and fullscreen GPU pass, used by D3D11 and D3D11On12/D3D12. Restores changed
  rendering state. Does not run within active visibility queries. Allocation/
  unsupported-surface failure retains original scene and logs bounded errors.
- No AA processing remains in the final framebuffer or DX12 presenter. No
  empty compatibility filter is retained. FSR1/interpolation paths unchanged.
- `src/ui/graphics_options.h`, `host_ui.cpp`, `native_launcher/app.cpp`: shared
  optional trailing version-1 AA boolean, new-profile default On; existing
  explicit Off preserved. Launcher Graphics / F1 Renderer expose it live.
- `src/input/desktop_bindings.h` / `control_labels.h`: primary controls agree
  with actual input reports. Desktop Xbox slots A SPACE, B E, X R, Y MMB,
  Black F, White Q, LT G, RT LMB; D-pad arrows; Start ESC, Back TAB,
  left-thumb SHIFT, right-thumb RMB. Menus A ENTER/B ESC/X C/Y V. Existing
  aliases remain. This does not implement arbitrary user rebinding.
- `src/ui/gameplay_glyphs.cpp`: at original font instruction 0xCD219, validate
  resolved controller font ID `0x0E38A4CA`, UTF16 cursor and quad geometry.
  Known glyphs A/B/C/D/H/I/J map to A/B/Y/X/Back/White/Black. Ordinary font ID
  `0x0C626FD5`, unknown icons, invalid memory and controller use keep original
  draws. For replaced glyphs jump only over four vertices to original End
  0xCD2B9, retaining original stack cleanup. Shadow return PCs 0xC9A88/0x10A572
  do not queue duplicate host keycaps. Full real-game placement remains QA.
- `src/game_menu_input.cpp`: registered generic prompt owner determines menu
  labels, remembered resolved font identity enables matching inline generic
  text. Eight allowlisted non-basic tutorials are hidden for Draw only, flags
  restored, original Continue A queued once per page. IDs verified across all
  five language resources: EBE31566, E2E425F0, 0680B053, 0F8780C5, 148ED17F,
  1D89E1E9, 0736FC78, 0E31CCEE. Basic F0EA44DC retained; same-size mission
  briefing E83F2E8A explicitly excluded. Basic guide still relies on English
  semantic text; unsupported localized Basic pages retain originals.

## Tests and runtime evidence for this checkpoint

Passed locally after latest code changes:
- Full Windows and Linux player links.
- `ctest --test-dir work/diagnostic-build -C Release --output-on-failure`: 32/32.
- WSL `ctest --test-dir /var/tmp/bruteforce-build --output-on-failure`: 21/21.
- Public runtime checks: both NV2A scene-parity suites, ordered marker safety,
  and scene AA/UI preservation: 4/4. CPU changed 123 / GPU changed 127 scene
  edge pixels; all 256 later UI pixels remain byte-identical On versus Off,
  including repeated boundaries. This exercises real CPU and WARP GPU draws.
- Windows native launcher: 4/4 (lifecycle, single instance, discovery, DLC).
- `python -m compileall -q scripts tests tools`; unittest 17 (one skipped);
  pytest frame comparison/timing 59 passed. Supported XBE verification passed.
- New safety fixtures: exact marker/cursor/bounds/read-only pages, pitch/alpha,
  no filtering after first UI boundary; gameplay font/binding pixels, shadows,
  invalid inputs/controller fallback and unchanged guest memory.
- CI workflow now includes ordered boundary and AA/UI preservation in the
  public runtime matrix. Confirm its actual result after the push.

Private evidence: `work/ui-checkpoint-{build,linux-build,windows-tests,
linux-tests,public-build,public-tests,launcher-build,launcher-tests,python,
pytest}.log`. Actual generated builds need the private inputs below.

Disposable D3D12 run:
`work/ui-scene-fix/interval-original-r9n5zm98/boot-mx04jj0a/boot.log`.
It reached Basic Controls, logged `[AA] scene before UI ...`, then ended at a
168-second game watchdog (driver includes earlier save-copy/startup time).
No guest-call failure was found. It did NOT reach verified pause-menu hover/
click or demonstrate mission completion. A watchdog exit3 is not success or a
new diagnosed crash. Driver `work/ui-scene-fix/pause_qa.py` uses forced pad
input, so cannot verify real MKB ownership. Seed helper copies large disposable
save/cache trees before Popen; reduce that private scratch copy for next QA.
Never substitute permanent user saves or recursively delete a game-core junction.

## Pause and squad evidence: next investigation

User left pause open for read-only inspection. Private snapshot:
`work/aa-input-quality/pause-ui-snapshot.json`, captured from old player PID34888.
That process subsequently closed normally. No game/launcher process was running
at checkpoint preparation; always recheck current processes. Do not use stale
PID values or kill a user's session. Normal gameplay Escape twice stopped the
Computer Use skill; current work avoided UI automation and used CLI/read-only
inspection. Prefer this while the user is playing.

Pause root: original generic view vtable `0x39E3D8`, tag `0xFDCD29B4`, visible/
enabled flags FFFFFF16. Old address 0x014DC100 is VOLATILE, not a reusable offset.
It IS registered in the primary manager at runtime (observed 0xE66FC0).
An earlier guess that pause used a different unmanaged owner was wrong; do not
add a broad managed-view bypass based on it. Snapshot omitted animation offsets
+0x1C/+0x20 and failed to capture fresh +0x74 state; inspect those next.
Existing `position()` adds owner transforms and animation offsets. `tick()`
requires registered active generic root and enabled children, queues normal
pad input after original focus callback. Optional BF_TRACE_UI logs bounded
`[UI-PAUSE]` child geometry/state when that view first becomes active.

Confirmed direct children (tag, vtable, local x/y/w/h):
- Resume 16C06C82,39E140, 0/35/272/20.
- Restart Mission 14398E21,39E140, 0/60/272/20.
- Restart Chapter 1FFD6617,39E140, 0/85/272/20, HIDDEN in snapshot.
- Aiming label 0E610311,39E140, 10/85/92/20.
- Quit 13D713E1,39E140, 0/110/272/20.
- Aiming value 1463DDF4,39E060, 102/85/158/20: class NOT handled by mouse tick.
- Accept 0499CE0B / Back 0E8204A4,39E020, 34/175 and148/175.
Original selectability 0x1097C0 requires visible bit4 and byte at +0x74 ==0;
existing filter is intentional, not proven cause. Focus original0xFFDC0 owns
callbacks. Do not click Restart/Quit while merely inspecting.

Private inspector `work/aa-input-quality/inspect_ui.py` is read-only, requires
native PE64/exact path and matching .map for g_xbox_mem_offset. The map in
work/diagnostic-build now belongs to NEW candidate, not old accepted binaries.
Do not assume RVAs match. Check inspector exit before reading JSON: failed reads
can leave an OLD snapshot present. Use page-bounded reads around guest holes.

Gameplay HUD uses a second UI engine: text vtable39F9B0, draw0xBB6C0;
font renderer CCD20 is common to both. Component hidden bit0 at+13; packed
short x/y at+8/+A, height/width at+C/+E. Text layout embedded+2C, string
pointer/inline+8,length+18,capacity+1C,font+24,screenXY+28/+2C.
Squad root vtable39F708; virtual update+14=C0FB0, draw+18=B22A0,
init+30=C0180. Children list atroot+2C. Root+BC bit0 open/bit3 closing;
C10D0 opens. C0180 fills pointers+74/+80/+84/+88/+8C, title key ECE71B9D.
B22A0 draws original child list B2720(root+2C). Investigate exact active child
identities/rectangles for artwork replacement, not screenshot coordinates.
Literal D-pad help string key1432B41C; Hold prefix1CC51F04; squad glyph row
ABCDJI and Back H confirmed. global469AB0 is asset/factory manager, NOT widget
root. No squad artwork replacement has been implemented.

## Local binaries, launch target and rollback (currently verified)

NEW candidate (built, NOT installed or selected in real launcher settings):
`work/ui-scene-fix/player/Release/bruteforce_recomp.exe`
SHA256 `c56db323cda62166d03703b6fb8299d87521e2d64afdf7f8f343d5b9413f7cb0`.
Windows build cache `work/diagnostic-build` outputs there. Rebuilding this cache
does NOT replace its older Release/bruteforce_recomp.exe path.

Accepted raw-mouse player remains at:
`work/diagnostic-build/Release/bruteforce_recomp.exe`
SHA256 `d3424454778dae60177a92098b04d10d90b7da3ff123d7ef4aa990df0c017f93`.
Same accepted candidate retained in work/crash-mouse-fix/verified-player/Release.
Backup `work/crash-mouse-fix/install-20261103-133522/install-manifest.json`
records previous binaries and 179 save/settings hashes preserved on that install.

Real launcher.settings STILL selects the obsolete full-frame-AA prototype:
`work/aa-input-quality/player/Release/bruteforce_recomp.exe`
SHA256 `95a8a2bc1f5d9606afe106ca6ec1dd8ed7ffeb175eb385ea7b8b4e3e89a5218d`.
Do not call that build the AA fix. Next install/test must deliberately select
new candidate and preserve rollback. Latest built launcher:
`work/interpolation/launcher/Release/BruteForceLauncher.exe`
SHA256 `02da7533ed25018498b104fa6bddabfed825efdd722a1685b0ae72454d042685`.
Launcher cache work/native-launcher/build outputs there. Root launcher/normal
installed copies have NOT been replaced by this checkpoint. Keep SDL2 DLL and
private assets alongside the selected launcher. Previous launcher-settings
backup: work/aa-input-quality/launcher-open-20261103-190734.

Permanent library `C:\Users\fireb\AppData\Roaming\Firebird\BruteForceRecomp`.
Preserve player-data/TDATA/4d53001e and UDATA, global-data/global-gameflags and
profiles. Current graphics.settings (explicit AA Off):
`auto d3d12 1 1 0 0 1 1 0 100 2.03 30 120 2 0` under BF_GRAPHICS1.
Do not reset sensitivity2.03, interpolation120, texture2 or AA without choice.
No installation/settings/save changes were made while preparing this push.
No production service or original game/DLC files were modified.
Rollback source by reverting checkpoint; for binaries/settings close normally,
restore matching backup paths, retain all current player-data. Never terminate
the user's game merely to overwrite its executable.

## Build inputs, prior fixes and platform limits

Pinned public toolkit (must remain clean):
`C:\Users\fireb\Documents\Codex\2026-10-31\le\work\xboxrecomp`,
commit f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b.
Private generated: sibling bf-analysis/generated; recovered: work/recovered.
Supported XBE `D:\Downloads\BruteForce-Decompilation\default.xbe`, SHA256
B6EA5D4DD6500AE86C3F5E3E4618BC9B1E3FACDDCB483170DFEE7A787087C721,
entry2F5D1F/title4D53001E. `python scripts/verify_offsets.py --target PATH`
checks this XBE, not native PE equivalence. Keep canonical body/hash validation.

Windows: `cmake --build work/diagnostic-build --config Release --parallel 4`.
WSL BruteForceBuild: repo /mnt/d/Documents/ChatGPT/bfRecomp,
player cache /var/tmp/bruteforce-build, launcher /var/tmp/bf-native-launcher.
`cmake --build /var/tmp/bruteforce-build --parallel 4` plus ctest.
Public runtime cache work/public-runtime-smoke, BF_RUNTIME_CHECKS_ONLY=ON;
no private game data needed for its WARP/CPU regression suite.
Native launcher: `cmake --build work/native-launcher/build --config Release`.
Python PYTHONPATH=scripts; optional capstone5.0.7 at work/tools/python-libs.
Android NDK r30 installed in work/tools/android-ndk-r30 with user acceptance,
but Android CMake intentionally rejects unsupported APK builds. No APK exists.

Accepted raw mouse hook is original camera0x133EA3 after curves/ramp/assistance,
not former post-clamp hook0x1F33DF. Input logical ID differs from physical port;
validate current dynamic396868 proxy +C4 ->396840 physical +BC port0 chain.
Original30 timing, authored camera limits/motion and real controller path remain.
Prompts Off must NOT disable raw input. Interpolation adds one source-frame delay.
Do not restore second analog clamp or invented native60 simulation.

Recovered combat entries: exact 8-byte vtable tail thunk1D8F0, plus787-byte
aligned-stack script callback2E490..2E7A3 (SHA891f0f02b028b3a6c53885b24a9416a9ffb396b5bcf814108766a4bdb1635c6f).
Pinned translation+registration and real-body fixtures pass Windows/Linux.
User's long running/killing crash sequence still not replayed completely.
Window thread now stop/joins before guest unmap; regression checks pass. Exact
fatal combat/shutdown DX12 error0x80070057 still needs replay; never silence it.
RECOMP_ASYNC_IO1 remains default; intermittent pre-movie startup stall is open.
Avoid BF_TRACE_FILE_IO normally (huge logs). Display180 can include repeats;
original simulation stays30. GPU D3D12 scene uses real D3D12 via D3D11On12;
a separate native DX12 scene pipeline, Linux GPU and native Android are unfinished.

Texture reconstruction Off/2x/4x is bounded Catmull-Rom for eligible sampled
color/cube assets, preserves original mips/alpha, excludes movies/depth/RTs,
extra cache256MiB/dimensions4096. Not HD source assets or native render resolution.
Native16:9 needs consistent camera/HUD/RT/depth-alias transforms; temporal APIs
need real depth/motion/jitter/reset/HUD separation. Do not show fake enabled DLSS.
USB overhaul gated on real controllers and cross-platform lifecycle tests.

DLC: D:\Downloads\BruteForceDLC.zip, private pack work/native-launcher/dlc-pack,
CMake BF_PRIVATE_DLC_DIR; 51 SHA-verified data files: Lockjaw, Cerulean and
Prisoners' Lament. Allowlist content/dlc-manifest.json; no Xbox installer or
regional title-update XBE execution. Original titleupdate profile differs, never
replace retail default.xbe. Launcher installs/verifies in TDATA/title/$c.
Logs confirm metadata discovery only; all three maps still need playthrough.
QA work/combat-input-quality/dlc-qa/game-core is a JUNCTION into
work/native-launcher/core-qa/game-core. Never recursively delete through it.

Use bounded evidence-led repairs, at most two repair loops per issue. No
speculative physics, ABI, offsets or timing patches. No subagents requested.
Clearly separate synthetic checks, bounded smoke, user acceptance and full
mission/controller/multiplayer validation. Continue from actual current code
and processes, not stale screenshots, PIDs or installed-path assumptions.
