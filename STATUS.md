# Verified state — 1 November 2026

This is an experimental recompilation project, **not a playable Windows/Linux/Android release**.

## Current progress

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
| Windows x64 | Diagnostic executable linked; native media validation passes; startup reaches graphics initialization and stalls before menu. |
| Windows optimized build | Current native-media/cache-service source linked successfully with MSVC x64 Release. Its bounded boot reproduced the graphics-initialization stall and watchdog exit code 3. |
| Linux | Native runtime/game not built or run. A read-only WSL prerequisite probe found no usable build-tool output and `pkg-config` reported permission denied. No Linux installation/configuration was changed. |
| Android | Not built, installed or run. Canadian Galaxy S24 Ultra is the requested first device. A public SDL2/Vulkan runtime candidate and Android application reference were inspected and pinned as research snapshots. |
| Controller, sound, rendering, missions, save/reload | Not verified. |
| Original semantic fidelity | Not verified. Discovery, translation and compilation are distinct from runtime correctness. |

Comparison with the existing Ghidra export found 13,684 shared entry addresses out of 13,694, ten Ghidra entries absent from the new discovery, and 8,623 new candidate entries. None of those counts proves function-body boundaries or semantics. See the machine-readable reports for the ten entry gaps and generated-file digests.

## Scope, files and rollback

Created this standalone project's CMake configuration, modular C++ host/tracing/native-media code, verification/extraction/translation/build/boot scripts, pinned tool/device profiles, documentation, tests and reports. No game binaries or assets are prepared for publication.

The user subsequently requested a private GitHub repository. Only the source project is prepared for that repository; the original game files, generated code and local builds remain excluded.

The four local tests passed with both the Windows Debug and Release executables and private inputs: supported input accepted, missing asset rejected without guest execution, a single-byte asset mutation rejected, and unresolved/failed translation output hardened with idempotent audit behavior. These are offline/input checks, not gameplay QA.

Original ISO/XBE files and existing BruteForceDecomp sources were not edited by this work. Installed games and permanent saves were not used. No external process injection, Windows kernel-driver work, network capture or production mutation occurred.

Private extraction, generated game code, build output and boot logs live under this chat's `work/` directory, outside the source deliverable. The source ZIP excludes executable game code and assets. Rollback is to remove this standalone source prototype and its specific private work directories after retaining useful logs. No production deployment occurred and no original game installation needs restoring.
