# Requested playable-port scope

User requirements recorded 1 November 2026. These are targets, not claims of implemented or verified features.

## Core game first

- Boot into the real game, load and complete gameplay, transition between levels, and save/reload using isolated test saves.
- Exercise every campaign and multiplayer level and every playable character. Build a per-level/per-character test inventory from the actual game assets and menus; do not infer coverage from executable compilation.
- Preserve original gameplay and timing as the default. Keep optional enhancements and cheats modular.
- Windows, Linux and Android ARM64 targets. First Android device: Canadian Samsung Galaxy S24 Ultra.

## Front end launcher

- A polished Brute Force themed launcher, including textured panels, squad presentation and readable keyboard/mouse focus states.
- Ask for the user's local ISO, show extraction/verification progress, retain a local game library and start the compiled native host. Do not distribute the disc contents.
- Desktop implementation now provides this flow through a loopback-only local UI and native file dialog. The real 685-file disc import, native validation and Windows menu launch have been exercised. Artwork is original vector/CSS styling; original game textures are not bundled.
- Native Android application UI, Storage Access Framework ISO selection/import, lifecycle and on-device launch remain pending. The desktop launcher does not establish Android application support.

## Enhancement menu and input

- Full native mouse-and-keyboard support is a core requirement, explicitly requested by the user: mouse look/aim, keyboard movement and actions, rebinding, sensitivity/inversion, cursor capture/release and complete menu navigation. Keyboard-as-gamepad diagnostics are not completion of this requirement.

- A custom in-game menu usable entirely with a controller, with consistent focus, Back/Confirm behavior, readable scaling and safe return of input to gameplay.
- Support controllers through a cross-platform mapping layer: Xbox, PlayStation, Switch and generic devices, with remapping, dead zones, rumble and hot-plug handling. Provide a binding fallback for devices without a known mapping. Actual supported hardware must be tested; do not claim that every controller model was verified.
- Keep gameplay, menu input, graphics settings, cheats, multiplayer settings and persistent configuration in separate modules.
- Include noclip and additional cheats whose effects are established against the actual game state. Host-authoritative multiplayer policy and visible session settings must prevent contradictory simulation state. Never silently rewrite permanent progress to implement a cheat.

## Graphics and sound

- Correct widescreen aspect, projection, HUD, split-screen, menus and videos; widescreen must not stretch the original image.
- Optional HD texture replacements, original-resource fallback, bounded texture memory and a user-supplied texture-pack workflow. Do not bundle copyrighted replacement assets.
- Validate sound against actual gameplay: sample formats, scheduling, pitch, channel layout, streaming boundaries, clipping, underruns, synchronization and pause/resume. A clean build is not evidence of crisp audio.
- Test stable frame pacing and original simulation speed before offering higher frame rates or render scale.

## Multiplayer

- Create a hosted game from the in-game menu; join another host's game. Treat P2P as a player-hosted authoritative session with explicit lifecycle and disconnect handling.
- Establish LAN/direct-address joining first. Internet joining also needs a tested NAT traversal/relay or port-forwarding strategy; an address text field alone does not establish internet reachability.
- Increase the player limit only after identifying the original limits and all dependent player arrays, replication, spawn logic, UI, teams, assets and bandwidth assumptions. The new cap remains to be selected and validated; no arbitrary cap is claimed implemented.
- Test every multiplayer map, all character choices, host departure, reconnect/failure paths, incompatible versions and simultaneous inputs on independent clients.
- Do not use packet interception/capture against other players or public services as part of development.

## Current boundary

Windows renders original menus. Native Linux now renders the loading screen but does not reach a verified menu; the sampled guest thread waits for an asynchronous read flag at `0x00265B80`. The Android translation audit compiles all 227 translation units for ARM64, but there is no integrated Android application. The enhancement menu, cheats, expanded multiplayer, texture replacement and complete input/audio/level/character validation remain pending. Existing measurements are in STATUS.md.
