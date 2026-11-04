# Native aspect-ratio work — status as of 4 November 2026

## Current proven state

Native 16:9 now uses a shared live setting in the launcher and F1 menu. It
defaults off. The public camera-helper transaction was verified to expand the
actual gameplay view to 16:9 while retaining the original 640x480 guest render
allocations and vertical framing. Camera and presenter metadata are associated
with the same source frame, so the presenter uses the aspect state belonging to
the camera image it receives.

In the private live tutorial run, successive source frames 2656 and 2657 were
captured with `wide=false` and `wide=true`. The 16:9 capture showed terrain at
the added side areas. Turning the F1 setting off restored pillarboxing. Output
presets 720p, 1080p, 1440p, and 2160p change presentation size; they do not
create native-HD game scenes.

Hardware checks covered both bilinear and FSR 1 presentation at 1280x720,
1920x1080, 2560x1440, and 3840x2160. The normal verification suite passes
40/40.

The first per-sprite anchoring attempt failed in the real title: health elements
split apart and the radar frame moved while its map and markers stayed centered.
The second repair uses complete native child-draw scopes. Five traced class/draw
pairs select health/shield, radar, co-op prompt, ammo/weapon and inventory groups.
All descendants inherit that anchor, including custom radar geometry, fonts and
host keycaps. Unknown roots, menus and reticles stay centered. Individual sprite
dimensions and font pivots no longer select anchors.

The exact final player passed actual tutorial visual QA: health frames/fills and
radar frame/map/dots stayed together at the left corners, ammo/inventory remained
at the right corners and the reticle stayed centered. F1 switched 16:9 -> 4:3 ->
16:9 with the matching HUD layout and world framing. All four output presets
were cycled live. Requests above the monitor's usable area fit those bounds;
the 1080p/1440p/2160p selectors on this monitor do not prove physical 4K display.
The separate hardware fixture verifies exact 3840x2160 swapchain processing.

A private depth probe recorded 240,741 samples below one and no exactly-zero
samples in a 640x480 capture. This shows useful depth values were present in
that capture. Projection/depth interpretation and motion vectors remain
unverified, so temporal FSR 3/4 are not ready.

The tested player is installed at `bruteforce_recomp.exe`, SHA256
`9b2c79bdd8e0f28140b1caa274913dff1a6b528a641cf3ffdedefd0cc7f2563e`.
Matching map SHA256 is
`bfafd0ba5189de74b5756381a2d5a70c6c549d5cf5137a30a73311021b35da63`
at `.local/installed-player.map`. Rollback retains the prior BF172 executable
and map in `.local/widescreen-install-20261104-164317`; restore that pair with
the game closed and preserve current player data. The launcher was rebuilt and
still selects the root player. Both user preference files and all656 existing
library files were hash-verified unchanged, including mouse sensitivity1.23.
Installed media validation passed. The user subsequently confirmed all smoke
checks pass: aspect/output selectors, intact corner HUD, centered reticle and
movement/aim/fire. This satisfies the user's standing source-push gate; agent
QA alone would not grant permission. Verify the final commit/remote state.

The final run used cloned saves, reached the tutorial after66.89seconds and
held it for120.39seconds. Its owned timeout terminated the process before the
attempted normal-close check; exit1 is not evidence of a game crash or normal
shutdown. No all-campaign, split-screen, unsupported cinematic camera or mission
3/4 performance acceptance is established. Widescreen defaults off; original
simulation, input, assets, allocation budget and completion waits are retained.
No production service was touched.

Private evidence is under
`.local/render-followup/integration-8b5e187/wide-view-a`, including the source
frame captures, `depth-analysis.json`, and `game.log`. HUD trace instrumentation
is in `src/game_text_guest.c`, `src/ui/hud_anchor.cpp`, and
`cmake/inline_glyphs.cmake`; related private run artifacts are under
`.local/render-followup/integration-8b5e187/owner-trace-c`.
Final GPU readbacks, `agent-visual-qa.json`, `protected-after.json` and the
installation receipt are under the sibling `hud-groups-d` directory. The current
receipt is `.local/render-followup/latest-candidate.json`. Normal checks pass
40/40, launcher checks4/4, and the previously verified two-API scene/depth and
exact-size aspect fixtures remain applicable. The trace's field named `uid` is
the raw component+0x1C word, not a proven semantic resource identifier; owner
classification uses validated vtable/draw pairs instead.

## Historical: video capability correction

`cmake/video_capabilities.cmake` authenticates the entire pinned MIT
`kernel_hal.c`, then modifies a private build copy. AV encoder option 6 already
reported HDTV pack 4, NTSC-M and 60 Hz, but omitted the mode capability bits used
by the original D3D selector. That correction added 480p bit19 and 720p bit17. The
separate low-bit capability queries and all original failure checks remain.
This does not select a new resolution, change the camera or claim widescreen.

The original call chain is `68340 -> 6A750 -> 36EC60 -> 3764C0`. Its mode
selection call at `376CD1` invokes `378439`; failure at `378692` returns
`80004005`. The mode table starts at `37A420`, with 185 twelve-byte records.
For the supported NTSC-M HDTV profile, the 1280x720 row requires both the
option-6 720p capability and presentation flag `0x10` (progressive). A missing
flag still correctly fails. No HRESULT is forced to success.

The focused public test executes the actual patched AV function. An optional
private regression executes the original translated selector and its actual
mode/format tables, authenticated against the exact supported XBE and complete
canonical translation hashes. No retail code or data is added to source control.

```powershell
cmake -S . -B work/diagnostic-build -DBF_VIDEO_MODE_FIXTURE_XBE=D:/Downloads/BruteForce-Decompilation/default.xbe
cmake --build work/diagnostic-build --config Release --target bf_video_capabilities_test bf_video_modes_test --parallel 4
ctest --test-dir work/diagnostic-build -C Release -R 'video_capabilities|retail_video_modes' --output-on-failure
```

Nine original-selector cases cover unchanged 480i, the old missing capabilities,
missing progressive flag, accepted 720p, unsupported 1080i/custom dimensions,
the existing 480p/widescreen policy, failure publication and original stack
cleanup/callee-saved EBX/EDI. Full Windows checks: 37/37. Linux: 23/23.
Public DX11/DX12 scene export/parity/AA checks plus AV capabilities: 8/8.
These checks do not establish native-widescreen gameplay acceptance.

## Historical: private 720p experiments and limits

All experiments are under ignored `work/aspect-ratio`; original game C and the
pinned toolkit were left untouched. The original 640x480 control reached the
tutorial, accepted scripted movement/turn/fire input and ended at its watchdog.

1. Changing native dimensions to 1280x720 reproduced mode-selection E_FAIL.
2. Adding the missing HD flags and progressive request passed original device
   creation and produced an actual 1280x720 framebuffer. This solves the old
   device-creation blocker, not camera/HUD integration.
3. That probe failed at the original null physics call after mission loading.
   Compared with 640x480, its two color buffers and depth buffer consume 7,200
   KiB more of the guest budget. The original game asset arena fell from 34,644
   KiB to 27,444 KiB. This is measured allocation behavior, not a physics fix.
4. A diagnostic 128-MiB guest profile exited through the title's firmware path;
   it was rejected. No RAM extension was retained.
5. A private diagnostic credited exactly 1,800 extra video pages in available
   memory accounting. An initial run did not compile that change: a later
   scene-backend overlay owned the final memory-layout source. After correcting
   the diagnostic's source selection, the asset arena was restored and the
   150-second run entered the tutorial, accepted scripted input and ended at the
   watchdog without the earlier guest failure. This accounting exception is
   **not production code**. A global memory credit is not a validated allocation
   or renderer design and must not be copied into a release.

At that checkpoint the experiment lacked coherent UI/movie/F1 sizing and
validated camera/frustum behavior. Some movie images occupied the original
640x480 area; the host overlay supported only 640x480. Render-surface dumps did
not prove the composed window. Those limits describe the historical 720p
experiment; the current camera and presentation evidence is summarized above.
HUD ownership and complete gameplay acceptance remain open.

## Historical: camera scouting before the current transaction

Read-only, PE/map-validated scans of disposable processes identified projection
objects by their authenticated class and dynamic owners. Projection fields
`.14/.18` are half angles, `.1c/.20` near/far, `.24/.28` tangents, `.2c` aspect,
`.30/.34` viewport half sizes and `.38/.3c` pixel projection scales.
For example, the same default projection moved from half sizes 320/240 and
aspect about 4/3 to 640/360 and aspect about 16/9. Other cameras retained
authored tangents, used cinematic subrectangles or were inactive. An aspect
constant alone cannot identify the active camera. No camera writes were made.

This scouting note preceded the current public camera-helper transaction. That
transaction now preserves the 640x480 guest allocations, keeps vertical framing,
and expands the supported gameplay view. Full HUD ownership and other gameplay
acceptance remain separate work.

At that checkpoint, the pre-UI color/depth export and native D3D12 handoff were
the proposed basis for temporal work. Engine motion, applied jitter,
camera/depth validation, reset identification, transparent/reactive handling
and HUD composition remained required. Existing spatial FSR1 and image-motion
interpolation are not gameplay temporal FSR or DLSS. The later depth probe
recorded real nonzero samples, but did not clear the projection/motion gate.
See `FSR_TEMPORAL.md` for the separate integration status.

## Historical: initial campaign scene export

The final AV-only candidate, SHA256
`9eca119d272b8f4830fcf457bb00cda52ae5dc21f5c689a79a901419e404fd0b`,
ran the 640x480 tutorial with disposable saves. It accepted scripted movement,
turning, jump and firing and ended at the 145-second watchdog without a diagnosed
guest failure. This is bounded scripted QA, not manual combat/all-mission QA.
The renderer captured two actual gameplay images, source generations2780/2781,
before native UI, with214 tracked depth-writing draws. Visual inspection shows
the real world/player without HUD in the color export.

However, all307,200 depth samples being finite and in[0,1] is insufficient:
306,081 samples are exactly zero,705 exactly one, and only414 strictly between
zero and one (415 distinct finite values). The two snapshots have the same
distribution. The depth_min/max/below_one metadata must not be interpreted as
proof of useful scene depth. Diagnose the live depth encoding, projection and
capture boundary before sending this attachment to a temporal SDK; do not
invent depth/motion to make an API dispatch succeed. The metadata correctly
keeps projection unverified and temporal_sdk_ready=false. Private evidence is
`work/aspect-ratio/campaign-scene-export`, including `validation.json`, raw
depth, color PPM and previews; none is published.

## Historical: provenance repair and passive camera candidates

The `C:/Users/kolby/Documents/ChatGPT/BFRecomp` checkout pulled checkpoint
`754117e` and main `52354bf`, merged as `8b5e187`, retaining unpublished
profiling/span-hoist changes. An actual local tutorial run reproduced the depth
symptom in four alternating color surfaces:214 recorded draws,306,081 zero
samples out of307,200,706 ones and413 values strictly between them.

The exporter previously kept a color's qualifying-draw count after its depth
attachment was cleared, modified through another color, or recreated. The new
metadata guard taints dependent color associations, persists that rejection
across flips, and requires a combined full color/depth reset followed by actual
qualifying draws before exporting again. Stencil-only work preserves depth
provenance. The native renderer also performs unconditional depth writes when
depth_write is enabled with testing off; those writes invalidate provenance.
No clear/draw commands, depth conversion, waits or original completion are changed.

Both APIs reproduced the stale-export regressions before the fix. The final
public suite passes13/13, including19 provenance cases on each of D3D11 and
D3D12. Positive controls verify actual .75/.25 depth and blue/red scene pixels;
all cases retain the expected final green UI pixels. Full native37/37 and
launcher4/4 pass. A guarded private tutorial run reached gameplay and exported
no untrusted depth maps. This is conservative rejection, not restored useful
scene depth: valid frames may also be rejected until an earlier capture boundary
and the actual world-depth encoding are established. That run therefore kept
SDK readiness false; subsequent depth evidence is summarized at the top of this
document.

Static source inspection identified a stronger camera seam than generic
frustum construction: original `sub_0006FCB0` passes its camera instance and
view context into `sub_000D9F00` at return addresses `6FCE5` and `6FE8B`.
Its later restoration code can reset projection state after dispatch. A private,
hash-gated build adds two passive callbacks immediately before those dispatch
argument pushes, preserving every original instruction and the canonical source.
It resolves camera+0x50 dynamically, bounds/checks reads and records at most64
samples after an explicit gameplay trigger. It does not write guest memory.

The actual local tutorial trace recorded64 samples from camera82C3D260,
projection014F5100 and context81B77890 (example process-specific addresses,
never reusable offsets). Aspect was approximately4/3, half viewport320/240,
half angles35/27.706 degrees, near/far .1/250 and projection scales +/-457.007.
This confirmed one consumed camera candidate in that passive trace. It did not
establish all-view coverage, final surface ownership, culling behavior, or
cutscene/split-screen selection. Those statements predate the current verified
camera-helper transaction; the HUD and broader gameplay acceptance remain open.

Private evidence is `.local/render-followup/integration-8b5e187`, including
`baseline-8b5e187/depth-analysis.json`, `native-aspect-view/camera-analysis.json`,
paired player/maps, test logs and `installed-candidate.json`. The isolated
trace sources/cache/build manifest are `.local/native-aspect-followup`.
The normal BF172 player remained installed at that checkpoint: SHA256
`bf172a1f24cfb495e6004fa397684022d24025d38d70ce0744512bd99e4d8f4b`,
paired with `.local/installed-player.map`; rollback is
`.local/update-20261104-152152`. All656 existing library files, including
permanent saves and preferences, were hash-verified unchanged. The private
trace executable was not installed. The widescreen source remains unpublished
pending user gameplay confirmation. No production service was touched.

## Earlier evidence and rollback

Private evidence: `work/aspect-ratio`, including `final-player-checks.log`,
`linux-checks.log`, `public-checks.log`, camera JSON and the probe CMake file.
Scratch campaign roots are under `work/combat-input-quality`:
`interval-original-q7si11fi` (working control), `6eninz55` (720p failure),
`dd1g0p3v` (rejected 128-MiB profile), `sc9vwrq3` (credit not compiled),
`2tdg6bov` (credit actually compiled, bounded campaign run).
`interval-original-pfk831u5` is the AV-only final candidate campaign/export run.

At that earlier checkpoint, the ordinary `work/diagnostic-build` cache had been
restored: no
`CMAKE_PROJECT_INCLUDE`, all private probe flags Off, output redirected back to
`work/menu-polish/player`. Build maps must stay paired with their executables;
the installed map beside `work/diagnostic-build/Release` is still authoritative
for the installed player. The AV-only candidate was not installed. The rollback
instruction applied to that earlier capability-only candidate; it does not
describe the current widescreen source state. No save migration or production
service was touched.
