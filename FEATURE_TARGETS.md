# Requested playable-port scope

Port development targets. Implemented features and their validation are tracked separately in STATUS.md.

## Core game first

Immediate priority: get campaign gameplay rendered and controllable, then establish a sustained 30-FPS-or-better baseline. Current geometry-allocation and CRT fixes do not establish playable gameplay; GPU command-stream handling remains a blocker.

- Boot into the real game, load and complete gameplay, transition between levels, and save/reload using isolated test saves.
- Exercise every campaign and multiplayer level and every playable character. Build a per-level/per-character test inventory from the actual game assets and menus; do not infer coverage from executable compilation.
- Preserve original gameplay and timing as the default. Keep optional enhancements and cheats modular.
- Windows, Linux and Android ARM64 targets. First Android device: Canadian Samsung Galaxy S24 Ultra.

## Front end launcher

- A polished Brute Force themed launcher, including textured panels, squad presentation and readable keyboard/mouse focus states.
- Ask for the user's local ISO, show extraction/verification progress, retain a local game library and start the compiled native host. Do not distribute the disc contents.
- The desktop front end is a C++ SDL2/Dear ImGui application. Its real 497-file core-game import was exercised; 188 demo, update and promotional files are excluded. The former browser launcher has been removed. Title art and icons are extracted locally from the verified user-supplied XBE and kept private.
- Native Android application UI, Storage Access Framework ISO selection/import, lifecycle and on-device launch remain pending. The desktop launcher does not establish Android application support.

## Enhancement menu and input

- Full native mouse-and-keyboard support is a core requirement, explicitly requested by the user: mouse look/aim, keyboard movement and actions, rebinding, sensitivity/inversion, cursor capture/release and complete menu navigation. Keyboard-as-gamepad diagnostics are not completion of this requirement.
- Game menus must support pointing and clicking their visible items. The live generic buttons, lists and on-screen keyboard now have native mouse hit testing, with original game focus/selection handlers and normal input activation. A scripted Windows main-menu Options hover/click/Back transition passed. Full widget/player-screen coverage and Linux interactive QA remain pending. Profile/campaign names use native Windows/SDL text events; scripted Windows edits, Backspace, length limits, Done and Back were exercised. Physical-keyboard and unsupported character-page/layout coverage remain pending.

- F1 now opens a native overlay with live input/prompt, FSR, scale, window-size and master audio settings. Mouse/keyboard operation is implemented; full controller operation, persistence and safe handoff during actual gameplay still need validation. Cheats remain unavailable until verified game-state hooks exist.
- Support controllers through a cross-platform mapping layer: Xbox, PlayStation, Switch and generic devices, with remapping, dead zones, rumble and hot-plug handling. Provide a binding fallback for devices without a known mapping. Actual supported hardware must be tested; do not claim that every controller model was verified.
- Keep gameplay, menu input, graphics settings, cheats, multiplayer settings and persistent configuration in separate modules.
- Include noclip and additional cheats whose effects are established against the actual game state. Host-authoritative multiplayer policy and visible session settings must prevent contradictory simulation state. Never silently rewrite permanent progress to implement a cheat.

## Graphics and sound

- Optional native DX12 on Windows, with D3D11/GDI fallbacks. AMD FSR 1 uses actual EASU and RCAS compute shaders. Neither technology is a mandatory hardware requirement. GPU readback validation is distinct from gameplay performance measurement.
- NVIDIA DLSS and temporal FSR need actual scene depth, motion vectors, camera jitter/history and correct resource lifetimes from the game renderer. These inputs are not exposed by the current software renderer; their integration remains pending. Do not advertise support from a toggle or fabricate these inputs.
- Correct widescreen aspect, projection, HUD, split-screen, menus and videos; widescreen must not stretch the original image.
- The F1 window presets cover 720p through 2160p, with desktop fitting. Fixed 2x/4x/8x FSR intermediate scales passed hardware readback checks. The original game source remains 640x480 and 4:3: these display controls do not establish native widescreen, higher scene resolution or a performance gain.
- Optional HD texture replacements, original-resource fallback, bounded texture memory and a user-supplied texture-pack workflow. Do not bundle copyrighted replacement assets.
- Validate sound against actual gameplay: sample formats, scheduling, pitch, channel layout, streaming boundaries, clipping, underruns, synchronization and pause/resume. A clean build is not evidence of crisp audio.
- Test stable frame pacing and original simulation speed before offering higher frame rates or render scale.
- Support optional display interpolation above 120 FPS while retaining original simulation/physics, AI, animation and input timing. Count simulated, rendered and generated frames separately. Do not unlock the simulation, change delta time, or label repeated frames as interpolation. This is not yet implemented or verified.

## Multiplayer

- Create a hosted game from the in-game menu; join another host's game. Treat P2P as a player-hosted authoritative session with explicit lifecycle and disconnect handling.
- Establish LAN/direct-address joining first. Internet joining also needs a tested NAT traversal/relay or port-forwarding strategy; an address text field alone does not establish internet reachability.
- Increase the player limit only after identifying the original limits and all dependent player arrays, replication, spawn logic, UI, teams, assets and bandwidth assumptions. The new cap remains to be selected and validated; no arbitrary cap is claimed implemented.
- Test every multiplayer map, all character choices, host departure, reconnect/failure paths, incompatible versions and simultaneous inputs on independent clients.
- Do not use packet interception/capture against other players or public services as part of development.

## Current boundary

Windows renders original menus. Native Linux now renders the loading screen but does not reach a verified menu; the sampled guest thread waits for an asynchronous read flag at `0x00265B80`. The Android translation audit compiles all 227 translation units for ARM64, but there is no integrated Android application. The enhancement menu, cheats, expanded multiplayer, texture replacement and complete input/audio/level/character validation remain pending. Existing measurements are in STATUS.md.
