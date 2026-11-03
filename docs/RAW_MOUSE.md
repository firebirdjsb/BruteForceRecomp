# Native relative mouse look

With **Mouse and keyboard controls** enabled in the Windows launcher, relative
Windows Raw Input counts feed the local gameplay camera directly. Sensitivity
is shared by launcher Input & audio and F1 > Controls. Controller look remains
on its original path. No setting migration or save modification is needed.

The previous callback wrote processed right-stick axes after the first clamp.
That was insufficient: original bindings clamp again, and the player camera
applies a response curve, time ramp, aim assistance and slowdown. Mouse input
now bypasses those stages at the camera handoff. It produces no virtual
right-stick motion in the default mode. `BF_MOUSE_RESPONSE=legacy` remains a
developer comparison path only.

Each relative count requests 0.001 radians times user sensitivity. The bridge
converts angular displacement into the original pitch/yaw request units using
the current model turn rate and frame seconds. Time does not scale or accelerate
the raw count itself. Physical pitch limits, camera enable flags, original
simulation timing and authored weapon/camera effects remain authoritative.
This is a camera input change, not a physics/timestep patch.

## Authenticated handoff and ownership

`cmake/native_camera.cmake` authenticates the complete canonical translation
before inserting a native callback at original `0x00133EA3` in `0x001332A0`.
The public toolkit and original executable are unchanged.

The input component's `+0x20` is a logical device ID, not a zero-based physical
port. Its resolved logical device is retained at frame `+0x40`. The bridge
follows the input manager's existing 12-byte mapping records, then validates
the physical pad's port. Gameplay commonly inserts a virtual pad proxy;
original `0x001F0BB0` resolves that proxy's `+0xC4` physical pad pointer. The
native handoff follows that same validated pointer. Treating logical ID1 as
physical port1, or inspecting the proxy's unused port field, rejects valid input.

All reads and the eight-byte pitch/yaw stack-local write are bounded and checked.
Invalid/unowned/protected memory retains original behavior. Focus loss, text/F1,
menus and controller activity release native ownership. A new camera epoch or
a gap above 250 ms discards accumulated loading/movie motion. Button prompts
can be disabled without disabling mouse input.

## Evidence and limits

- The real patched raw-input reader passes count accumulation, no count cap,
  sensitivity, neutral virtual right-stick output, focus/menu release and stale
  motion rejection tests.
- Tests execute the original translated controller clamp and pitch/yaw setters.
  Equal counts produce equal requested angular displacement across frame times;
  direct/proxy ownership, other ports, invalid/read-only memory, original pitch
  limits and exact argument cleanup are covered.
- A bounded read-only live inspection identified the actual logical ID1 to
  proxy to physical port0 chain. The corrected live log confirms ownership and
  nonzero mouse counts reaching the camera. The user then said “this is the fix”
  and “this feels good.” No further response tuning was applied after acceptance.

The input path still updates with the original simulation. Optional visual
interpolation separately adds one source-frame presentation delay; it does not
sample the game camera at 120 Hz. Linux/Android raw camera integration, complete
controller hotplug and all-character/vehicle/cutscene acceptance remain open.
