# Locally bundled DLC

The supplied `BruteForceDLC.zip` was inventoried without running its Xbox
installers. It contains these data packs:

| Pack | Mode | Original content directory |
| --- | --- | --- |
| Lockjaw | Campaign | `4d53001e00000001` |
| Cerulean | Deathmatch | `4d53001e00000002` |
| Prisoners' Lament | Squad Deathmatch | `4d53001e00000003` |

The public source inventory is `content/dlc-manifest.json`: 51 files,
45,800,455 bytes, with per-file sizes and SHA-256 hashes. The original supplied
archive SHA-256 is
`f9fcd21043afbace9ef7f4891e968010f0f6871b916544d30fedca8a4b118a23`.
Neither the archive nor extracted game bytes belong in source control or CI
artifacts. Keep local application packaging separate from the source push.

## Prepare a local application build

Run once with the supplied archive, choosing an empty output directory:

```powershell
python scripts/prepare_dlc_pack.py --zip D:/Downloads/BruteForceDLC.zip --output work/native-launcher/dlc-pack
cmake -S . -B work/native-launcher/build -DBF_LAUNCHER_ONLY=ON -DBF_PRIVATE_DLC_DIR=D:/Documents/ChatGPT/bfRecomp/work/native-launcher/dlc-pack
cmake --build work/native-launcher/build --config Release --parallel 4
```

Preparation authenticates the archive and complete allowlist before writing.
It extracts only the three data directories. The CDX installers, their readme
instructions and regional title-update executables are not executed or copied.
The title-update entry point differs from retail; replacing default.xbe would
invalidate the current recompilation's code/address evidence.

Keep the resulting `assets/dlc` beside the launcher executable, along with its
other assets and Windows SDL2 DLL. End users of that local packaged application
do not select a separate DLC archive or run an installer. A bare source build
without the private payload can still import and launch the base game.

## Automatic verification and preservation

The launcher installs the data on disc import, Verify files and game launch.
The destination is `LIBRARY/player-data/TDATA/4d53001e/$c`; original title startup
maps this title directory to T: and enumerates T:\$C. Package names are normalized
to lowercase for portable lookup.

All 51 bundled files must match the compiled inventory. New complete package
directories are staged, rehashed and renamed into place. Every installed file
is checked again. Cancelling removes only the installer's new staging tree.
Existing matching packs are reused. Existing differing/incomplete packs are
reported and preserved for review, rather than silently replacing custom data.
Profiles, global progress, disc files and the original ZIP are not modified.
Unsafe paths and symbolic-link/junction destinations are rejected.

The launcher displays DLC installation status. A developer can also use
`BruteForceLauncher --workspace LIBRARY --verify-only`; a nonzero result indicates
verification/import failure. This mode does not launch gameplay.

## Evidence and remaining work

Windows and Linux installer tests pass with synthetic data, including staged
cancellation and save preservation. The actual Windows launcher installed all
51 supplied files into a separate QA library, and native verification accepted
all three packs. A DLC-seeded game run opened each pack's ContentMeta.xbx and
reached first-mission gameplay with HUD and scripted movement/fire.

This establishes installation and title discovery, not playable DLC maps.
Lockjaw completion, both multiplayer maps, their audio/FX, and any requirement
for title-update code remain unverified. One run also reproduced the existing
intermittent startup file-completion stall; retries progressed. Keep those
issues distinct from a failed DLC hash check and preserve ordinary player saves.
