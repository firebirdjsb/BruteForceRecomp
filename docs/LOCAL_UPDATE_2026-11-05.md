# Local build update and handoff - 5 November 2026

The user requested pulling all commits/changes and updating local files to the
newest build. Workspace: `D:/Documents/ChatGPT/bfRecomp`, Windows/PowerShell.
All remote branches were fetched. Consolidated main `a0a85ab` already contains
both rendering branch tips, so main was fast-forwarded without dropping work.
There were no initial local edits or running player/launcher processes.

## Necessary local build repairs

Code checkpoint `f7c300009504366105a0d5df3947a153cebca0ad` adds two Linux fixes:

- `src/gpu/temporal_trace.cpp`: rename the internal `read` helper/calls to
  `read_guest`, avoiding the POSIX `read` overload collision. No offset, bounds,
  access validation or memory behavior changed.
- `CMakeLists.txt`, `cmake/native_aspect.cmake`, `cmake/temporal_motion.cmake`:
  include the authenticated shared motion-wrapper overlay for every title host,
  independently of Windows-only aspect integration. Linux otherwise linked
  duplicate original wrapper symbols and lacked their renamed original bodies.
  Windows retains the same original overlay order and behavior.

No canonical generated/recovered source or pinned toolkit file was modified.
No SDK was downloaded, no gameplay FSR gate relaxed and no physics/input tuning
was applied. Original assets, permanent saves and settings were preserved.

## Validation and installed files

- Windows complete player build and43/43 CTests passed, including original
  aspect helpers, UI, cursor and raw mouse checks.
- Linux complete player build and28/28 CTests passed in WSL BruteForceBuild,
  build directory `/var/tmp/bruteforce-build`. This is not Linux GPU acceptance.
- Public renderer/runtime build and49/49 CTests passed on this machine.
- Native launcher build and4/4 checks passed. Python:17 tests ran, with one
  optional native-input check skipped; native file validation ran at install.
- A140-second scratch-save tutorial run used D3D12 scene/DX12 presentation,
  FSR1,120 visual interpolation and requested native16:9. It reached the world,
  issued movement/turn/jump/fire inputs and ended at its owned watchdog, with
  no guest failure. Logged transactions show16:9 horizontal expansion, unchanged
  vertical bits/viewport and restoration of124 owned bytes. This is bounded
  scripted QA, not manual HUD/audio/controller/all-mission or normal-close QA.

Installed player SHA256:
`d8e31620a3f5ad63886750015989399c1857d155b698ba582ef9941b5000e433`.
The launcher's configured player is
`work/diagnostic-build/Release/bruteforce_recomp.exe`; the old
`work/aa-input-quality/player/Release` alias was also updated with matching maps.

Installed native launcher SHA256:
`6d4372ccdb6ca1c887dafc6074565ab3968a3d7f2c669602b09d9da71780eb77`.
Both root `BruteForceLauncher.exe` and its previous
`work/native-launcher/build/native_launcher/Release` path were updated.

Atomic installation changed6 files, retained122 already-current files and
verified identical hashes for64 TDATA/UDATA/settings files. Native content
validation returned0. Receipt and rollback files:
`work/local-update/install-20261105-055512/install-manifest.json`.
After normally closing both apps, restore the exact executable/map paths from
that manifest's backups to roll back; keep current player-data. No production
service or original game/DLC data was changed.

Build/test/campaign logs and immutable build hashes are under
`work/local-update/update-20261105` and adjacent `*-20261105.log` files. The
campaign scratch root is `work/combat-input-quality/interval-original-ro4t4nfw`.
These private files, binaries, maps and captures are not pushed.

## Current behavior and next work

Existing preferences, including sensitivity2.03 and interpolation120, remain.
Native16:9 defaults off for older preference files; use Launcher > Graphics >
Native widescreen or F1 > Renderer to enable it on the supported GPU/DX12 path.
The720/1080/1440/2160 presets are presentation sizes; the guest source remains
640x480. They do not create native-HD scene detail.

The local build has no optional AMD temporal SDK configured. FSR1 remains
available; FSR3/4 requests retain their guarded unavailable/fallback behavior.
Gameplay temporal FSR3/4 and DLSS are unfinished. Read
`AGENT_HANDOFF_TEMPORAL_FSR_2026-11-04.md` for the unresolved whole-world ownership
and final dispatch gates. The older other-machine SDK results are not this
machine's gameplay acceptance. Validate the exact final remote HEAD's Actions
run separately; the results above are completed local checks.
