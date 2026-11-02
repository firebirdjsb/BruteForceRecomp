# Brute Force recompilation prototype

Goal: a faithful native port of the original Xbox Brute Force (2003) to Windows, Linux and Android. This is a separate experimental project. It does not modify BruteForceDecomp or replace its evidence-led source reconstruction.

**This is not a finished or verified playable port.** Translation, compiling, booting, rendering and completing a mission are separate gates. Generated code and inferred calling conventions must be checked against the original executable and measured execution.

See [STATUS.md](STATUS.md) for completed builds, the observed DVD-service startup blocker, validation evidence and remaining platform work.

## Desktop launcher

On Windows, open **Launch Brute Force.cmd**. On Linux, run `sh launch.sh`. Python 3.10+ and an existing native build are required. The launcher finds the toolkit/executable in this checkout's known CMake build directories, or accepts explicit paths:

```powershell
python -m launcher --toolkit "PATH_TO_PINNED_TOOLKIT" --exe "PATH_TO_NATIVE_EXECUTABLE"
```

The Brute Force themed command center opens in your local browser. Choose your ISO with **Browse files**, select **Import disc**, then **Start Game** after validation. It extracts into `.local/launcher/imports`, keeps saves in its separate `.local/launcher/player-data` profile, and writes per-session game logs. Imported data is revalidated on restart and before launch. The real 685-file disc import and Windows native menu launch were tested. The UI uses original vector/CSS artwork; game textures/assets are never distributed with the launcher.

Only the authenticated local UI is exposed on loopback; game files are not HTTP assets. No upload, account or game-file download is involved. Linux's native chooser needs `python3-tk`; a full ISO path can also be pasted. This is currently a **desktop launcher**, not the pending native Android application UI. The keyboard checkbox enables the diagnostic controller mapping, not completed native mouse aim or rebinding. The Start button launches an experimental host; it does not imply playable campaign support.

The current Debug build now renders the original Start screen and reaches the Campaign Type menu through scripted controller reports. It is not yet a verified playable release. [FEATURE_TARGETS.md](FEATURE_TARGETS.md) records the requested enhancement menu, full mouse/keyboard and controller support, cheats, HD/widescreen and expanded multiplayer scope.

Startup fixes apply only to hash-checked build copies of the public runtime; its checkout stays clean. The semaphore target is derived from the live D3D device, and the completion word changes only when the renderer consumes an actual semaphore-release command. `BF_TRACE_FILE_IO=1` restores successful-read payload tracing; failures remain visible by default.

For an existing generated directory, run `scripts/recover_entries.py --xbe FILE --toolkit TOOLKIT --functions FUNCTIONS_JSON --output RECOVERED_DIR` and pass `-DBF_RECOVERED_DIR=RECOVERED_DIR` to CMake. The normal `generate` pipeline does this automatically. Recovery verifies the missing callback's original bytes and emits its complete lifted body privately. The public repository holds no recovered game-code body.

Diagnostic input options: `--keyboard` enables the keyboard test mapping (Z/X/C/V = A/B/X/Y, Enter = Start); `--mouse-keyboard` also enables the existing raw-mouse adapter, with F9 capture toggle. This adapter is a bring-up path, not proof of the requested native mouse/keyboard experience. `--sample-startup` enables bounded Windows main-thread sampling; `--gpu-trace` records submissions. USB and vblank models are enabled by the boot-test launcher. Physical controllers, mouse aim and full gameplay still require validation.

## Exact input

- Retail North American title `4D53001E`, version 6, XDK 5233.
- XBE SHA-256: `B6EA5D4DD6500AE86C3F5E3E4618BC9B1E3FACDDCB483170DFEE7A787087C721`.
- Guest entry `0x002F5D1F`; 30 sections; 147 kernel imports.
- The ISO's root executable was independently hash-checked against the supplied executable.

Xbox executables use XBE headers, not Windows PE headers. `scripts/verify_offsets.py` validates the actual format, image bounds and this exact build. Guest virtual addresses in the host identify original code; host memory addresses are derived by the runtime loader.

## Integration progress and validation

The [upstream feature progress matrix](docs/UPSTREAM_INTEGRATION_PROGRESS.md)
tracks renderer/HLSL work, synchronized presentation, texture decoding and
shader caching, verified mono-HRTF audio, 1440p/4K scaling policies, USB
lifecycle safety and actual frame benchmarking. A successful synthetic compile
does **not** imply that DirectX 11/12 game rendering or full game boot passes.

To inspect opt-in distinct-image frame statistics, set
`RECOMP_FRAME_STATS` to a writable CSV filename while running your approved
local original game, then run `python tools/frame_bench.py <trace.csv>`.
Unidentified GPU-owned frames remain unknown, not counted as distinct.

## DirectX 12 compatibility foundation

An isolated, **backend-neutral guest GPU fence timeline** and an optional
Windows D3D12 WARP/D3D11On12 compatibility fixture are now included. The
future DX11 and DX12 renderers can reuse the original-Xbox synchronization
semantics, same-resolution frame comparison, and original content validation
without duplicating title-specific patches.

**No DX12 gameplay renderer is built or selected yet.** Microsoft Direct3D12
requires explicit command queues, resource transitions, descriptor management
and confirmed fence completion. Passing the WARP interoperability smoke test
does not establish rendered gameplay. Details are in
[DX12 compatibility](docs/DX12_COMPATIBILITY.md).

## Renderer frame comparisons

The independent MIT-licensed frame-difference tool can measure captured
CPU/reference and future GPU image differences without game data in Git.
Install optional tools with `python -m pip install -r tools/analysis-requirements.txt`,
then run `python tools/compare_frames.py reference.png candidate.png
--diff mismatch.png`. Its JSON result reports changed pixels, thresholds and
regions; it does not prove images are from the same game state.

## Demo content policy

New ISO imports deliberately skip the separately identified Tao Feng and
MechAssault demo payload directories (`fx/`, `Mech/`) and the root
`demos.xbe`. The original game's other files stay intact, and the
importer never executes or changes the source disc. **Existing imported
libraries are not silently altered**: reimport your disc to use the
new demo-exclusion policy. The built-in Demos entry on the original game's
menu is a separate guest-code question and is not claimed removed here.
Do not distribute extracted game files or substitute another title's data.

## Evaluated external recompilation work

The generic texture-math optimization is proposed on a separate review
branch, not assumed to prove improved performance or runtime correctness.
See [Tzeentchnet reuse assessment](docs/TZEENTCHNET_REUSE.md) for the
upstream D3D11/GPU, audio, rendering-scale and validation candidates, XBE
fingerprint mismatch, license boundaries and required regression checks.
Adapted code attribution is recorded in [third-party notices](THIRD_PARTY_NOTICES.md).

## Public tools

| Repository | Role / assessment |
| --- | --- |
| [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp) | Selected experimental lifter/runtime. Pinned in toolchain.json; desktop Windows and Linux code exists. |
| [capstone-engine/capstone](https://github.com/capstone-engine/capstone) | x86 decoding; Python dependency pinned to 5.0.7. |
| [XboxDev/ghidra-xbe](https://github.com/XboxDev/ghidra-xbe) | Optional independent static inspection. Existing Ghidra exports remain evidence, not executable replacements. |
| [xemu-project/xemu](https://github.com/xemu-project/xemu) | Optional reference execution and hardware research; runtime components reused by XboxRecomp have separate licenses. |
| [libsdl-org/SDL](https://github.com/libsdl-org/SDL/tree/SDL2) | Existing upstream Linux input/window/audio dependency; possible Android foundation. |
| [TGP482/XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp) | Alternative assessed; its documented playable renderer is Windows D3D12. Not selected for the cross-platform prototype. |
| [xenophobentx/xboxrecomp, takedawn branch](https://github.com/xenophobentx/xboxrecomp/tree/takedawn) | Promising reusable SDL2/Vulkan runtime for Windows, Linux and Android. Source inspected, not yet integrated or validated with this XBE. Pinned research snapshot in platform-references.json. |
| [xenophobentx/takedawn](https://github.com/xenophobentx/takedawn/blob/main/docs/android.md) | Android application/build reference using that runtime. Its documented Burnout device results are upstream claims, not Brute Force evidence. |

Upstream README claims are not proof that Brute Force works. Preserve upstream notices, including separately licensed xemu-derived components. Audit the pinned LICENSE, NOTICE and LICENSES directory before distributing linked binaries.

## Reproduce

Python 3.10+, Git, CMake 3.20+ and a native compiler are required. Use an isolated Python environment and install `capstone==5.0.7`. The pipeline clones the pinned public toolkit into its work directory unless `--toolkit` names an existing clean pinned checkout. All stages validate the XBE first. It does not upload game files.

```powershell
python scripts/pipeline.py verify --xbe "D:/your-game/default.xbe"
python scripts/pipeline.py analyze --xbe "D:/your-game/default.xbe"
python scripts/pipeline.py generate --xbe "D:/your-game/default.xbe"
python scripts/pipeline.py runtime --xbe "D:/your-game/default.xbe"
python scripts/pipeline.py build --xbe "D:/your-game/default.xbe"
```

On Linux use `python3`; install the dependencies described in the pinned toolkit's `docs/GETTING_STARTED.md` (SDL2, libepoxy, OpenSSL, ffmpeg development packages). Native x64 builds/validation and a visible SDL loading screen are now verified under WSLg. Menu/gameplay remains blocked by the asynchronous read wait recorded in STATUS.md.

Each stage logs to `.local/` by default; `--work-dir` selects another private directory. Generated source, disassembly databases, the XBE and extracted assets must stay private and outside commits/releases. The pipeline uses all executable sections, includes ABI analysis, and does not silently substitute cdecl for a missing analysis stage. Its inferred boundaries and ABI are still unverified.

For a diagnostic game build with symbols and unoptimized generated code, add `--configuration Debug` to the `build` stage. The project removes the default MSVC C Debug `/RTC1` flag because upstream runtime libraries explicitly enable `/O2`; those two flags cannot be combined. Optimized performance and Debug boot results are separate checks.

To extract a disc into a new directory after the toolkit is available:

```powershell
python scripts/extract_disc.py --iso "D:/your-game/game.iso" --toolkit .local/xboxrecomp --output .local/game-data
```

Extraction checks directory traversal, duplicate paths, free space and the executable hash. It copies disc files without executing dashboard updates, demos, or downloaders. It refuses to overwrite an existing output directory.

To try building the experimental host after generating:

```powershell
cmake -S . -B .local/game-build -DXBOXRECOMP_DIR="ABSOLUTE_PATH_TO_TOOLKIT" -DBF_GENERATED_DIR="ABSOLUTE_PATH_TO_GENERATED"
cmake --build .local/game-build --config Release --parallel 4
```

Replace the two path arguments with real absolute directories. Compilation failures are blockers to investigate, not evidence that the game is close to playable. The user-approved native media service checks the exact SHA-256 and sizes of the XBE, `data/common.tgz`, and `data/tutorial.tgz`, replacing the hardware-only DVD check. It does not claim to audit every asset. The original helper body is retained and `BF_ORIGINAL_DVD=1` selects it for comparison. No gameplay overrides or guessed global initialization have been added.

The authenticated `WBINVD` site uses a coherent host-memory barrier because the port does not model Xbox CPU cache state; hardware cache/timing equivalence remains unverified. Other unsupported instructions and unresolved calls stop with diagnostics. Generation audits upstream output and converts unresolved direct targets and failed translations into explicit diagnostic stops, retaining original output privately for comparison.

Use `scripts/boot_test.py --exe PATH --game-dir PATH --work-dir PATH` for a bounded diagnostic boot after linking. It checks the full XBE hash, creates a fresh save directory, captures logs and terminates after 15 seconds by default. A zero exit code or time limit is not gameplay verification.

The boot test enables the runtime's pushbuffer executor and framebuffer window. `--gpu-trace` adds GPU submission diagnostics. It returns failure for an early firmware return, watchdog stall, timeout or nonzero process exit. Use the program's `--validate-only GAME_FOLDER` option to check supported local inputs without executing guest code. If preparing an older generated directory manually, run `scripts/prepare_native_media.py --xbe FILE --generated DIR`, then `scripts/audit_generated.py DIR`, before building.

## Platform gates

NDK r30 is installed locally with user acceptance of its terms. `android-toolchain.json` pins its revision and official archive checksum. A separate object-only audit compiles the existing generated game code for real Android ARM64 without pretending to be an application:

```sh
cmake -S cmake/android-audit -B .local/android-audit -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DXBOXRECOMP_DIR="$TOOLKIT" -DBF_GENERATED_DIR="$GENERATED" -DBF_RECOVERED_DIR="$RECOVERED"
cmake --build .local/android-audit --parallel 4
```

All 227 translation units pass this audit with NDK r30 Clang 21. Compiler warnings still require semantic review. There is no APK or linked Android graphics/audio/application runtime yet; this audit cannot be installed on the S24 Ultra.

| Target | Required evidence |
| --- | --- |
| Windows x64 | Link host, validate generated code and unresolved targets, bounded boot, rendered menu, controller navigation, sound, mission loading, gameplay and isolated save/reload. |
| Linux x64 / ARM64 | Repeat Windows gates using the upstream SDL2/OpenGL renderer; validate case-sensitive asset paths, signals, threading and floating-point behavior. Do not assume Wine tests prove native support. |
| Android ARM64 | Evaluate the pinned Takedawn Vulkan/SDL2 runtime candidate and register-model compatibility first. Integrate generic platform code without Burnout-specific addresses/fixes. Add NDK build, app lifecycle, game-data import, app-private saves and controller handling; validate background/resume, audio, memory mapping and signals. Then repeat mission QA on a real device. |

Android configuration deliberately fails with an actionable message until platform integration is implemented. A renamed desktop executable or empty APK is not an Android port. Prefer reusing the public SDL2/Vulkan implementation if its compatibility audit passes; a new renderer or wholesale SDL3 migration is not the first step. The selected baseline toolkit's desktop GL 3.3+libepoxy backend is not an Android GLES backend. Device/driver coverage and game-specific performance remain unverified.

The first Android test device requested by the user is the **Canadian Galaxy S24 Ultra**. Samsung confirms its Snapdragon 8 Gen 3 for Galaxy chipset in its [Canadian announcement](https://news.samsung.com/ca/enter-the-new-era-of-mobile-ai-with-samsung-galaxy-s24-series). `device-target.json` records the target and pending device checks. Begin with the installed driver, guest-native render scale, original aspect ratio and original game timing; no performance claim or mandatory custom driver is justified yet.

Preserve Xbox 32-bit guest pointers on all 64-bit hosts. Validate x87 precision/rounding, SSE lanes, overlapping memory writes, TLS, exceptions, indirect calls, library code and instruction coverage. Use exact original-body evidence where available. Do not replace missing behavior with success-returning stubs.

## Checks

Run `python -m unittest discover -s tests -v`. Native file-validation tests run when `BF_TEST_EXE` names the locally built executable and `BF_GAME_DIR` names the private extracted game folder; otherwise they are explicitly skipped. These tests reject missing/corrupted inputs and check translation-gap handling. They do not validate gameplay or rendering. No game data belongs in this repository.

## Scope and rollback

Only this standalone prototype and its private work files are created. The original ISO/XBE, existing reconstruction repository, installed games, permanent saves and production systems are not modified. No process injection, kernel driver, network capture, public push or deployment is performed. Test runs must receive a fresh scratch-save directory and be time-limited. Rollback consists of removing this prototype and the specific private work directory after retaining any useful logs; no original game installation needs restoring.
