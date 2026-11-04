# Native aspect-ratio work - 4 November 2026

The user has resumed engineering and prioritized native aspect-ratio changes
before gameplay temporal FSR/DLSS. The installed player remains the working
640x480 build. No experimental 720p player was installed. Original simulation,
physics, raw mouse input, saves, game assets and launcher preferences are unchanged.

## Confirmed fix

`cmake/video_capabilities.cmake` authenticates the entire pinned MIT
`kernel_hal.c`, then modifies a private build copy. AV encoder option 6 already
reported HDTV pack 4, NTSC-M and 60 Hz, but omitted the mode capability bits used
by the original D3D selector. It now includes 480p bit19 and 720p bit17. The
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

## Private 720p experiments and limit

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

The native experiment still lacks coherent UI/movie/F1 sizing and validated
camera/frustum behavior. Some movie images occupy the original 640x480 area;
the host overlay explicitly supports only 640x480. Dumped render surfaces are
not necessarily the composed window. Do not use those dumps as proof that the
whole live window or every gameplay frame is correct. No matched-position
horizontal-view, culling, HUD, split-screen, FX or mission performance acceptance
was completed. The bounded repair limit was reached; further changes need a
fresh narrow design based on these results, not more speculative mode flags.

## Camera evidence and next implementation

Read-only, PE/map-validated scans of disposable processes identified projection
objects by their authenticated class and dynamic owners. Projection fields
`.14/.18` are half angles, `.1c/.20` near/far, `.24/.28` tangents, `.2c` aspect,
`.30/.34` viewport half sizes and `.38/.3c` pixel projection scales.
For example, the same default projection moved from half sizes 320/240 and
aspect about 4/3 to 640/360 and aspect about 16/9. Other cameras retained
authored tangents, used cinematic subrectangles or were inactive. An aspect
constant alone cannot identify the active camera. No camera writes were made.

Next isolate enhanced render-target storage from the title's original asset
budget, while retaining real allocation bounds, aliases and completion rules.
Then integrate the chosen aspect with active projection, frustum/culling,
viewport, native HUD/menu/movie transforms and mouse/keycap coordinates. Preserve
authored FOV semantics, original 30-Hz simulation and the accepted raw mouse path.
Validate identical camera positions, added horizontal visibility and unchanged
vertical framing before exposing the mode in the native launcher/F1.

The real pre-UI color/depth export and native D3D12 handoff remain the basis for
temporal work. Engine motion, applied jitter, camera/depth validation, reset
identification, transparent/reactive handling and HUD composition are still
required. Existing spatial FSR1 and image-motion interpolation are not gameplay
temporal FSR or DLSS. See `FSR_TEMPORAL.md` for that separate integration gate.

## Real campaign scene export

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

## Local evidence and rollback

Private evidence: `work/aspect-ratio`, including `final-player-checks.log`,
`linux-checks.log`, `public-checks.log`, camera JSON and the probe CMake file.
Scratch campaign roots are under `work/combat-input-quality`:
`interval-original-q7si11fi` (working control), `6eninz55` (720p failure),
`dd1g0p3v` (rejected 128-MiB profile), `sc9vwrq3` (credit not compiled),
`2tdg6bov` (credit actually compiled, bounded campaign run).
`interval-original-pfk831u5` is the AV-only final candidate campaign/export run.

The ordinary `work/diagnostic-build` cache has been restored: no
`CMAKE_PROJECT_INCLUDE`, all private probe flags Off, output redirected back to
`work/menu-polish/player`. Build maps must stay paired with their executables;
the installed map beside `work/diagnostic-build/Release` is still authoritative
for the installed player. The new AV-only candidate is not installed.
Rollback the source checkpoint and rebuild to remove the capability change.
No save migration or production service was touched.
