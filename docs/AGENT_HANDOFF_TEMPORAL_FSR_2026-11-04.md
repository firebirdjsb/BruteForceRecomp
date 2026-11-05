# Agent handoff: temporal FSR checkpoint, 4 November 2026

## Stop state and authorization

The user stopped implementation and requested the checkpoint and handoff,
committed as `f9e08aa` on `codex/temporal-fsr-integration`. The user subsequently
explicitly requested merging all branches and keeping their work on main.
Main was fast-forwarded to include this checkpoint and every other local/remote
branch tip, with no conflicts or discarded commits. The existing stash's
texture work was already included; the stash and untracked DLC ZIP are retained.
This source consolidation does not resume unfinished FSR implementation or
promote the private executable to the installed application.

The requested outcome is genuine in-game temporal FSR3/4. **It is not achieved.**
The real SDK processor and guarded rendering components work, but complete
title motion/projection provenance and the final game dispatch are unfinished.
Never describe an SDK test or a selected F1 request as working gameplay FSR.

The user's pasted AGENTS instructions cap repairs of the same issue at two.
Both ownership repair attempts were used. Functional ownership work stopped;
the disproven static validation was withdrawn, not replaced with another guess.
A further ownership repair needs a newly authorized bounded cycle. The user
also requires gameplay smoke acceptance for candidate promotion. Their later
explicit branch-consolidation request authorizes publishing this incomplete
source checkpoint to main; it is not a new FSR gameplay acceptance. The earlier
widescreen smoke acceptance does not establish working gameplay FSR.

## Installed application: preserve it

Workspace: `C:/Users/kolby/Documents/ChatGPT/BFRecomp`.

The installed, user-accepted widescreen/HUD player and launcher are unchanged:

- Root `bruteforce_recomp.exe` SHA256:
  `9b2c79bdd8e0f28140b1caa274913dff1a6b528a641cf3ffdedefd0cc7f2563e`.
- `.local/installed-player.map` SHA256:
  `bfafd0ba5189de74b5756381a2d5a70c6c549d5cf5137a30a73311021b35da63`.
- `.local/launcher-build/native_launcher/Release/BruteForceLauncher.exe` SHA256:
  `036ca6a3a34a27637c42f66a551bcc506c20ac779a60e9478b20d53fd3980649`.
- Existing rollback: `.local/widescreen-install-20261104-164317`.

Permanent library/saves/preferences are under
`C:/Users/kolby/AppData/Roaming/Firebird/BruteForceRecomp`. All title diagnostics
used copied saves and private settings beneath `.local/temporal-fsr`.
No installed application, permanent save, retail asset or production service
was changed in this checkpoint. Preserve the untracked user file
`BruteForceDLC.zip`; it is deliberately excluded from the commit.

Native 16:9 with intact corner-anchored HUD and 720/1080/1440/2160 output
presets was already accepted. Existing WASD/aim/fire/zoom/cursor work is not a
new task. AnyDesk needs Gaming Mode for relative mouse input. Keep F1 as the
settings entry; do not restore F2/F3/F4/F9 settings/capture hotkeys.

## Source implemented in this checkpoint

- `src/graphics/dx12_device.*`, `src/gpu/scene_device.cpp` and corresponding
  CMake: one shared D3D12 device/native queue, separate D3D11On12 translation
  queue and real fences. Native unwrap/return does not use the translation
  queue. The older-runtime same-queue deadlock is fixed without fake completion.
- `src/graphics/game_temporal.*`, `temporal_control.*`, `temporal_handoff.*`:
  real optional AMD provider selection/dispatch, pre-draw SDK jitter tickets,
  coherent typed input validation, bounded retained output leases, immutable
  HUD copies and exact executor-generation/color-VA handoff. The window thread
  must use retained tickets, never query the latest producer source.
- `src/graphics/dx12_presenter.*`, `present_dispatch.cpp`, `present.hlsl`:
  direct GPU output presentation and native HUD plus live host overlay after
  reconstruction. Actual provider metadata travels with its output. A request
  does not set active status. Saved requests are not overwritten at presenter
  creation when `BF_UPSCALER` is absent.
- `src/graphics/ui_composition.*`, `src/ui/host_ui.*`, `ui_draw.*`: separate
  premultiplied native FP16/host BGRA layers. Source sync precedes capture.
  Genuine same-target feedback, unsupported blend and source changes still
  reject independent HUD composition, with first-cause diagnostics.
- `src/gpu/scene_snapshot.*`: GPU-held original world depth can be paired with
  later pre-HUD color only for the same generation/view/context/geometry serial.
  This does not bless projection or missing motion.
- `src/gpu/temporal_trace.*`, guest/CMake adapters, decoder origin metadata:
  authenticated, ordered semantic records; actual ctor/dtor actor generations;
  actor child ownership; raw static observations. Lifetime observation starts
  even when FSR is off. Bounded trace I/O is separate from runtime observation.
- `src/gpu/motion_history.*`, `motion_renderer.*`, `motion_hlsl.h`, VSH/PS/backend
  hooks: actual GPU post-VSH position history using original vertex IDs, motion,
  reactive/composition masks and canonical-depth diagnostics. They do not make
  untracked opaque work valid. New/disoccluded known history is explicitly
  invalid/reset/reactive. Retail static adapters are always rejected.
- `src/ui/upscaler_state.*`, `graphics_options.h`, F1 and launcher plumbing:
  OFF/FSR1/FSR3/FSR4 requests, separate actual status/capabilities, append-only
  version-1 preference migration. Sensitivity/aspect/audio settings are retained.
  The current source still cannot deliver completed gameplay FSR3/4.

Important GPU details: motion uses previous-minus-current unjittered render
pixels. Real queried jitter is removed from that calculation. The tested
canonical formula is `A + B / PS_SV_Position.w`, not multiplication. Original
RT0/depth/stencil/alpha/scissor behavior remains. VS/PS b2 must explicitly reset
because the original partial-clear shader also uses b2. The real finite-filter
GS broadcasts the minimum history-valid value over the triangle, including
zero-weight edges. UI uses MRT3; motion/reactive/depth/composition use 1/2/4/5.

No complete title source calls `bf_game_temporal_submit` yet. The backend's
next-generation jitter latch defaults off. Do not turn it on merely because
provider creation or a subset of actor history works. Missing opaque data must
retain native fallback; rejected jittered sources must not be displayed as an
uncorrected CPU fallback.

## Exact blocker and withdrawn assumption

`motion-d` captured frames 1945-1952 using stock geometry shaders. Each had
eight identified main RGB actor parts, but 302-388 other main world draws were
untracked, including 114-172 opaque and 222-297 depth-writing draws.

The original static scene constructor and resource observation work. However,
retail `scene+0x2C` was wrongly considered a D3DPushBuffer size. Original
`00372150` feeds that changing word to synchronization routine `00371CE0`.
It is not an established byte extent. Hundreds of additional world draws are
also outside the observed `1F5600` scope; changing one offset would not solve
whole-scene ownership.

The unsafe validation has been withdrawn. Even aligned synchronization values
can never approve static identity. Raw data/sync words and independently
observed constructor generation are diagnostics only. A generic synthetic
static MotionHistory key is not proof of the retail layout.

Read `.local/temporal-motion-check/ownership-final-diagnosis.json` and
`docs/FSR_TEMPORAL.md` before a newly authorized investigation. Establish actual
resource allocation/header bounds and remaining pass owners from original code
and observations. Do not invent slots, use draw ordinals/VB hashes as lifetimes,
or weaken provenance to get a frame through the SDK.

## Verification actually completed

- Full public runtime build succeeded; **49/49 CTests passed**. Log:
  `.local/temporal-fsr/final-runtime-tests.log`.
- SDK-enabled standalone ALL build succeeded; **16/16 CTests passed** on RX7600,
  including actual FSR3.1.6/4.1.2, controller/jitter, source tickets, SDK framegen
  conformance, native-resource presentation and scene interop. This does not
  establish in-game frame generation. Log: `final-sdk-tests.log` in that folder.
- Focused MRT/history tests passed on native D3D11 and 11On12: rigid/deformed
  motion, applied jitter, canonical depth, triangle validity, transparency,
  RT0 parity and six nonblack partial-clear/b2 cases. Generic VSH compile passed.
- F1/state tests and launcher lifecycle tests passed, including old preferences,
  new FSR requests, preserved aspect/sensitivity and no extra hotkeys. Launcher
  UI source was compile-checked in an isolated cache. Screenshot:
  `.local/upscaler-main-page.bmp` (request shown separately from active OFF).
- Private native SDK-enabled player built successfully. Final scripted tutorial
  capture used no `BF_UPSCALER` override and retained saved FSR1. It reached the
  world after 133.2s, recorded 15.1s, then the owned timeout terminated it.
  Exit 1 is intentional termination, not normal-close or user acceptance.
- That native run observed eight tracked actor submissions, usually eight
  preceding histories (occasionally seven plus new history), clear observed,
  zero resource failures, jitter0 and **complete0 on every captured frame**.
  Ownership and strict projection classification still reject the full source.
  Evidence: `.local/temporal-fsr/final-native/game.log` / `result.json`.
- Projection replay: 85 title batches, 32,871 indexed vertices; CPU/GPU validity
  agreed, W bit-identical, normalized depth maximum difference 1.2e-7. Ordinary,
  large-far background and orthographic secondary projections are distinct.
  Evidence: `.local/temporal-fsr/projection-proof/projection-proof.json`.
- Seven held-depth/pre-HUD-color generations match frame/VA/view/context.
  Evidence: `.local/temporal-fsr/motion-c/analysis.json`.

Early ALL builds exposed missing standalone link dependencies. These were fixed
before the final green runs: the optional prepare-present callback stays in
`Nv2aBackendExt`; the decoder owns `bf_motion_parameter_va`; every host-UI target
links the shared upscaler state. Earlier not-run failures are not the final
verification result. These are the checkpoint's local results. The older
`08a52c9` CI run was green but does not cover this checkpoint; use the new main
consolidation's Actions run for its current hosted result.

## Local build/evidence paths for resumption

Private prototype, **not the installed game**:
`.local/temporal-fsr/player/Release/bruteforce_recomp.exe`, SHA256
`e46183810d47b78d1bb51b27ca1d5f3e1999e8921ac38819dc277f369b73b334`.
Its paired map is `.local/temporal-fsr/game-build/bruteforce_recomp.map`.
The authored source snapshot/manifest are `.local/temporal-fsr/source` and
`source-manifest.json`. Canonical generated/recovered/toolkit files remain
outside it; never modify those originals.

SDK checkout: `C:/Users/kolby/Documents/Codex/2026-11-04/task/fsr-sdk` (SDK2.4).
Private build cache: `.local/temporal-fsr/game-build`. SDK-only checks:
`.local/temporal-processor-sdk-check`. Public runtime: `.local/temporal-ui-check`.
Do not rebuild `.local/launcher-build/…/BruteForceLauncher.exe` to test a candidate;
that is the installed launcher's path.

CMake executable:
`C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`.
CTest is beside it. Private snapshot configure/build needs process-scoped
`GIT_CEILING_DIRECTORIES=C:/Users/kolby/Documents/ChatGPT/BFRecomp/.local/temporal-fsr`;
otherwise git-apply silently skips absolute overlay paths through the parent
repo's ignored-prefix handling. `/MP4` plus MSBuild `--parallel 1` bounds compiler
concurrency. `.local/build-venv/Scripts/python.exe` has Pillow; the default
Python does not. Existing bounded drivers validate the owned EXE/map and use
copied saves. No private title process remains running at handoff.
