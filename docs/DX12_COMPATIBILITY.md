# DX12 and DX11 reuse contract

Windows has a native DX12 presenter and FSR 1 in `src/graphics/`. It presents
completed CPU-rendered frames. This document describes the additional contract
for a future DX12 NV2A scene renderer; scene draw commands are not connected.

## What is already shared

The original-Xbox/NV2A state machine, guest physical-memory and semaphore
addresses, texture format conversion rules, and generated guest code must
not know which host graphics API is in use.

| Concern | API-independent rule | Implementation / remaining DX12 work |
| --- | --- | --- |
| CPU texture sampling | Exact signed wrap/mirror/clamp and DXT-cache-slot math | `src/bf_texture_sample.h` applies to the software reference; DX11/DX12 GPU texture sampler and BC upload paths must pass separate equivalence tests rather than assume the CPU optimization speeds shaders |
| Command submission and guest fences | Do not publish a guest-memory semaphore before the actual required GPU work completes | `src/gpu/guest_completion_timeline.h` validates monotonic fence submission, in-order guest writes, device removal, invalid values and retry-on-write-failure |
| D3D12 GPU completion | A queue submission/Signal HRESULT is **not** completion | `src/gpu/d3d12_fence_bridge.h` submits a D3D12 fence signal and uses `ID3D12Fence::GetCompletedValue` to gate the API-neutral queue |
| DX11 and DX12 interoperability | DX11 work can execute on a D3D12 command queue through the official D3D11On12 path | Optional Windows WARP test creates a D3D11On12 device on a D3D12 queue. This does not adapt the project's NV2A renderer to DX11, nor establish a DX12 gameplay backend |
| Presentation/frame snapshots | Presenter must see complete frames, not half-copied memory | Private Windows `fb_present.c` SRWLOCK fix protects CPU/GDI snapshots. A DX12 GPU-present path needs its **own** resource states and completion fence; copying the Win32 lock alone is insufficient |
| Rendering validation | Compare only independently matched scenes, at the same dimensions and frame/flip identity | `tools/compare_frames.py` compares RGB differences and regions from separately captured CPU/DX11/DX12 images; does not infer synchronized game state |
| Audio, USB, imports and disk filter | No coupling to DirectX | Existing input, media, demo exclusion and guest logic remain shared across host backends |

## Guest fence rules

A future D3D12 NV2A backend must follow this order:

1. Translate and record the original Xbox graphics commands without changing
   the game's emulated ordering or omitting depth/stencil operations.
2. Submit the required draw, clear or copy work to an `ID3D12CommandQueue`.
3. Queue a matching `ID3D12CommandQueue::Signal` of a normal queue/CPU-sync
   `ID3D12Fence` using `bf::gpu::signal_gpu_queue`. Do not register a
   submission on failure.
4. Associate the emulated Xbox BACK_END_WRITE_SEMAPHORE_RELEASE with that
   submitted fence value and its original guest address/value.
5. Poll a **real** `GetCompletedValue()` through `poll_gpu_queue`.
   Invoke the caller's validated, mapped guest DWORD writer *only* once the
   fence has completed, and handle invalid addresses as write failures.
6. Treat `UINT64_MAX` from `GetCompletedValue` as device removal;
   never release all guest waits, silently signal success, or wrap fence values.
7. Preserve guest stack, interrupt, flush and register contracts independent
   of whether the host uses DX11, DX12, software or eventually Vulkan.

The proof-of-concept timeline is deliberately **not wired into**
`src/gpu_completion.cpp` or the patched NV2A executor yet. Doing so before
there is a real D3D12 submission path would fabricate GPU completion or
create deadlocks. The current original-XBE-authenticated live semaphore target
and command-triggered CPU executor fix are left intact.

## DX12 implementation sequence

1. Land and test an **API-neutral extended NV2A render contract**:
   vertex programs, combiners, sampler state, texture formats and render
   target/depth surface semantics. It should be a common definition used by
   both backend implementations, not a parallel DX12 copy of game logic.
2. Port a known-correct Windows D3D11 backend behind a switch, retaining
   the CPU reference. Test matched frames with the shared comparison tool.
3. Use D3D11On12 as an **optional bridge experiment**: D3D12 device/queue
   owns the underlying resources, D3D11On12 translates supported DX11 work.
   Acquire/release wrapped resources and flush the D3D11 context before
   downstream DX12 work. D3D11On12 is *not* a native optimized DX12 renderer.
4. Implement native DX12 with root signatures, PSOs, descriptor heaps,
   `ID3D12GraphicsCommandList`, DXIL/HLSL shader compilation and explicit
   resource-state transitions, including render-target/depth aliases,
   texture updates, CPU/Bink writes and guest readback. Use fence-based
   allocator, upload and swap-chain resource lifetime control.
5. Verify frame-to-frame equivalence, guest-memory readback, semaphore
   progression, clip/cull/stencil, HUD and cutscenes before making DX12
   a selectable gameplay renderer.
6. Keep Linux SDL and the Android/Vulkan direction independent of DirectX.

## Validation status

- The existing CPU texture path and Windows framebuffer changes were built
  against the exact pinned public runtime using synthetic fixture inputs.
- `gpu_completion_timeline_test.cpp` verifies no premature guest writes,
  monotonic ordering, failed guest writes, and device-removed sentinels.
- `d3d12_fence_bridge_test.cpp` can test real D3D12 WARP fence completion
  and D3D11On12 device creation on a Windows CI runner. CTest marks the case
  **Skipped**, not passed, when the runner lacks DXGI/D3D12 WARP support.
  The CI runner prints the test output verbosely so this distinction can be
  audited instead of inferring DX12 hardware support from a green check.
- `d3d12_texture_roundtrip_test.cpp` now exercises a 7x5 synthetic RGBA
  texture upload through a 256-byte-aligned row footprint, a D3D12 copy
  into an ordinary GPU texture, an explicit COPY_DEST-to-COPY_SOURCE
  barrier, GPU-to-readback transfer and a real completion fence. It compares
  every visible pixel byte against the original CPU pattern. No proprietary
  game data is used. Reports **Skipped** if a DX12 WARP device is absent.
- No full game, native DX12 draw/PSO, swap-chain, original-game texture
  coverage, or physical GPU/driver validation is claimed.

## API documentation

- [Microsoft D3D12 command queue and Signal](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12commandqueue)
- [GetCompletedValue and device-removal sentinel](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue)
- [Microsoft D3D11On12 interop and wrapped-resource lifecycle](https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12)
