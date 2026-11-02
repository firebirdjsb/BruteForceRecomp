# Current verified state — 2 November 2026

This remains an experimental recompilation, not a fully playable release.

## Campaign rendering checkpoint

The tutorial now loads and renders Tex, terrain, vegetation and sky after the intros. The original GPU software methods were missing: operation 5 signals the D3D fence event; operations 3/4 apply the original reusable-pushbuffer fixup records and return jump; operation 10 updates the original byte accounting and continuation. The DMA walker now shares the actual CALL-state register with those operations. DMA walking is the default, with the CPU renderer retained. Original physics and simulation timing were not changed.

The 238-second `work/gameplay-priority/fixups-live-9a0natba/` run and 298-second visible `campaign-window-yf3rily3/` run reached their watchdog limits without the earlier command corruption/native crash. `gameplay-controls-oskkh3yh/frame.bmp00054.bmp` and `frame.bmp00067.bmp` show Tex moving forward in response to the runtime controller-report fixture. This establishes tutorial rendering and a movement path, not mission completion or physical-device QA. The visible run also exercised camera, jump and fire reports; their complete effects need further inspection. Nonzero game PCM was captured, but host audio reported starvation/underruns. The visible scene fell to about 3–4 presented frames/s. Solid 30 FPS, complete effects, audio quality, all levels and characters remain unverified.

The separate native candidate is `work/diagnostic-build/campaign-test/Release/bruteforce_recomp.exe`, SHA-256 `C5BB4D4121CDE7F7EC1ADDE37F5AB41738D426FCA1BB97DA73314C14EA5333DB`. Seven Windows CTests, eight Linux CTests and nine Python checks passed before repository integration. The normal launcher/player was not replaced. Permanent saves and original assets were not edited; no production service was touched.

Changed source: `src/gpu_completion.cpp`, `src/gpu_pushbuffer_fixups.cpp`, `src/gpu_event_bridge.inc`, `src/main.cpp`, `cmake/runtime_fixes.cmake`, `cmake/memory_statistics.cmake`, `cmake/pushbuffer_stream.cmake`, CMake wiring and focused GPU tests. Runtime transformations remain hash-pinned private build copies. The original reusable-buffer helper at 0x00377F00..0x00377F80 has SHA-256 `0ab8a4519944a61257e9f882992fb03c21555d692abd0353bf26c2fc3eaefe06`; its software dispatch and fence behavior were checked against the authenticated XBE and disassembly. GPU CALL register semantics were checked against xemu's public PFIFO implementation. No external binary injection was used.

The following gameplay-first investigation records the preceding failure and its diagnosis; its statement that the campaign is still entirely black is superseded by this checkpoint.

## Gameplay-first investigation

Campaign gameplay and a measured playable frame rate are the first priority. This batch changed campaign memory, original missing CRT entries and GPU command decoding; it added no launcher or enhancement features.

Changed implementation surfaces: `cmake/memory_statistics.cmake`, `scripts/recover_entries.py`, `cmake/recovered_entries.cmake`, `src/main.cpp`, `src/guest_failure.*`, `cmake/physics_trace.cmake`, `src/physics_trace.cpp`, `cmake/pushbuffer_stream.cmake`, `src/pushbuffer_segment.inc`, `tests/pushbuffer_test.cpp` and their CMake wiring. Existing in-flight menu/launcher work was preserved.

The earlier physics crash was caused by a failed allocation, not by the coordinate-transform instructions. The original tutorial loader requested 2,907,252 bytes for `objects-tutorial.ipn`, received zero, then ran its original relocation loop with a zero base. Geometry offsets consequently pointed into executable instructions. The public runtime reserved an 8-MB main stack, charged against the same 64-MB budget from which the game sized its heap. The authenticated XBE's stack commit at file+0x130 is 128 KB. The private runtime now reserves a 1-MB main stack, retaining eight times that commit; worker threads already allocate separate 512-KB heap stacks. Physical RAM remains 64 MB. Initial reported available pages rose from 11,430 to 13,222, and the original tutorial buffer allocated at `0x828E5580` in the traced run. Original relocation then produced valid geometry pointers and loading passed the previous null call. No coordinate clamp, null-call bypass or physics timestep change was applied. Evidence: `work/gameplay-priority/relocation/boot-t40n2tpe/boot.log`, `stack/boot-yz0u4yqz/boot.log` and the `[IPN]` allocation/relocation records. A separate 128-MB experiment failed during startup because the contiguous arena remains 64 MB; that experiment was removed.

Further loading exposed missing original x87 CRT branch `0x002F04DC`. `scripts/recover_entries.py` now lifts that branch and four related missing/shared entries from their exact authenticated instruction ranges. The original stack pops, constants and returns are retained. Source bodies remain private with per-body SHA-256 provenance. `cmake/recovered_entries.cmake` removes only the now-recovered `0x002F04E3` hard stop from a hash-checked private copy; ten other unresolved direct targets remain fatal. The run passed this CRT stop and failed later in native GPU command reading.

Native fault capture and the linker's map identify the next crash in `pb_walk_segment`, following a command address outside the 64-MB physical mapping (`0x8F160000`). The private segment reader now preserves unfinished method packets across contiguous submissions and rejects out-of-range CALL/JUMP targets. The compiled decoder test checks every split point of payloads that deliberately resemble control commands, valid subroutine order and an invalid physical call. Packet-state behavior was cross-checked against [xemu's FIFO implementation](https://raw.githubusercontent.com/xemu-project/xemu/master/hw/xbox/nv2a/pfifo.c); no xemu code was imported.

**Campaign still does not render playable gameplay.** The repaired segment reader completed its 153-second watchdog without the native access violation, but captures show Loading/black output and the stream still desynchronizes: word `0x3F291414` is interpreted as a jump at `0x800F60C0`. The alternate `RECOMP_PB_WALK=dma` test instead stopped progressing near the menu and ended at its watchdog. After these two GPU approaches, no further speculative renderer repair was added. The next safe step is to establish correct FIFO ring traversal and the original D3D fence/notification behavior before claiming a rendered mission. Evidence: `work/gameplay-priority/fault/`, `stream/boot-mavvq2ee/` and `dma/boot-t4h55bkz/`. Very high flip counts during desynchronization are not playable FPS.

A temporary exact texture-address optimization was tested and removed because it produced no material frame-rate improvement. Matched 55-second original START-screen tests measured median 13.8 versus 14.0 flips/s across the final 15 samples; renderer time remained approximately 48 ms per flip. These are headless title-screen measurements, not gameplay or the requested 30-FPS floor. Results: `work/gameplay-priority/performance-comparison.json` and `menu-before/`, `menu-after/`. No experimental texture-address code remains in the source candidate.

Final Windows Release and native Linux builds pass their six and seven CTests respectively; nine Python/input-validation checks pass. Exact XBE hash/layout validation passes. Blue START-screen captures were inspected. Full gameplay, physical input, gameplay audio quality and sustained frame-rate QA remain unverified. The newest engine candidate remains separate at `work/diagnostic-build/gameplay-candidate/Release/bruteforce_recomp.exe`, SHA-256 `522BA04FA95FCAFE922EC64B952CE2094784AD6CFD90F00029C3C69265F32A6E`; the launcher still points to the previous menu/input executable. The normal player, permanent saves, ISO/XBE/assets and pinned toolkit checkout were not replaced or edited. No production service was touched. Rollback for this investigation is to keep using the existing normal player and rebuild the previous source state; do not reset player-data folders.

## Native menu input and live F1 settings

Mouse hit testing now follows the original live UI objects, coordinate-owner transforms and actual letterboxed window image. Generic buttons and lists use original focus/selection routines and activate through the normal input-report path. The original on-screen keyboard uses its safe typing handlers, described below. Embedded button panels share the screen vtable, so membership in the game's actual UI-manager view list is checked before consuming pointer events. A fresh scripted Windows test hovered Options, clicked into it, and returned to the main menu using Escape. Original focus flags and screen changes were logged; the Options capture was inspected. Evidence: `work/menu-input-qa/managed/boot-bom864vm/boot.log`, `managed/frame.bmp00025.bmp` and `frame.bmp00030.bmp`.

The follow-up name-entry test exposed zero-sized key containers and delayed controller activation selecting a later hovered key. Hit testing now uses each key's original rendered child button, including the 120-pixel-wide Done key. Mouse key clicks share the bounded, object-owned native typing queue and call the original handlers after keyboard Update. A fresh scratch-profile test clicked A then B in rapid succession, visibly produced `AB`, and clicked Done to reach Profile Options. Both the name-entry and Profile Options captures were inspected and passed the blue-animation check with zero corrupted green pixels. Evidence: `work/menu-input-qa/profile-pointer-queue/boot-06ehf028/boot.log`, `frame.bmp00030.bmp`, `frame.bmp00047.bmp` and `green-check.json`. This uses the engine input fixture, not physical mouse automation. Generic menu buttons still activate through normal controller reports; on-screen keys use the safe original typing handlers.

F1 opens the native settings overlay and F1/Escape closes it. Mouse and keyboard control live mouse-menu selection, keyboard/mouse prompts, FSR 1, Auto/2x/4x/8x FSR output factors, Auto/720p/1080p/1440p/2160p window presets, mute and master gain. Audio gain ramps across one original mixer period; it does not alter the simulation clock or permanent game preferences. Settings last for the process. Requested windows fit the desktop work area. DX12-only controls report unavailable on other backends.

The original 640x480 scene remains 4:3. Fixed factors control real FSR intermediate dimensions (8x is 5120x3840), then fit that image to the window. Window presets and FSR are not native 16:9 projection/HUD support, higher scene raster resolution, temporal reconstruction or frame generation. Native widescreen and cheats remain unavailable. The overlay says so; no nonfunctional cheat toggles were added.

Common A/B/X/Y menu legend glyphs are replaced by keyboard/mouse keycaps when keyboard/mouse input is active. Physical-pad activity selects the original legends again. Unknown glyphs retain original rendering. The actual Windows framebuffer capture `work/menu-input-qa/window/window.bmp` shows LMB/ESC legends in Campaign Type. Its native DX12 log records live window and 2x/4x/8x changes. The visible test used disposable saves and ended at its watchdog limit; it is not gameplay verification. Complete physical-controller switching, special widgets, split-screen/player-specific menus and Linux interactive input still require QA.

**The requested solid 30 FPS is not achieved.** The visible menu measured roughly 14–17 FPS. Batch timing identifies about 37–42 ms per main-menu flip in the current software scene renderer. Opaque/source-over SIMD optimizations passed differential float-color checks but did not remove that bottleneck. No simulation speed, physics timestep, movie asset or claimed/generated frame count was changed.

New source surfaces are `src/game_menu_input.cpp`, `src/ui/`, `cmake/game_menu_input.cmake`, `src/render_blend.h`, `cmake/render_profile.cmake`, related guest/window/audio/graphics integration and focused tests. Current campaign-failure tracing is in `src/guest_failure.cpp`; optional original center-transform diagnostics are separately guarded by `BF_PHYSICS_DIAGNOSTICS` and canonical source hashes.

## Green menu regression

The corruption was reproduced in the raw renderer framebuffer with both filtered and full-disc assets. FFmpeg decodes the original `menuBack.bik` correctly; DX12/FSR and demo removal are not the cause. Tracing showed the original Bink loader's 458,752-byte streaming allocation returning zero, followed by a 14,980-byte read into a null destination. Empty YUV planes converted to approximately RGB(0,154,0).

The runtime's `MmQueryStatistics` usually substituted half of 64 MB for host available RAM. Brute Force sized its own heap from that artificial 32-MB answer. `cmake/memory_statistics.cmake` now routes the bridge to guest-reservation accounting in a hash-checked private runtime copy. It charges image, runtime/main-stack reservations, low-heap high-water span, contiguous allocations and reserved GPU pages, with saturating arithmetic. This is conservative budgeting for the current runtime, not complete Xbox physical/virtual memory management; virtual reservations and oversized runtime stacks can still overstate physical usage.

The observed initial answer changed from 8,192 to 11,430 available pages. The original movie buffer then allocated successfully at guest `0x81E2DCA0` in the traced run. The null read disappeared and the original blue animation rendered across fresh launches. No video asset, movie converter, simulation clock or demo-visibility code changed in this fix.

`scripts/check_menu_frames.py` rejects the previous green capture (52.1% corrupted green pixels). Three selected corrected captures from two fresh processes passed with zero such pixels, substantial blue coverage and differing hashes. Private captures and reports are under `work/menu-regression/`. These headless rendering checks do not establish physical input or full-game validation.

## Campaign and input boundary

The campaign-failure paragraphs in this section record earlier traces. The gameplay-first checkpoint above establishes the allocation failure and supersedes their provisional cause and blocker descriptions.

Corrected-build tests passed the earlier campaign-creation null call at return VA `0x0010073B`, played `Campaign_Intro.bik` and `Tex_Intro_Final.bik`, reached `mb01.bik`, and began loading tutorial assets using disposable saves. The extended Release run then stopped with exit 21 at a different null call, return VA `0x002B74D6`, during tutorial loading. Log: `work/campaign-memory-qa/extended/boot-2e_ya4hd/boot.log`. Original caller `0x002B7490` has 80-byte body SHA-256 `719d37271436979882ffc1da07be0a657715042f2f47c94ef8719355c37eca33`. Its observed structure had an infinite float at +0x18 and null owner at +0x20; the underlying cause is not established. No bypass or guessed pointer repair was added. Playable campaign gameplay remains unverified.

Native name entry is now implemented through Windows window messages and Linux SDL text events. `src/text_events.*` queues bounded input across the window/game threads. `src/game_text_input.cpp` validates the live original keyboard object and resolves its actual key descriptors and captions; `src/game_text_guest.c` invokes the original focus/activation/Back handlers and preserves the guest register/FPU state after the original update. `cmake/game_text_input.cmake` retains the original generated bodies in a hash-checked private copy. No original asset or text buffer is directly patched.

Printable characters supported by the current original key page, Space, Backspace/Delete, Enter for Done and Escape for the game's Back action are handled without also applying keyboard-to-controller actions. Arrows and physical controllers keep the original grid navigation. Name entry works independently of the launcher's keyboard-test checkbox; raw mouse capture is released while the dialog owns typing. Normal-keyboard letters remain uppercase, as in the game. Character-page/layout coverage, physical-keyboard QA and Linux live text-entry QA remain pending.

Two fresh Windows headless runs used only disposable saves. The native input queue drove the original handlers to visibly produce campaign name `CODEX9ZQ`, enforced the original 12-character maximum, and used Enter to reach Select Difficulty. A second run produced profile name `TYPEZQ` and Escape reached Profile Options through the original Back behavior. Evidence: `work/text-input-qa/edit-run/boot-z3n5ao8a/boot.log`, `edit-run/frame00131.bmp`, `profile-run/boot-ii51dk2g/boot.log`, `profile-run/frame.bmp00516.bmp` and `frame.bmp00629.bmp`. These tests feed the same queue as native events; they do not establish manual physical-keyboard or gameplay validation. Completed-flip profile captures passed the green-menu check with zero corrupted green pixels.

Generic mouse-menu selection is implemented and the main-menu Options transition was exercised as described above. Full menu/widget and physical-controller coverage, the missing character preview and the requested menu FPS floor remain unresolved.

A fresh UI-build campaign run reproduced the same null call at return `0x002B74D6`, reached through `0x002C6D49`, after the original intro movies. The diagnostic follow-up found an incoming physics position of approximately `(2.28e33, 1.10e15, -9.96)` and infinite derived distances, while the configured minimum timestep remained finite (`1/66`). The producer frame identifies the path `0x00159660 -> 0x002B39F0 -> 0x002C6A00`; the original transform translation is finite, while the local offset used before the physics update is invalid. The producer is still under investigation. No null-call bypass, arbitrary coordinate clamp or physics alteration was applied. Evidence: `work/menu-input-qa/campaign/boot-_w_jb1jq/boot.log`, `campaign-diagnostic/boot-vgsp1lrx/boot.log` and `campaign-followup/`.

The isolated `proxy-source` diagnostic run further identifies the invalid geometry vector pointer before physics setup. Original `0x001590A0` obtains it through entity+0x6C -> model data -> +0x24 -> +4 -> +8 and passes it at original return `0x0015911A` into `0x00158CD0`. That constructor stores the supplied pointer in its two-DWORD proxy geometry; original getter `0x002B97D0` later copies its three floats unchanged. For the failing actor the supplied pointer is `0x000479D0`, and its bytes match original executable instructions. At the same offset in the extracted original `objects-tutorial.ipn`, the three floats are finite (approximately -5.02, 6.19, -0.034). Several other proxies likewise contain small file-like offsets. This supports a missing resource-pointer relocation as the next hypothesis; the responsible loader and correct buffer base are not yet established. No relocation was guessed or written. Evidence: `work/menu-input-qa/proxy-source/boot-1fi4995u/boot.log`, exit 21. The optional probes are OFF in the player build.

## Application, platform and validation checkpoint

- The current launcher is a native C++ SDL2/Dear ImGui application. A real ISO import retained 497 core files and excluded 188 demos/update/promotional files. Authentic branding and both executable icons are extracted locally and remain private. The old browser launcher was removed.
- Windows has actual hardware DX12 presentation with D3D11/GDI fallback and optional AMD FSR 1 EASU/RCAS scaling. Hardware readback and D3D12 debug-layer checks passed, including resize/shutdown after fixing the presentation fence. Scene rasterization remains on the CPU. DLSS, temporal FSR, 120+ FPS interpolation, HD replacements and complete widescreen validation remain pending.
- Native APU/XAudio2 produces measured nonzero 48-kHz stereo PCM. Timing/tracing changes improved observed underruns; crisp and synchronized gameplay audio remains unverified.
- The launcher has the local Windows Release menu/F1 and ordered mouse-key build at `work/diagnostic-build/Release/bruteforce_recomp.exe`, SHA-256 `FE8C754D94BF78F106F6C9AF1388020BA423BC7E7DB63807E1E08AFB3B9DA241`. The immediately previous player is retained in `work/menu-input-qa/rollback-before-key-clicks/`; the earlier typing/menu-memory build remains in `rollback-before-menu-ui/`. Release builds contain no separate PDB. Physics diagnostic builds remain separate under the private diagnostic-build output folders.
- Nine Windows Python tests and all five Windows CTests pass: import guard, guest page accounting, reference-vs-SIMD blending, native text events and native UI ownership/coordinates/F1/live controls. Original XBE hash/layout validation passes. Updated Options captures pass the green/blank-frame check with zero corrupted green pixels. Hardware DX12/FSR readback and debug-layer tests pass, including live 2x/4x/8x/Auto changes, color, alpha, aspect preservation, resize, downscale and shutdown.
- Native Linux host rebuilds with this change; its six CTests pass (the five shared tests plus POSIX MMIO vectors). SDL's already-scaled logical mouse coordinates are used without applying window scaling twice. Earlier WSLg inspection reached Loading; gameplay/menu/text-entry parity remains unverified. The NDK r30 ARM64 object audit is still not an integrated Android application or S24 Ultra APK.

All missions, modes, characters, abilities, enhancements/cheats and expanded P2P multiplayer remain separate incomplete targets in `FEATURE_TARGETS.md`.

The native launcher is open using the QA workspace and normal player executable. Automated tests used disposable saves; a separate visible-window run also used scratch saves. Original ISO/XBE/assets, permanent player saves and the pinned toolkit checkout were not edited; no production service was touched. No game binary, asset or private analysis dump is committed. For this mouse-key change's rollback, close the game and restore `bruteforce_recomp.exe` from `work/menu-input-qa/rollback-before-key-clicks/` to `work/diagnostic-build/Release/`, preserving player-data folders. The earlier menu/F1, typing and pre-menu-memory-fix backups remain in their separate work folders.

## Historical notes — superseded by the checkpoint above

The following notes describe earlier work. Their launcher type, file counts, build status and validation counts are historical.

This is an experimental recompilation project, **not a playable Windows/Linux/Android release**.

## Current progress

The desktop launcher now imports the user's ISO, reports extraction progress, validates the native inputs and starts the Windows executable. The real disc imported **685 files** into a separate QA library; Start launched the original Start/Demos menu, and closing the game returned the launcher to Ready. The library survives launcher restart and is revalidated before use. The Windows native file chooser, long ISO path, refresh/session persistence and responsive layouts were inspected. Styling uses authored vector/CSS textures, with no original game artwork bundled. Native Android launcher/application integration remains pending.

Native Linux x64 now builds and visibly renders the original **Loading** screen through SDL2 under WSLg. It does **not** reach a verified game menu. Fixed the POSIX memory reservation so contiguous RAM, NV2A, MCPX and tiled aliases fit inside a reserved 4-GB guest virtual window. Previously the OS placed RAM near the top of user address space and the device mappings failed. The tiled/contiguous alias is checked by the runtime. Added the POSIX device fault handler and reused the public runtime's Linux x64 MMIO decoder, with eight compiled instruction/context checks. The original toolkit remains clean; three source adaptations are made only in private build copies.

Enabled the runtime's existing validated model for events built directly by the XDK. The D3D notification event at live device +`0x195C` (`0x0037D69C` in the observed run) now has a host event with its actual type/state, instead of being passed as a host handle. On Linux that invalid handle previously faulted inside `pthread_mutex_lock`. Guest waits are preserved. SDL now owns the process main thread and runs the guest on the runtime's dedicated thread.

The approved **NDK r30, 30.0.16248370**, is installed under `work/tools/android-ndk-r30` (Linux host). The official archive checksum matched before extraction. All **227 generated/recovered C translation units compile to Android ARM64 objects** at API 28. This is a translation portability audit: it does not link the Android renderer/runtime, produce an APK, prove instruction equivalence, or establish Galaxy S24 Ultra support. The main application target still rejects Android configuration until that integration exists.

The next measured Linux blocker is the zero-delay yield loop at original `0x00265B80`, waiting for an asynchronous read object's byte at +5 to clear while loading common resources. A native GDB sample identifies `sub_00265AD0 -> sub_00254C60 -> sub_00255330`; worker threads wait on their request events. No flag was forced clear and no successful read was fabricated. The 60-second run ended at the test limit. Windows startup/menu input tests also remain bounded runs, not gameplay validation.

Current checks: **12 Python tests pass on Windows and Linux**, including supported/missing/corrupted inputs, extraction path restrictions, launcher request isolation, library containment and launch gates. Windows Debug builds and its compiled import test pass. Linux builds and both CTests pass (import guard and eight MMIO vectors). NDK ARM64 object build passes. Current Release, physical controllers, full mouse/keyboard gameplay, audio quality, all missions/characters, enhancement UI, cheats and expanded P2P remain unverified or unimplemented.

Build-environment changes are local: a separate `BruteForceBuild` Ubuntu WSL distribution and project-local NDK were added. Existing Kali's public archive keyring was refreshed from its official source with a verified checksum and its original retained in `work/kali-archive-keyring.original.gpg`; package indices were updated, but the large simulated package migration was not installed. No original game, permanent save or production service was modified. Source rollback is to revert the relevant commit and rebuild; QA imports/builds remain private under `work/` and the isolated distro. Do not remove player data as part of a routine source rollback.

## Earlier Windows startup batch

The Windows Debug build visibly renders the real Start/Demos screen. A scripted USB controller-report test also reaches the Campaign Type screen showing Campaign, Quick Campaign, Host Session and Join Session. Both screens were inspected directly. This establishes menu rendering and a controller-report navigation path, not completed gameplay, verified physical-controller support, mouse aim, working networking or audio quality.

Fixed and observed in this batch:

- Prevented the look-behind import scan from overwriting two ordinary instruction words. The bridge now reports **147/147 resolved, 147 bridged, zero stub imports**, rather than 149/147 with two invalid entries. An 11-vector compiled guard test passes.
- Connected the existing GPU semaphore-release handler to the live D3D semaphore pointer. The original wait remains intact; completion advances from the formerly stuck value 3 to 105/115 and further as real commands are consumed.
- Recovered measured missing `MU_Init` at `0x0037EE7C` from its authenticated 268-byte body. Generated code remains private; original body SHA-256: `5fa2203370ec405e1dc3ebd5668a8f80fe3b30332ba5d7fde4c68ec8abe54c2c`.
- Made successful per-read payload tracing opt-in while retaining failure diagnostics. Startup now loads common and splash-screen assets and renders menus during the bounded run.
- Initialized the guest USB model with its required Windows device-fault handler. Reports advance and scripted buttons navigate menus. The verified USB CLI/STI pair updates guest IF and masks/restores runtime guest IRQL around the original frame-counter snapshot.
- Added bounded native-PC/D3D-state sampling. Watchdog expiration is labeled `BOUNDED_RUN_ENDED`; expiration alone does not prove a frozen game.

The four offline/native validation tests and compiled import guard test pass. Updated source has been built in Debug; the new Release build was interrupted to prioritize bring-up and is **not claimed verified**. Physical controllers, full native mouse/keyboard support, campaign gameplay, audio quality, every level/character, enhancement UI, cheats and expanded P2P multiplayer remain pending. FEATURE_TARGETS.md records the full requested scope.

All runs used fresh scratch saves. Original ISO/XBE files, existing decompilation work, installed games and permanent saves were not modified. The pinned public runtime remains clean: replacements are generated only inside the private build directory. No production deployment occurred. Reverting this source batch and rebuilding provides rollback.

## Historical baseline from 31 October

**Current result:** the user-approved native file-validation service passes and startup proceeds beyond the DVD check. A verified `WBINVD` site now uses a host-memory barrier. With the public pushbuffer executor enabled, the runtime opens a 640x480 framebuffer window, but the GPU report records zero drawn triangles and zero pixels. Startup stalls during graphics initialization and the 13-second watchdog terminates it. No menu or gameplay has been verified. The DVD failure described below is the preserved baseline, not the current blocker.

## Completed evidence

- Verified the supplied Brute Force XBE against its exact SHA-256, title ID, entry point and all 30 section ranges.
- Read the supplied ISO, extracted 685 files into a private working directory, and verified its executable matches the original XBE. No disc executable was launched during extraction.
- Built the pinned public XboxRecomp runtime libraries for Windows x64.
- Generated 22,307 candidate functions, with ABI analysis, into 224 C chunks (2,456,450 generated lines). The generator reported zero failed translations, **not zero semantic errors**.
- Audited 11 unresolved direct-call targets and 34 unsupported-instruction markers. Unresolved generated returns were converted into explicit diagnostic stops. Original generated output is retained privately.
- Linked the complete Windows diagnostic executable with MSVC x64, including generated code, the runtime and the C++ host.
- Ran a bounded diagnostic startup using disposable saves. It reached 23 kernel calls, then requested `HalReturnToFirmware(2)` before rendering a menu. The runtime exits with code 0 on that path; the boot-test script explicitly identifies it as `EARLY_FIRMWARE_RETURN`.
- Verified that the corrected tracing wrapper preserves the baseline's ordered sequence of 23 kernel ordinals. This is bounded trace evidence, not proof of full equivalence.
- Passed exact-input validation, single-byte corruption rejection, unresolved/failed-translation fail-stop checks, audit idempotence, and Python syntax checks. The extraction helper refused an existing destination as intended.

## Preserved baseline: original DVD startup blocker

The guest's original media helper at `0x002F801F` sends a 44-byte SCSI pass-through request through `NtDeviceIoControlFile`, IOCTL `0x0004D014`:

1. MODE SENSE CDB `5A 00 3E 00 00 00 00 00 1C 00 00 00` receives the runtime's modeled mode page.
2. CDB `AD 00 FF 02 FD FF FE 00 06 64 00 C0` requests 1,636 bytes. The public runtime has no matching response implementation and returns success without filling that guest data buffer. The sampled response bytes remain zero.
3. The guest retries that request five times, fails its subsequent validation, and takes its own firmware-return path. No menu or gameplay has been verified.

The helper was checked directly against the original XBE and Ghidra export: body SHA-256 `f44421e4a03680a1170b1f18f84cb281ad2a3878831cc18e5fda23aa0c15cad2`; its original exit instruction is a plain `ret` at `0x002F81E7`.

The user explicitly chose native local-file validation. The implemented service checks the XBE and two required asset archives against their measured sizes and SHA-256 hashes, fails on damaged/missing/unsupported input, and returns the original helper's status/stack contract. The original generated helper body is preserved under a separate symbol; `BF_ORIGINAL_DVD=1` selects it. No unconditional-success replacement was installed. `--validate-only GAME_FOLDER` runs those checks without guest execution.

One intermediate trace run was discarded: the runtime uses a shared kernel dispatcher with lookup-time ordinal selection, and the first tracing wrapper cached that dispatcher without reselecting the ordinal. The corrected wrapper selects the target immediately before calling through and reproduces the original kernel-call sequence. Only the baseline and corrected runs support the observations above.

## Platform and fidelity limitations

| Surface | State |
| --- | --- |
| Windows x64 | Current Debug build and native file validation pass; original menus and launcher-to-game flow observed. Gameplay remains unverified. |
| Windows optimized build | Current source batch not verified in Release. Earlier Release boot evidence describes the historical startup blocker only. |
| Linux | Native x64 build, validation and two CTests pass; SDL loading screen observed under WSLg. Asynchronous read wait prevents a verified menu. |
| Android | NDK r30 installed and all 227 translation units compile for ARM64. No linked Android application, APK or S24 Ultra device test yet. |
| Controller, sound, rendering, missions, save/reload | Not verified. |
| Original semantic fidelity | Not verified. Discovery, translation and compilation are distinct from runtime correctness. |

Comparison with the existing Ghidra export found 13,684 shared entry addresses out of 13,694, ten Ghidra entries absent from the new discovery, and 8,623 new candidate entries. None of those counts proves function-body boundaries or semantics. See the machine-readable reports for the ten entry gaps and generated-file digests.

## Scope, files and rollback

Created this standalone project's CMake configuration, modular C++ host/tracing/native-media code, verification/extraction/translation/build/boot scripts, pinned tool/device profiles, documentation, tests and reports. No game binaries or assets are prepared for publication.

The user subsequently requested a private GitHub repository. Only the source project is prepared for that repository; the original game files, generated code and local builds remain excluded.

The four local tests passed with both the Windows Debug and Release executables and private inputs: supported input accepted, missing asset rejected without guest execution, a single-byte asset mutation rejected, and unresolved/failed translation output hardened with idempotent audit behavior. These are offline/input checks, not gameplay QA.

Original ISO/XBE files and existing BruteForceDecomp sources were not edited by this work. Installed games and permanent saves were not used. No external process injection, Windows kernel-driver work, network capture or production mutation occurred.

Private extraction, generated game code, build output and boot logs live under this chat's `work/` directory, outside the source deliverable. The source ZIP excludes executable game code and assets. Rollback is to remove this standalone source prototype and its specific private work directories after retaining useful logs. No production deployment occurred and no original game installation needs restoring.
