# Verified state — 1 November 2026

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
