# Next-agent handoff prompt - 4 November 2026

Continue the native Brute Force recompilation in `D:\Documents\ChatGPT\bfRecomp`
(Windows/PowerShell), private `firebirdjsb/BruteForceRecomp`, branch main.
Read the user's AGENTS instructions, STATUS.md, docs/RAW_MOUSE.md,
docs/FRAME_RATE.md and docs/DLC.md before editing. This document accompanies the
scene-only-AA / keyboard-prompt source checkpoint. Use `git log -1` and verify
remote/CI against the exact commit; scene-only-AA checkpoint `fd27458f8a4d5d85eeacfbc2073f5e24f4759969` passed
CI 41709249003. Earlier accepted raw mouse checkpoint is
`2bc47c9153f8d1c7cb39eeef9f83ea7d78318a39` (CI 41695942892 succeeded).

The user explicitly requested a GitHub push and a handoff with everything needed
to continue. Push source, tests and documentation only. Never commit retail XBE,
ISO/assets/DLC payloads, generated/recovered game C, captures, logs, save data,
private dumps or binaries. The launcher is a native application, never a website.

## Latest continuation - menu polish and carousel clicks (4 November)

This section supersedes earlier installed-binary and glyph-layout statements.
Latest source adds menu keycap clearance, source-frame glyph lifetime, clickable
mission/map arrows and the previously missed controller-font Start glyph.

- host_ui.cpp right-aligns keycaps against their original icon rectangle with
  a2-pixel gap. ENTER/SPACE grow leftward, away from Select/Accept text. It uses
  the original rectangle's vertical center. Do not shift the original game text.
- The250-ms prompt timer is gone. Pending glyphs are committed by
  bf_ui_prompts_publish(source_frame) when a complete source image is published;
  bf_ui_prompts_select selects the same frame under the image handoff lock.
  Windows uses s_present_ids; SDL uses s_ready_seq. Four bounded history slots
  and a selected copy keep skipped/repeated source images consistent. A frame
  without glyphs publishes an empty batch, so closing menus cannot retain old
  keycaps. Label storage is fixed-size (64 prompts,32 chars each), no frame-time
  heap allocation. Focus loss clears metadata. Direct pre-flip/test painting
  can use pending data until the first published frame. Keep lock ordering:
  image handoff lock then UI guard; never acquire an image lock while holding
  the UI guard. Runtime overlays apply only to their source image.
- Tests ui_prompt_frames_test.cpp cover exact label clearance, first menu-free
  frame, repeated/skipped/retired frames, moving prompts, pending future-frame
  isolation and focus loss. Source CI now builds this public fixture directly
  with host_ui/ui_draw/scene_aa_state; no private game code is required.
- Actual mission screen: root tag16DDAA42; carousel class39E248, observed tag
  E1E43AF2, local106/102/394/140. Original draw103DD0 owns default arrow images
  at+9C/+A4, but those are HIDDEN on the retail skin (flags12). Visible sibling
  art is class39E290, left tag1FD9234B at74/102/32/140 and right tag10BA4815 at
  500/102/32/140. These coordinates describe evidence only, NOT player constants.
  Mouse code resolves live objects/positions, supports visible owned arrows
  or same-row/height adjacent sibling art with those IDs. It requires enabled/
  visible carousel and matching navigation owner; root is still managed/active.
  Do not regress to hit-testing only the hidden owned images or the content
  rectangle. It focuses via original FFDC0 and queues Left/Right (10/11).
  Original1038E0/103A70 retain bounds, selection, previews and notifications.
- Read-only atlas verification confirmed A/B/C/D=ABXY, E/F=LT/RT, G/H=Start/
  Back, I/J=White/Black. Earlier generic C/D mapping was wrong; it now shares
  the HUD's corrected order. Hint clicks use the same glyph_control lookup.
  G now resolves to physical Start's desktop key ESC, including both native
  and generic standalone font spans. English literal Start rewriting remains.
  Actual button reports/raw mouse and controller ownership are unchanged.
  Private font evidence is work/menu-polish/controller-font-physical.bmp plus
  font-metrics.bin; never push it. Read resource Data through its contiguous
  alias (0x80000000 | Data), not unrelated low-heap bytes.

Validation: full Windows/Linux player links;33/33 Windows and22/22 Linux tests;
standalone Linux command used by the new public CI step passed. Previous9b79981
CI result should be checked against its own commit. Current Python/source checks
and new CI result should be verified against the final commit after push.

Runtime evidence (all disposable profiles, original timing, D3D12 scene):
- First arrow attempt reached the real selector but missed its hidden default
  arrow objects. Read-only inspection showed the visible skin siblings. The
  repaired attempt below passed; do not call the first attempt successful.
- work/menu-polish/interval-original-9nxkqwaj/boot-p5puy9xj/boot.log:
  right hover/click at516,172 observed index0, later hover index1; left click
  at90,172 observed index1, later hover index0. No unresolved/invalid guest call.
- work/menu-polish/interval-original-agkpqm2q/boot-eod6uk78/boot.log:
  a visible window pass repeated the arrow changes, backed out twice, and
  captured the actual window compositor. map-visible.bmp shows clean spacing
  between ENTER and Select; main-after-close.bmp shows no lingering keycaps.
  These two captures were visually inspected. Unlike older headless captures,
  these are real rendered menus, not solid-color readback artifacts.
- All bounded runs ended at their watchdog (exit3), not a new diagnosed crash
  or completed mission. Native fixture events are scripted input; physical
  mouse/controller acceptance and every menu transition are still pending.
  Continue checking movie transitions, localization, confirmations and long
  key labels; do not equate this menu pass with broad gameplay validation.

Current build cache work/diagnostic-build now outputs to:
work/menu-polish/player/Release/bruteforce_recomp.exe.
Its SHA256 and both installed game paths match:
5c6da703de67167815b75a19362e194e9d531b16fff21eeb124254cbd4c4c46c.
Canonical selected path: work/diagnostic-build/Release/bruteforce_recomp.exe.
Old prototype alias: work/aa-input-quality/player/Release/bruteforce_recomp.exe.
Matching maps were copied. work/ui-scene-fix/player is now an OLD candidate;
never assume it is the latest build or point the launcher back to it.
Backup work/menu-polish/install-20261104-081518/install-manifest.json records
previous files/maps/settings and64 protected save/settings hashes. All64 stayed
identical; native --validate-only returned0. Launcher selection/preferences
were not rewritten. Launcher remains2e225405... from the earlier AA installation.
No original XBE/ISO/DLC or production service changed. Close apps before rollback,
restore matching binary paths from this newest manifest and keep player-data.
Own QA runs have ended. Check current processes before replacing any binary.

## Latest continuation - HUD and pause fixes (4 November)

The screenshot report expanded to HUD AA, swapped R/MMB labels, Start/trigger
wording, and displaced/unresponsive pause controls. Source and installed player
now include the following corrections; verify them with a real mouse before
claiming full player acceptance.

- Native HUD images use original primitive BA950, before CCD20 font draws.
  cmake/inline_glyphs.cmake now authenticates chunk0040 SHA
  d51c8e0605f62e43f2b4179d860ddfa0206fdbc0d01e4970fc113f6ddf0c4035 and
  places the ordered marker at that primitive's entry, before any HUD pixels.
  The existing once-per-surface/frame gate protects later UI. Expanded real
  NV2A CPU/D3D11/D3D12 test preserves all384 opaque HUD-art and256 text pixels
  exactly On/Off;99 CPU /104 GPU remaining scene pixels change. Transparent
  HUD naturally composites against the changed scene; no final-frame AA.
- HUD C/D order is now X/Y -> R/MMB via hud_glyph_control. Generic-menu mapping
  and all actual input reports remain unchanged. This is the user's requested
  HUD correction, not a new controller remap.
- src/ui/prompt_text.{h,cpp} renders English Start-button wording as ESC;
  trigger-to-fire wording as LMB. User explicitly confirmed the left-trigger
  instruction meant **weapon firing**. Inventory/throw wording remains G;
  context handles original swapped-trigger descriptions. Labels come from the
  primary binding table. Unmatched/localized strings and controller use retain
  originals. A source-word boundary prevents edits to ordinary Restart/Start
  Campaign labels. This is not a complete localized instruction translation.
- Native CCD20 wrapper calls the renamed authenticated original font draw with
  copied44-byte return/argument frame and changed UTF16 in2048 unused guest
  stack bytes, at most383 units. It restores caller-relative ESP delta, leaves
  caller args/source strings intact, keeps original fonts/alpha/wrapping path,
  and refuses invalid/read-only scratch pages. Tests cover context labels,
  controller fallback, source/argument identity and protected destinations.
- Pause root virtual GetPosition+8 points to1012E0, not base EF0F0. Root+65 bit0
  selects current player viewport; bit1 centers in its safe area. Native
  original1013D0/101510/101590 own this calculation. The C bridge preserves all
  guest GPR/SSE/x87 state, uses a validated8-byte scratch output and calls the
  actual original getter. Both keycaps and mouse hit rectangles use it. No
  viewport coordinates were hardcoded into player code. Original focus/action
  methods remain. Class39E060 Aiming value now focuses on hover and queues
  normal D-pad Right on click (original10C410 owns cycling and notification).

Validation: Windows/Linux full links, all32 Windows and21 Linux tests passed;
expanded AA preservation passed CPU/WARP11/WARP12; focused viewport fixture
checks false hits at old coordinates, original-focus callback indices, Resume
Accept pulse, Aiming Right pulse and exact keycap raster location. Glyph test
was rerun after adding destination write-protection coverage. XBE verifier and
Python compileall passed. Earlier59132b3 CI41823813815 succeeded.

A real four-minute disposable campaign run:
work/hud-aa-fix/interval-original-zesn5qij/boot-mwyefmt3/boot.log.
Driver work/hud-aa-fix/pause_qa.py / boot_scratch.py copies only prior disposable
TDATA/UDATA, omits Cache, uses forced pad input and BF_UI_INPUT_LIVE. It reached
Basic Controls at~86s, opened pause at~91s, read native Resume rect184/173/272/20
and Accept rect218/313/110/20. It hovered/clicked Resume at320,183, reopened
pause, hovered Restart at320,208, returned to Resume and clicked again. Actual
original focus flags changed (96->97 on Resume); no unresolved/invalid guest
call was found. Its watchdog exit3 is a bounded ending, not mission completion.
Framebuffer captures were solid-color in this headless mode: do not call them
visual gameplay/HUD evidence. Physical mouse ownership/cursor and actual AA
On/Off HUD appearance still need a visible player test. No Restart/Quit action
was clicked. The run has finished; never reuse its PID44012.

Installed player SHA256 at canonical, candidate and old-prototype paths:
7eef314cdd101a9fda9427cf353af197b61cd614abf3d99649a4e822a917b61a.
Newest backup work/hud-aa-fix/install-20261104-072558/install-manifest.json
contains old executables/maps/settings and64 protected save/settings hashes.
All64 retained identical bytes; native --validate-only returned0. Launcher
settings still selects work/diagnostic-build/Release/bruteforce_recomp.exe.
The launcher remains the prior updated build2e225405... and is open at root;
no launcher binary or preferences changed in this second installation. Original
ISO/XBE/DLC and production services remain untouched. Restore matching manifest
paths only with apps closed, keeping current player-data. No game is currently
running at handoff preparation; check processes rather than assuming this lasts.

## Immediate objective and remaining user reports

1. Verify the scene-only AA correction in real gameplay: text, HUD, menus and
   prompts must stay sharp. Do not restore the old completed-frame AA path.
2. Verify the installed pause-menu coordinate fix with real mouse hover/click,
   especially Aiming and confirmation dialogs; retain original action callbacks.
3. Verify current keycap spacing/lifetime and all instruction contexts with a
   real mouse/controller. Keep current atlas mapping and input bindings. Wide
   keycaps now extend away from following labels; do not restore the old anchor.
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
  `0xBF414100` before authenticated native HUD image primitive `0xBA950`, original font
  entry `0xCCD20` and generic UI quad entry `0xEE6E0`. Original device global `0x37DD88`; device+0/+4 are
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
  HUD glyphs A/B/C/D/H/I/J map to A/B/X/Y/Back/White/Black (corrected above). Ordinary font ID
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
Current `position()` also calls the original viewport-aware root getter as
described in the latest continuation; this older snapshot omitted that behavior. `tick()`
requires registered active generic root and enabled children, queues normal
pad input after original focus callback. Optional BF_TRACE_UI logs bounded
`[UI-PAUSE]` child geometry/state when that view first becomes active.

Confirmed direct children (tag, vtable, local x/y/w/h):
- Resume 16C06C82,39E140, 0/35/272/20.
- Restart Mission 14398E21,39E140, 0/60/272/20.
- Restart Chapter 1FFD6617,39E140, 0/85/272/20, HIDDEN in snapshot.
- Aiming label 0E610311,39E140, 10/85/92/20.
- Quit 13D713E1,39E140, 0/110/272/20.
- Aiming value 1463DDF4,39E060, 102/85/158/20: now handled by mouse tick (see latest continuation).
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

## Earlier AA installation - 4 November 2026 (superseded by latest continuation)

User supplied AA Off / Edge AA screenshots showing corrupted F1 overlay text.
Process inspection proved PID16376 was running the obsolete full-frame-AA
prototype at work/aa-input-quality/player/Release, hash95a8a2bc1f5d9606afe106ca6ec1dd8ed7ffeb175eb385ea7b8b4e3e89a5218d.
The correct fd27458 source was pushed, but its candidate had NOT been installed
and launcher.settings still selected that old binary. This was an installation
mismatch; no new speculative filter changes were made. The user closed both
game and launcher normally for installation. Do not reuse these old PIDs.

The scene-only player is NOW installed at all three of these paths:
- work/diagnostic-build/Release/bruteforce_recomp.exe (selected by launcher).
- work/ui-scene-fix/player/Release/bruteforce_recomp.exe (current build output).
- work/aa-input-quality/player/Release/bruteforce_recomp.exe (old path corrected).
Matching SHA256:
`88553bbcfc6e7b3365f9b64e324b099b7ef30330619111160fe4af409e0052e2`.
The additional source change is a descriptive F1 label:
`AA: SCENE ONLY / TEXT EXCLUDED`. Matching .map copies accompany both installed
player paths. work/diagnostic-build remains the build cache and its output
remains work/ui-scene-fix/player. Do not assume rebuilding updates other copies.

Updated native launcher is installed at root BruteForceLauncher.exe and
work/native-launcher/build/native_launcher/Release/BruteForceLauncher.exe;
source build output work/interpolation/launcher/Release/BruteForceLauncher.exe.
All three SHA256:
`2e22540588387a8e49bd064ff806a25bf48bc3f1800bcf4b76d72851e075df6b`.
Both installed SDL2 DLLs match the launcher build. Private artwork/DLC folders
were retained. Launcher AA help now explicitly says text/HUD/F1 are excluded.
Use `Launch Brute Force.cmd` or root launcher for the next test.

New backup:
`work/ui-scene-fix/install-20261104-064548/install-manifest.json`.
It contains exact source/installed hashes, old binaries, both old maps where
present, launcher/graphics settings and hashes of all63 permanent files under
TDATA/UDATA plus graphics.settings. All63 verified byte-identical after install.
Only launcher.settings intentionally changed to select the canonical player;
its graphics/backend/FSR preference line was retained. Native --validate-only
against the installed game-core returned0 (native-validation.log in backup).
No original game/DLC file or production service was modified. Only local app
executables, matching maps/DLLs and the selected executable preference changed.

Permanent library `C:\Users\fireb\AppData\Roaming\Firebird\BruteForceRecomp`.
Preserve player-data/TDATA/4d53001e and UDATA, global-data/global-gameflags and
profiles. Current graphics.settings, after user turned AA Off before exit:
`auto d3d12 1 1 0 0 1 1 0 100 2.03 30 120 1 0` under BF_GRAPHICS1.
Do not reset sensitivity2.03, interpolation120, texture1 or AA without choice.
The last field is AA; the user can toggle it in F1 for a real comparison.

Full player and launcher rebuilt after the label-only edits; focused native
menu check1/1 and launcher checks4/4 passed again. Existing AA CPU/GPU tests and
32/21 suites above cover the unchanged filtering implementation. Actual new
AA-On versus AA-Off F1/HUD/menu screenshots still need live player acceptance;
installation and synthetic preservation checks alone are not gameplay proof.

Rollback: close both apps normally, restore exact backup paths from this new
manifest, and restore launcher.settings only if intentionally selecting that old
build; retain current player-data. Restoring obsolete AA prototype reproduces
its text issue. Accepted pre-AA raw mouse player remains privately retained at
work/crash-mouse-fix/verified-player/Release, SHA
`d3424454778dae60177a92098b04d10d90b7da3ff123d7ef4aa990df0c017f93`.
Earlier backup work/crash-mouse-fix/install-20261103-133522 remains available.

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
