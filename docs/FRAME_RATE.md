# Frame-rate work and remaining milestones

## Player controls

Windows GPU builds have an opt-in **Up to 60 FPS (experimental)** setting in
the launcher's Graphics section and **F1 > Renderer > Frame rate**. F1 applies
it live. Original pacing remains the default. The setting is unavailable in
the active CPU reference renderer. Renderer/device changes still need a restart.

The shared version-1 preferences append `frame_limit` after mouse sensitivity.
Files ending after volume or sensitivity retain all their old values and use
30 FPS. Only 30 and 60 are accepted. Launchers check `host_fps60=1` before
launching an older game application with the new setting selected.

This shortens the host's TWO/TWO_OR_IMMEDIATE presentation interval to ONE,
preserving the framebuffer address, method number, IMMEDIATE flag, other
intervals and software commands. Vblank remains 60 Hz. Original guest clocks,
physics instructions and memory fields are unchanged. It does not implement
render interpolation, frame generation or 120+ FPS gameplay.

## Evidence from 2 November 2026

Bounded, state-driven first-mission runs used separate scratch saves, authored
HUD captures, scripted movement/camera/fire and actual NV2A flip hashes on an
RTX 3060 Ti. The table uses the last 25 seconds, after tutorial dismissal and
the input sequence. These are automated smoke tests, not completed missions.

| Mode | Presented FPS | Distinct pictures | Physics seconds / wall second | P95 frame gap |
| --- | ---: | ---: | ---: | ---: |
| Original D3D11 pacing, control | 30.00 | 100% | 0.999983 | 35.31 ms |
| D3D11, one-vblank experiment | 54.17 | 100% | 0.999989 | 19.69 ms |
| D3D12/11On12, saved 60-FPS setting | 34.46 | 100% | 1.000008 | 36.20 ms |
| Final D3D11 player, probes compiled out | 51.72 | 100% | Not instrumented | 20.65 ms |

The minimum original physics step stayed `0.0151515156` seconds (1/66) in
both modes. This rules out a doubled physics clock in the sampled window;
it does not establish complete movement, collision, AI, animation, audio,
cutscene or multiplayer equivalence. The bridge remains slower on this host.
The 60-FPS label is a target, not a performance guarantee.

Private traces are under `work/high-fps/interval-1-gxn0k3eq` (control;
the first probe did not change TWO_OR_IMMEDIATE), `interval-1-ubkn9kn8`
(corrected D3D11 interval), `interval-original-j34sz557` (saved setting
through D3D12), and `interval-original-zb5oc_fi` (final normal player). The
normal player reached the mission with HUD and ammo reduced from 48 to 35
after scripted fire. Some sparse BMPs are report-time intermediate targets
when the capture flag is off; use actual per-flip hashes for presented-frame
metrics. Captures, original code and game data are not tracked.

All 21 Windows native tests, both GPU image-parity suites, both Windows/Linux
launcher tests, and Linux pacing/UI tests pass. Python checks include fixtures
where a 60-FPS presentation incorrectly doubles simulation or repeats pictures,
so the analyzer cannot silently call either a successful gameplay result.

## Reproducing timing diagnostics

Use an isolated build output and disposable saves, never the permanent player
library. Configure `BF_FRAME_TIMING_DIAGNOSTICS=ON` on a private GPU build.
`BF_FRAME_TIMING_CSV=<absolute path>` records body samples plus a sibling
`.clock.csv` for scheduler samples. `BF_TEST_FRAME_INTERVAL=1` is an isolated
interval experiment; leave it unset to test the actual saved user preference.
The launcher strips both diagnostic variables from child environments.

The probes are exact generated-source SHA-256 gated. Original 0x002C6A7B
observes the effective timestep before `FSTP [ESI+0x78]`; original 0x002C7D00
observes the scheduler's incoming target double at ESP+12. Dynamic world/body
pointers are read within committed low guest RAM. No code injection, guest
stack modification or allocated memory cave is used. The retail XBE layout
and bytes were separately checked with `scripts/verify_offsets.py` and local
disassembly. The normal player is built with diagnostics **OFF**.

```powershell
python scripts/analyze_frame_timing.py --frames <frames.csv> --clock <timing.csv.clock.csv> --json <report.json>
```

The report includes picture-hash availability, real flip rate, frame-gap
distribution and each observed physics-clock rate. It always marks manual
gameplay verification false. Choose a window outside loading, pause and
tutorial screens before interpreting the clock slope.

## Ordered remaining work

1. **Higher FPS:** broaden 30/60 comparisons across movement distance, camera
   rotation, fire cadence, AI, collision, abilities, audio and cutscenes. Profile
   the D3D12 bridge separately. Real 120+ render interpolation needs render
   history and simulation/render separation; do not raise the 60-Hz clock.
2. **Native image quality:** add actual higher-resolution color/depth targets
   with coherent render-to-texture aliases, then adjust scene projection for
   16:9 while keeping HUD/menu coordinates correct. Temporal FSR/DLSS need
   valid scene depth, motion vectors, jitter/history, reset handling and HUD
   separation. Current FSR 1 and window sizes only scale the finished 4:3 image.
3. **Platforms:** connect actual NV2A draws to a Linux GPU backend. Android
   additionally needs the native runtime, SDL/activity lifecycle, storage import,
   input, audio, ARM64 linking and packaging; validate a real APK on the user's
   Canadian Galaxy S24 Ultra. Existing NDK/object audits are not an APK.
4. **Acceptance:** record each campaign mission and multiplayer map, four
   characters/abilities, audio transitions, physical controllers, hotplug,
   disconnect and shutdown. Synthetic coverage never marks those complete.
5. **Enhancements/networking:** authenticated runtime cheat integration and
   live F1 controls; then host/join lifecycle, two real peers, disconnect/rejoin
   and packet/state compatibility. Raise player limits only after auditing
   original fixed arrays, allocation, replication, UI and mission scripts.

No expanded player limit, P2P implementation, cheats, Linux GPU support,
Android APK, temporal reconstruction or native widescreen is claimed here.
