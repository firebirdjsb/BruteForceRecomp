# Brute Force recompilation prototype

Goal: a faithful native port of the original Xbox Brute Force (2003) to Windows, Linux and Android. This is a separate experimental project. It does not modify BruteForceDecomp or replace its evidence-led source reconstruction.

**This is not a finished or verified playable port.** Translation, compiling, booting, rendering and completing a mission are separate gates. Generated code and inferred calling conventions must be checked against the original executable and measured execution.

The immediate priority is a rendered, controllable campaign at a stable frame rate. The latest checkpoint recovers the measured missing combat/script entries, applies raw mouse counts after the camera's analog response, joins the window thread before resource teardown, and retains optional texture reconstruction and verified local DLC. The game retains original 30-FPS pacing; optional motion interpolation generates 60/120-FPS visuals separately. Bounded runs reached the first mission with HUD and scripted input, and the user accepted the new mouse feel. Intermittent startup and full combat/DLC-map acceptance remain open. See [STATUS.md](STATUS.md) and [frame-rate evidence](docs/FRAME_RATE.md) for exact boundaries.

## Desktop application

The launcher is a native C++ SDL2/Dear ImGui application. It does not open a browser or require a local web server or Python at runtime. On Windows, open **Launch Brute Force.cmd** after building; on Linux use `sh launch.sh`.

Windows builds expose **Graphics > Visual interpolation > Off / 60 / 120**.
**F1 > Renderer > Visual interpolation** changes the same setting live. This
experimental image-motion path requires DX12 presentation and accepts CPU,
D3D11 or D3D12 scene images. Direct D3D11/GDI presentation retains original
frames. Original pacing remains unchanged in every mode. Interpolation adds
one source frame of latency and can show artifacts; Off is the default.
The former native-60 option is retired, and its saved setting migrates to 30.

**Graphics > Anti-aliasing** in the launcher and **F1 > Renderer > Anti-aliasing**
share an experimental scene-edge filter. It runs before native HUD artwork and text/UI draw
commands, rather than filtering the completed frame. Synthetic CPU/D3D11/D3D12
tests preserve subsequent UI pixels exactly; full in-game HUD/menu validation
is still pending. The pause menu now uses its original viewport-aware position
for mouse hits and keycaps; a scripted campaign test exercised hover and Resume.
Keyboard prompts distinguish weapon fire (LMB) from inventory use (G), and
show ESC for Start instructions. Squad-command artwork remains unfinished. See the
[current checkpoint](STATUS.md) before choosing a local build.

```sh
cmake -S . -B .local/launcher-build -DBF_LAUNCHER_ONLY=ON
cmake --build .local/launcher-build --config Release --parallel 2
```

Build dependencies are fetched from pinned public repositories. Linux also needs system SDL2 development files. The application asks for the local ISO, imports and verifies **497 core-game files**, and excludes 188 demo/update/promotional files. It retains a local library and separate player-data folder, validates supported native inputs before launch, and starts the compiled game directly. The original ISO and existing saves are preserved. The former website implementation has been removed.

Locally packaged builds can include **Lockjaw, Cerulean and Prisoners' Lament**.
The launcher automatically installs and SHA-256-verifies all 51 allowlisted DLC
data files during import, verification and launch. Existing differing DLC is
preserved and reported. Xbox installers and title-update executables are excluded.
The supplied payload stays private; source contains only its inventory and hashes.
See [DLC packaging and verification](docs/DLC.md) for the local build steps and
the remaining in-game validation boundary.

Use `BruteForceLauncher --workspace FOLDER --game-exe FILE` to select an explicit library and native build. Otherwise the app uses the platform application-data directory and searches for a native Release build. Keep the executable, SDL2 DLL on Windows, and assets folder together. A mouse/keyboard/gamepad navigable launcher does not establish complete input support inside the game.

The launcher hides when Start begins game validation and startup, and returns when that
process exits, including a crash. Validation or start failures restore the app.
Only one interactive launcher can own a library, even when opened from different
installed copies; reopening it while the game runs does not reveal its hidden window.
Graphics, input and audio preferences are shared with F1 through the
local `graphics.settings` file; permanent saves stay in `player-data`.

The latest continuation prompt is [NEXT_AGENT_HANDOFF.md](docs/NEXT_AGENT_HANDOFF.md).
It records the installed builds, current evidence and unresolved gameplay issues.

For authentic local branding, run `python scripts/extract_branding.py --help` and configure `-DBF_PRIVATE_BRANDING_DIR=PRIVATE_OUTPUT_FOLDER`. The tool validates the exact XBE and extracts its title icon. The game executable and launcher use the icon; the launcher also loads its title image. These extracted assets stay private and are never committed or bundled with public source.

## Native graphics

Windows GPU scenes expose **Texture reconstruction: Off / 2x / 4x** in launcher
Graphics and F1 > Renderer. This reconstructs eligible sampled textures, including
HUD artwork and cube faces, with original mip levels retained and a bounded
256-MiB additional cache. It is spatial filtering, not an AI texture pack; it
cannot recover missing artistic detail. Movies, depth and render targets keep
their original path. The final scene still renders at its existing resolution;
native higher resolution, 16:9 projection and temporal FSR/DLSS remain unfinished.

The desktop input update adds full-speed WASD with normalized
diagonals, held mouse-button reports, and mouse sampling independent of USB
poll frequency. **F1 > Controls** contains the keyboard/mouse guide and a live,
draggable **0.1x–10x mouse sensitivity** slider; the native launcher also exposes
sensitivity under **Input & audio**. F1 is the only settings/capture hotkey.
The native player replaces the English **Basic Controls** tutorial's original
controller illustration and thumbstick instructions with authored WASD/mouse
artwork while keyboard/mouse prompts are active. Controller activity restores
the original artwork. The original title, panel and Accept/Skip actions remain.
Recognition checks the actual lesson text and widget hierarchy; other lessons
retain their original visuals until their content has been verified.
The Windows player consumes relative mouse counts at the authenticated camera
update after the original stick clamps, response curve, ramp and aim assistance.
Mouse motion no longer produces a virtual right-stick report. The original
pitch limits and authored weapon/camera effects remain on their existing path.
The user confirmed the corrected live build felt good on 3 November 2026.
See [raw mouse implementation and evidence](docs/RAW_MOUSE.md). The preceding build passed movement,
aiming, firing, zoom, grenade/item input and cursor capture; complete campaign
acceptance remains open.
The cursor is hidden/confined by the game window while aiming, and released
for menus, F1 settings, focus loss and minimization.
The user verified mouse motion and hidden cursor through **AnyDesk Gaming Mode**;
standard AnyDesk control did not deliver the relative mouse behavior required
for aiming. Keep Gaming Mode enabled for remote play. RMB maps to the game's
right-stick click (toggle zoom); **G** uses the selected grenade/inventory item.

The GPU texture path now uploads the game's authored mip chain, includes all
levels in cache invalidation, and honors mip filtering. It does not generate HD
textures or increase internal resolution. Native AMD D3D12 reference-image
differences remain a separate rendering issue. The player fixes a confirmed
stale depth/stencil alias used by the bloom mask; the matching title scene no
longer shows the bright blurred wash. The user confirmed the candidate was
working; complete campaign and effects coverage remains open.
The reviewed additive-clipping workaround was rejected: a matched tutorial
test removed valid text and restored it when reverted. It is not included in
the player. Complete camera-angle and campaign validation remains open.

Windows selects hardware DX12 automatically and falls back to D3D11, then GDI when initialization fails. The native launcher exposes renderer and AMD FSR 1 settings under **Graphics**. Direct launches can use `BF_RENDER_BACKEND=auto`, `dx12`, `dx11`, or `gdi`, and `BF_UPSCALER=off` or `fsr1`.

FSR 1 runs AMD's EASU and RCAS GPU compute passes when the window is at least the source size; smaller windows use bilinear downscaling. Aspect ratio is preserved. The default scene renderer is the CPU reference. Windows builds with `-DBF_EXPERIMENTAL_NV2A_D3D11=ON` can select **D3D11 GPU (experimental)** and GPU vertex programs in the launcher or F1's Renderer page. The scene is read back for the existing DX12/FSR presenter. This is spatial FSR 1, not temporal reconstruction or frame generation. DLSS integration remains pending because the renderer does not yet expose valid depth, motion vectors and camera history. Linux/Android do not use DX12; native graphics integration on those platforms remains separate. See [GPU backend evidence and limits](docs/GPU_SCENE_BACKEND.md).

A standalone hardware test requires no game data:

GPU-enabled builds also offer **D3D12 GPU via D3D11On12 (experimental)** for
scene rendering, separate from the presentation selector. Direct runs select
it with `RECOMP_GPU=d3d12`. It uses a real D3D12 queue through the bridge;
a native D3D12 scene pipeline remains unfinished. The current GPU title path
restores zero-depth tutorial/HUD rendering and preserves CPU-written movies.
Mouse/keyboard launches enable mouse capture; F1 settings releases it automatically.

```sh
cmake -S . -B .local/graphics-test -DBF_GRAPHICS_ONLY=ON
cmake --build .local/graphics-test --config Release
.local/graphics-test/Release/bf_graphics_smoke.exe
.local/graphics-test/Release/bf_interpolation_smoke.exe
```

These tests read actual GPU output to check color/alpha, letterboxing, changing source images, FSR output differences, window resize, downscale fallback and resource shutdown, plus reconstructed quarter/half/three-quarter motion frames, stationary HUD pixels and history resets. They do not validate gameplay or predict performance.

Startup fixes apply only to hash-checked build copies of the public runtime; its checkout stays clean. The native title profile enables `RECOMP_ASYNC_IO=1`; an explicit `0` is a direct-run diagnostic control. It preserves completed read data, final IOSB and event notification while returning the runtime's pending status for asynchronous handles. This prevents the observed original pack-reader busy loop. The semaphore target is derived from the live D3D device, and the completion word changes only when the renderer consumes an actual semaphore-release command. `BF_TRACE_FILE_IO=1` restores successful-read payload tracing; failures remain visible by default.

For an existing generated directory, run `scripts/recover_entries.py --xbe FILE --toolkit TOOLKIT --functions FUNCTIONS_JSON --output RECOVERED_DIR` and pass `-DBF_RECOVERED_DIR=RECOVERED_DIR` to CMake. The normal `generate` pipeline does this automatically. Recovery verifies the missing callback's original bytes and emits its complete lifted body privately. The public repository holds no recovered game-code body.

Diagnostic input options: `--keyboard` enables the keyboard test mapping (Z/X/C/V = A/B/X/Y, Enter = Start); `--mouse-keyboard` also enables mouse aiming, with capture released by F1 settings and focus loss. `--sample-startup` enables bounded Windows main-thread sampling; `--gpu-trace` records submissions. USB and vblank models are enabled by the boot-test launcher. Complete campaign and device coverage still require validation.

The game's profile/campaign name screens accept direct typing: letters/numbers/Space, Backspace (or Delete), Enter for Done, and Escape for the game's Back action. Original on-screen key labels, length limits, sounds and confirmation handlers still apply; the normal keyboard uses uppercase. Arrows and physical controllers retain the original grid navigation. Typing temporarily suppresses keyboard-to-controller actions and raw mouse capture, independently of the launcher's keyboard-test checkbox. Windows window messages and Linux SDL text events feed the same bounded queue. Scripted Windows tests edited both name screens and exercised Done/Back; complete physical-keyboard and Linux text-entry QA remain pending.

Native mouse menu input now hit-tests the game's live buttons, lists and on-screen keyboard. It uses original focus/selection handlers, then the normal controller-report path for activation. Left click selects; right click or Escape goes Back. Enter selects and arrows navigate when a menu owns input. Mouse coordinates follow the actual letterboxed game image. A scripted Windows run hovered Options, entered it with a click and returned with Escape. Unique menu widgets, split-screen/player-specific screens and complete physical-device coverage still need QA. Mouse aiming/gameplay remain separate work.

**F1** opens the live settings overlay; F1 or Escape closes it. Mouse or arrows/Enter change mouse menu selection, keyboard/mouse prompts, FSR 1, FSR output scale (Auto/2x/4x/8x), window resolution (Auto/720p/1080p/1440p/2160p), mute and master volume. Direct runs use session-only settings; launcher sessions persist them. Window presets fit within the desktop work area. Fixed FSR factors describe its intermediate output relative to the original 640x480 source; 8x is 5120x3840 before fitting to the window, not native scene rendering. DX12/FSR controls are unavailable on other backends. The common A/B/X/Y menu prompt glyphs gain keyboard/mouse keycaps while keyboard/mouse is active; unrecognized glyphs retain original rendering. Actual physical-controller switching still requires QA.

Tab/Page Up/Page Down or the page buttons select F1's **Renderer** page.
Scene renderer, GPU vertex programs and presentation changes take effect at the
next launch. When started by the app, live and restart preferences persist and
the launcher reloads them on return. Direct runs without `BF_UI_SETTINGS` use
session-only preferences. Original swap-interval pacing, exact texture
validation and automatic shader caches are reported on that page.

The renderer retains the original 4:3 game image. The 16:9 window presets are **not native widescreen gameplay**. Correct projection/HUD behavior and cheats remain unavailable, and complete campaign/mission validation remains unfinished. The experimental GPU tutorial run reported about 30 presented frames/s in its late scene; loading dips and full mission/input/effects QA remain. See STATUS.md for measured limits and test evidence.

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

## Graphics backend boundary

The native DX12 presenter and FSR 1 are implemented in `src/graphics/` and
operate on completed images in guest memory. The optional D3D11 scene backend
now connects actual NV2A draws, generated vertex shaders and register combiners,
then reads back for that presenter. The CPU reference remains available. A native DX12 scene
pipeline needs its own resource, descriptor, PSO and fence implementation.
See [DX12 compatibility](docs/DX12_COMPATIBILITY.md).

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
menu is controlled by the separately authenticated private game adaptations.
Do not distribute extracted game files or substitute another title's data.

## External integration

All commits from `integration/tzeentchnet-texture-math` are integrated into
main alongside the native application and campaign fixes. The reference
repository was checked at commit `24d2e58687c976390efa23ab8b83a6b80078f1ec`.
Its different XBE fingerprint prevents transplanting its title hooks.
[Reuse assessment](docs/TZEENTCHNET_REUSE.md),
[feature status](docs/UPSTREAM_INTEGRATION_PROGRESS.md) and
[third-party notices](THIRD_PARTY_NOTICES.md) distinguish runtime changes,
tested reusable components and remaining integration gates.

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
