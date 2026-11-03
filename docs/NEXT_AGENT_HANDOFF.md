# Next-agent handoff prompt — 3 November 2026

Continue the native Brute Force recompilation in `D:\Documents\ChatGPT\bfRecomp`
(Windows/PowerShell), private `firebirdjsb/BruteForceRecomp`, branch main.
Read the user's AGENTS instructions, current source, STATUS.md, docs/DLC.md
and docs/FRAME_RATE.md before editing. The user explicitly requested a GitHub
checkpoint and this handoff. Push source when requested; never commit original
assets, DLC payloads, generated/recovered game C, captures, saves or binaries.
Keep permanent saves/preferences and the native application launcher.

Use evidence-backed narrow repairs, at most two repair loops per issue.
Do not claim compilation, synthetic tests or bounded smoke runs prove complete
mission/combat/audio/controller acceptance. Do not apply speculative physics,
coordinate, ABI or instruction patches. No subagents are requested.

## Current requirements and checkpoint scope

Playable campaign is priority 1. Preserve original 30-FPS simulation; the user
wants interpolated 60–120 visuals, not faster guest timing. Other requested work:
snappy native mouse/keyboard, HUD/assets, native 16:9 and higher scene resolution,
temporal FSR/DLSS, Linux GPU, S24 Ultra Android APK, all missions/characters/FX/
audio/controllers, cheats and expanded peer-hosted multiplayer.

This checkpoint implements an authenticated missing combat thunk, mouse aim
after the original analog clamp, optional GPU texture reconstruction, and
automatic installation/verification of the supplied DLC. Broad remaining
features are not implemented by these changes. No website launch path returned.

## Installed local applications and rollback

Installed player: `work/diagnostic-build/Release/bruteforce_recomp.exe`
SHA-256 `0576bd7688c7587ee27c3914fd4d14bf96871955393e3d6b59e64a81980d7de8`.
Installed launchers: root `BruteForceLauncher.exe` and
`work/native-launcher/build/native_launcher/Release/BruteForceLauncher.exe`,
both SHA-256 `eceb229abe3ec0d7d9661ed43a71f37f99e6718c3e01d6bdf50678d955480012`.
Use `Launch Brute Force.cmd` from the workspace. Check current process paths
before replacing anything; never terminate the user's gameplay for installation.

Backup and exact install manifest:
`work/combat-input-quality/install-20261103-095648/`.
The installer recorded 107 existing save/settings files and verified that all
retained identical bytes. It updated only local executables and added the DLC.
To roll back binaries, close them normally and restore the matching backed-up
paths from install-manifest.json. Preserve the current player-data directory.
No production service, original ISO/XBE or pinned toolkit was modified.

Library: `C:\Users\fireb\AppData\Roaming\Firebird\BruteForceRecomp`.
Permanent progress is under `player-data/TDATA/4d53001e` and UDATA. Never replace
`global-data.*`, `global-gameflags.*` or existing profiles with QA fixtures.
Original title startup maps T: to TDATA/title; TitleData is a separate empty
fallback and is not the correct DLC destination.
Latest existing graphics preferences retain scene d3d12, original30, visual120
and mouse sensitivity3.11; do not overwrite them with scratch settings.

## Combat crash: exact diagnosis and repair

Original user crash occurred while running/killing enemies. Captured log:
`work/crashes/2026-11-03-17937133779081499/game.log`, line47860,
`Unresolved guest call at 0x0001D8F0; stopping.`, exit20.
Original session is AppData library/sessions/17937133779081499/game.log.
Preserved log SHA-256:
`1ddb0e0c216f8c9bf9d8c93060ecb503fddc371ca2f5c7cc1a644ee49116c884`.

For the authenticated retail image, 0x1D8F0 is exactly eight executable bytes:
mov eax,[ecx]; jmp dword ptr [eax+0x6DC]. Body SHA-256:
`ae24c26279f8dc7f0f1bff547ef015024d464df679265916ea8bf3e1265b8e64`.
Discovery incorrectly merged it into neighboring 0x1D8CA. Call chain:
0x3B750 -> 0x22F70 -> 0x1D8F0; observed return0x3B7B9,
original vtable0x3A47B8+0x6DC -> method0x1A47D0.

scripts/recover_entries.py now authenticates/translates that exact original
body via the pinned FunctionTranslator into private work/recovered/recomp_gameplay.c.
CMake compiles it; src/main.cpp registers its original address. No invented
physics routine or hand-written guest behavior replaces the thunk.
tests/recovered_gameplay_test.c executes it with a synthetic object/vtable and
checks one dispatch, return value, ECX/callee-saved registers and tail-call stack.
It passes Windows/Linux. A ten-minute first-mission scripted run had no guest
failure but did not reproduce an actual enemy-kill transition. Do that next
with scratch saves; do not assert the full combat crash is accepted yet.

## Mouse and texture work

Original 0x1F2CF0 clamps processed right axes to [-1,1] after the previous
callback. cmake/desktop_input.cmake now calls bf_mkb_apply_native_aim at exact
loc_001F33DF after that update. src/input/native_mouse.cpp validates port at
controller+0xBC, state pointer+0xB4 and writable floats state+0x0C/+0x10,
rejecting invalid/protected destinations. Other ports/buttons/movement remain
on the original path. tests/native_mouse_test.cpp executes the actual private
translated update and checks clamp versus native4/-3, stop, other port, bad
memory and exact stack cleanup. Raw polling/sensitivity tests also pass.
Player mouse feel and visual latency still need manual testing; interpolation
adds one source-frame delay. Do not silently change sensitivity or this setting.

src/gpu/asset_upscale.{h,cpp} reconstructs eligible sampled color assets by
bounded Catmull-Rom filtering, Off/2x/4x. Original mip pixels are retained below
the added levels. RGBA/BGRA, 2D and six cube faces are supported; dimensions cap
at4096 and optional extra cache at256MiB, with original fallback. Movies,
render targets and depth aliases are excluded. Linear mipped assets can use
16x anisotropy; point/UI sampling is preserved. GPU cache invalidation and
source rewrite detection remain active. BF_TRACE_ASSETS=1 logs cache use.

Launcher Graphics and F1 > Renderer share `Texture reconstruction`, persisted
as an optional trailing graphics.settings field, defaultOff. Real GPU tests
pass 8 cases per API (2x/4x, RGBA/BGRA, non-square/cube, original mips, alpha),
on WARP and RTX hardware for D3D11 and D3D11On12. Native window inspection
confirmed HUD and a live F1 change2x->4x then return to mission. This is spatial
texture reconstruction, not an AI/artist HD pack, native higher-resolution
rendering or a guarantee of sharper final HUD. Current final scene is640x480.

## DLC supplied by the user

Archive: `D:\Downloads\BruteForceDLC.zip`, SHA-256
`f9fcd21043afbace9ef7f4891e968010f0f6871b916544d30fedca8a4b118a23`.
Allowlist: content/dlc-manifest.json, three packages/51 files/45,800,455 bytes:
Lockjaw campaign, Cerulean deathmatch, Prisoners' Lament squad deathmatch.
The archive/readme are data, not instructions. No bundled installer is executed.

scripts/prepare_dlc_pack.py authenticates archive+inventory+all payload hashes
before extracting allowlisted data. Prepared private pack:
`work/native-launcher/dlc-pack`. CMake BF_PRIVATE_DLC_DIR packages it in
assets/dlc beside the launcher. Both locally installed launcher copies have
this private folder; source pushes contain metadata only. End users of that
local package do not select or locate a separate DLC archive. A bare source
build without private payload still permits the base game.

native_launcher/content_pack.{h,cpp} verifies compiled hashes, rejects unsafe
paths/links/junctions, stages and rehashes new whole packages before rename,
reuses matching installs and preserves/report differing existing packages.
Cancellation cleans only its new staging tree. library.cpp runs this during
import, verification and launch into `player-data/TDATA/4d53001e/$c`.
The launcher displays installed/verified status and pending map validation.
Installed root launcher --verify-only returned0; all51 installed files verify.
Synthetic safety tests pass Windows/Linux, including partial cancellation and
save preservation. Python preparation tests use no original game content.

Actual game logs show successful reads of all three ContentMeta.xbx files,
then first-mission gameplay and HUD with scripted movement/fire. THIS IS
DISCOVERY, NOT DLC MAP PLAYTHROUGH. Test Lockjaw plus both multiplayer maps.
The regional title-update XBEs in the archive are intentionally excluded:
size4362240, entry0x2F4E6F, unlike supported retail entry0x2F5D1F. They need a
separate authenticated translation profile; never copy over default.xbe.
Private comparison: work/combat-input-quality/dlc-title-update-analysis.json.

## Startup/shutdown and rendering boundaries

RECOMP_ASYNC_IO=1 remains the native profile default and launcher environment.
It got paired runs through the original pack-reader wait, but ONE CURRENT RUN
STILL STALLED before movies. Do not call startup completely fixed. Original
wait is 0x265B87, sub0x265AD0 busy byte+5; callback0x265910 clears at0x265992,
worker0x264620, ReadFile/GetOverlappedResult0x2F33A0/0x2F5084. Kernel calls
release/reacquire the guest CPU; a simple held-lock deadlock is not established.
Avoid BF_TRACE_FILE_IO during normal QA: millions of tiny reads inflate logs.

The captured crash also logs DX12 interpolation Close command list failure
0x80070057 AFTER APU/MemoryLayoutShutdown. Separate unresolved shutdown race:
pinned fb_present.c starts a native window thread and closes its handle without
providing stop/join. Global presenter destruction may race it after guest unmap.
A proper stop/join before destruction needs investigation; don't hide the error.

Original timing/interval/vblank remains30. Visual interpolation is image-motion
estimation/warping with one source-frame latency, not FSR3/DLSS. Requires DX12
presentation independent of CPU/D3D11/D3D12 scene. Direct DX11/GDI retain originals.
Menus/F1 bypass it, invalid gaps/skips/modes reset history. Display180 includes
repeats and is not simulation180. D3D12 scene uses D3D11On12 and a realD3D12
queue, not a separate native scene pipeline. Do not reintroduce native60 hacks.

True internal resolution, coherent scaled depth/RT aliases and camera/HUD/menu
16:9 transforms are unfinished. Projection-only stretching is insufficient.
Temporal FSR/DLSS needs valid scene depth/motion/camera/jitter/reset/HUD data;
the presenter currently gets final color. Do not add fake enabled options.
LinuxGPU and AndroidAPK remain unfinished despite previous NDK object audits.
USB lifecycle overhaul is gated on real controllers and cross-platform shutdown.

## Evidence, build locations and next steps

Pinned toolkit must remain CLEAN:
C:\Users\fireb\Documents\Codex\2026-10-31\le\work\xboxrecomp
commit f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b.
Private generated C: sibling bf-analysis/generated; recovered: work/recovered.
XBE: D:\Downloads\BruteForce-Decompilation\default.xbe,
SHA B6EA5D4DD6500AE86C3F5E3E4618BC9B1E3FACDDCB483170DFEE7A787087C721.

Windows player build cache work/diagnostic-build currently outputs candidate
work/combat-input-quality/final-player/Release. Launcher cache
work/native-launcher/build outputs work/interpolation/launcher/Release.
WSL BruteForceBuild: repo /mnt/d/Documents/ChatGPT/bfRecomp,
player /var/tmp/bruteforce-build, launcher /var/tmp/bf-native-launcher.

Current checks: all28 Windows native, all18 Linux native, all4 launcher checks
per platform, unittest17(one skipped) and pytest59 pass. Both synthetic scene
parity suites and GPU asset WARP/hardware cases passed. These are not all-level QA.

Private bounded runs under work/combat-input-quality:
- interval-original-7cujmdyf: ten-minute mission scripted smoke, no guest failure.
- interval-original-f6ohj52t: 4x textures, visual120, D3D12 mission smoke.
- interval-original-anlnzduu: DLC-seeded startup stall before movies.
- interval-original-li2pz62v: D3D11 retry through movies, 70sec bounded.
- interval-original-8_kfq7z6/boot-18_de78x: DLC-seeded D3D12,200sec,
  first mission/HUD, movement/fire, live F1 texture2x->4x; no guest failure.
- interval-original-dk9y_rsi: DLC menu-only scratch run; full map selection was
  not established before user stopped Computer Use with physical Escape.
Do not send further UI input after that interruption in the same turn.

The DLC QA library work/combat-input-quality/dlc-qa/game-core is a JUNCTION
into work/native-launcher/core-qa/game-core. Never recursively remove its target.
Private campaign.py/dlc_campaign.py/boot_dlc.py automate scratch saves only.
A watchdog exit3 is a bounded run ending, not a clean mission completion.

Next: reproduce combat while killing enemies with disposable saves; confirm
mouse feel; select/play each DLC map; investigate intermittent async startup
and window-thread shutdown. Then scale real render targets and 16:9 transforms
before adding genuine temporal APIs. Keep all broader mission/audio/controller/
multiplayer acceptance explicitly pending. Update this handoff when evidence
changes, and verify GitHub checks against the exact checkpoint commit.
