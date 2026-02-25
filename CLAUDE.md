# vkwave - Async GPU Rendering Engine

## IMPORTANT — Check Coverage Before Implementing New Code

Before writing new functionality, **always consult the LLVM coverage report** to see if existing code already provides what you need. The codebase has ~370 uncovered functions across core wrappers, loaders, and pipeline utilities that may already do what you're about to write.

```bash
# Regenerate the coverage report
cmake --build build/linux-clang-coverage --target coverage --config Debug

# Browse per-file HTML report
xdg-open build/linux-clang-coverage/coverage-html/index.html
```

Key areas with existing unused functionality:
- **Camera** — full orbit camera with pan, dolly, zoom, yaw, pitch, roll, azimuth, elevation
- **Loaders** — glTF (with materials), PLY, IBL (BRDF LUT, irradiance, prefiltered cubemaps)
- **Texture** — image creation, upload, layout transitions, sampler
- **AccelerationStructure** — BLAS/TLAS build for ray tracing
- **RayTracingPipeline** — SBT creation, trace dispatch
- **Representation** — Vulkan enum→string for all common types
- **Fence** — block, reset, status (superseded by timeline semaphores but still available)

Reuse before reimplementing. If an existing function almost fits, extend it rather than duplicating.

## REQUIREMENTS - Non-Negotiable Design Principles

### 1. GPU Pass Overlap Across Frames

All render passes MUST be able to execute overlapping across frames on the GPU. For example, PrePass[F+1] MUST be able to execute simultaneously with Intermediate[F] on the GPU. This is the entire point of this engine.

Shaders are reentrant. If passes operate on different input/output images (indexed by frame), the GPU can execute them simultaneously. This is how real-time rendering achieves high throughput.

**What this means in practice:**
- Per-phase command buffer submissions (not one monolithic command buffer per frame)
- Timeline semaphores for fine-grained inter-pass dependencies
- No full memory barriers between passes (this serializes the GPU)
- Resource pools indexed by frame, not global shared state

### 2. Semaphore DAG for Pass Ordering (No Overtaking)

Passes within a frame execute in a defined order. The ordering is enforced by a semaphore dependency DAG, not by barriers. This prevents write-after-read hazards (overtaking) while allowing maximum GPU parallelism across frames.

Each pass signals a semaphore on completion. Dependent passes wait on that semaphore before starting. The graph builds this DAG from declared pass dependencies.

### 3. Graph Owns All Vulkan Resources — RAII Wrappers + Compile-Time Enforcement

All Vulkan resources are wrapped in RAII classes with non-trivial destructors (like the existing `Fence`, `Semaphore` wrappers). The render graph owns these wrappers. Passes hold only raw `vk::` handles received from the graph.

**Why:** If passes own resources, they can't be ring-buffered by the graph. Ring-buffering is required for GPU pass overlap (pass N+1 needs its own copy of mutable resources while pass N is still in flight).

**Resource struct:** All graph-owned resources live in a single struct. Passes reference this struct and are built up incrementally — they are classes with state (configuration, cached handles), not stateless functions. But they must not own Vulkan resources.

**Compile-time ownership check:** Passes inherit from a CRTP base that enforces non-ownership via `std::is_trivially_destructible`. Since all Vulkan resources are wrapped in RAII classes (non-trivial destructor), any pass that accidentally owns a resource triggers a compile error.

```cpp
template <typename Derived>
struct Pass
{
  Pass()
  {
    static_assert(std::is_trivially_destructible_v<Derived>,
      "Passes must not own Vulkan resources — store them in the graph's resource struct");
  }
};
```

**Why the static_assert is in the constructor, not the destructor:** A user-provided destructor (`~Pass()`) would make `Pass<Derived>` non-trivially-destructible, which propagates to all derived classes — making the check always fail regardless of what members they have. A user-provided *constructor* does NOT affect trivial destructibility, so the check works correctly. Every pass that inherits from `Pass<Derived>` gets the check automatically when constructed.

This works because:
- Raw `vk::Pipeline`, `vk::Image` handles are trivially destructible (just uint64 wrappers) — OK to hold as cached copies
- RAII wrappers (`OwnedPipeline`, `OwnedImage`, `Fence`, `Semaphore`) have destructors — caught by static_assert
- `std::vector`, `std::unique_ptr`, etc. — also caught

Passes are classes that can be built up incrementally with configuration, references to the resource struct, and cached raw handles. They just can't own the resources themselves.

### 4. Passes Are N-Unaware

Passes never know how many frames are in flight. They receive a `frame_index` from the graph and use it to index into resource arrays. The graph decides N and allocates N copies of mutable resources. Changing the ring depth is a graph-level decision with zero pass changes.

### 5. Per-Phase Command Buffer Submissions

Each phase is a separate `vkQueueSubmit` with its own command buffer. This is what enables the GPU to overlap passes from different frames. A single command buffer per frame forces sequential execution.

### 6. Async Compute

Compute passes (e.g., screen-space blur) should use a separate compute queue when available. This enables true GPU parallelism: compute work from frame F overlaps graphics work from frame F+1 on different hardware units.

## Architecture — Per-Swapchain-Image Resource Sets

The proven architecture (from the original Wolfenstein project that ran fully overlapping at 12 kHz):

Each swapchain image has its own self-contained resource set — command buffer, descriptors, framebuffers, storage images. The graph owns N copies of these resource structs (one per swapchain image). Passes index into them by `frame_index`.

**Overtaking prevention:** A fence per slot. The CPU waits on the fence before reusing a slot, limiting how far ahead it can get. This is the only CPU-GPU synchronization needed.

**GPU overlap happens naturally:** Passes from different frames operate on different resource sets, so no barriers are needed between them. The GPU overlaps them because there are no data dependencies.

**Intra-frame ordering:** Timeline semaphores chain passes within a single frame. Each pass signals on completion; the next pass waits before starting.

### Target Pipeline

```
Depth PrePass (raycast) → Scene (HDR) → SSS Blur (compute) → Tonemap/Composite
```

Each phase submits independently. Across frames, the only synchronization is the per-slot fence.

## THINGS TO AVOID

### Full Memory Barriers Between Passes
This is the Inexor anti-pattern. Full barriers (`VK_PIPELINE_STAGE_ALL_COMMANDS_BIT`) between every pass serialize the entire GPU. Use fine-grained semaphores instead.

### Passes That Own Resources
This was the Vulkanstein3D anti-pattern. "Self-contained stages" that own their descriptors, framebuffers, and images prevent the graph from ring-buffering resources. The graph cannot manage what it does not own.

### Monolithic Command Buffers
One command buffer per frame prevents GPU overlap across frames. Each phase needs its own command buffer and queue submission.

### Global Shared Mutable State
Any mutable state shared between passes without frame indexing creates data races when passes from different frames overlap.

### ImGui as a Separate Submission Group

ImGui is a lightweight overlay — a few textured triangles drawn on top of the final image. Making it its own `SubmissionGroup` with a dedicated command buffer, command pool, render pass, timeline semaphore, and `vkQueueSubmit` adds massive per-frame CPU overhead for no GPU benefit:

- Extra `vkQueueSubmit` (~0.1-0.3ms kernel-mode transition on NVIDIA)
- Extra command pool reset + command buffer record
- Extra timeline semaphore wait in next frame's `begin_frame()`
- Extra binary semaphore pair (acquire wait + present signal)

This was measured: vkwave rendering a single cube at 220 Hz while Vulkanstein3D rendering a full PBR scene with glTF models, ray tracing, SSS blur, and multiple passes at 260 Hz — on the same GPU, same present mode. A simple cube was slower than a heavy scene because the extra `vkQueueSubmit` for ImGui dominates CPU time at high frame rates.

**Correct approach:** ImGui records into the last submission group's command buffer. Either as a second render pass within the same command buffer (no extra `vkQueueSubmit`), or ideally as a callback at the end of the final render pass. This is what Vulkanstein3D does — `UIStage::record()` is a single line that calls `ImGui_ImplVulkan_RenderDrawData()` into the composite pass's command buffer. Zero extra submissions, zero extra synchronization.

ImGui does not need its own submission group because it never participates in cross-frame GPU overlap — it always draws to the swapchain image that's about to be presented, and it has no downstream dependents.

**GPU-side cost — dependency chain extension:** The extra submit doesn't just cost CPU time. It extends the GPU-side semaphore dependency chain: Composite signal → ImGui wait → ImGui execute → ImGui signal → Present wait. With inline recording this collapses to: Composite (includes ImGui draws) signal → Present wait. The longer chain means the graphics queue stays occupied longer per frame, which delays when the acquire semaphore signals for the next frame. This shrinks the cross-frame overlap window — exactly the thing this engine exists to maximize. With async compute in the target pipeline, the graphics queue needs to be free to start Frame N+1's early passes while Frame N's compute blur is still running. An ImGui submit sitting between composite and present eats into that window.

**Industry guidance confirms this:** NVIDIA documents that each `vkQueueSubmit` has "significant performance cost on CPU." The OS scheduling overhead per submission is 50-80 microseconds. The rule of thumb is fewer than 10 submits per frame, each with at least 0.5ms of GPU work — ImGui overlay triangles will never meet that threshold.

### Frame-Count-Based Animation
Never drive animation speed from `cpu_frame()` or any frame counter. Frame rate varies with window size, GPU load, and present mode, so animations tied to frame count run at unpredictable speeds. Always use wall-clock elapsed time (`std::chrono::steady_clock`).

## Debug Visualization

No extra passes or pipelines for debugging. A single `int debugMode` push constant in the fragment shader switches output:

```glsl
switch(pc.debugMode) {
  case 0: outColor = finalHDR; break;         // normal
  case 1: outColor = vec4(N * 0.5 + 0.5, 1); break;  // normals
  case 2: outColor = vec4(roughness);  break; // roughness
  case 3: outColor = vec4(metallic);   break; // metallic
  case 4: outColor = vec4(ao);         break; // AO
  case 5: outColor = texture(thicknessMap, uv); break; // thickness
}
```

Toggle via ImGui. Dead branches are skipped by GPU predication — zero cost. For fullscreen texture inspection, the composite pass can skip tonemapping and sample a specific binding directly (another debug mode value). No separate debug stages, pipelines, or descriptor sets.

## Incremental Plan

### PRIORITY FIX — Merge ImGui into last submission group

**Must be done before any new milestone work.** The current `ImGuiGroup` is a separate `SubmissionGroup` with its own `vkQueueSubmit`, command pool, render pass, and timeline semaphore. This adds 50-80us of CPU overhead per frame for trivial GPU work, making a single cube slower than Vulkanstein3D's full PBR scene. Fix: record ImGui draw commands into the last `ExecutionGroup`'s command buffer (as a second render pass or end-of-pass callback). Delete `ImGuiGroup` as a `SubmissionGroup` subclass. Validate: frame rate should match or exceed Vulkanstein3D on the same GPU and present mode.

### Milestone 1 — Prove the Architecture (single pass, overlapping frames)

1. **Fullscreen triangle + push constant color.** One render pass writes to the swapchain. Push constant holds a color that changes each frame (e.g. cycle hue by frame number). Validates: pipeline, render pass, swapchain present.

2. **Per-swapchain-image resource sets.** N command buffers, N fences. CPU acquires image, waits on that slot's fence, records, submits, presents. Validates: ring-buffered resources, no single-frame bottleneck.

3. **`eImmediate` present mode.** No vsync, frames go as fast as possible. This is where overlap becomes observable — the GPU should be working on multiple frames simultaneously.

4. **Window resize.** Drain all fences, recreate swapchain + size-dependent resources, resume. Validates: teardown/rebuild path.

5. **ImGui overlay.** Record ImGui draw commands into the last submission group's command buffer — not as a separate submission group. Push constant debug controls (color picker, frame counter display). Validates: ImGui integration without extra submission overhead.

### Milestone 2 — 3D Scene (single pass, real geometry)

6. **UBO with camera + model transforms.** Per-frame UBO (ring-buffered). Fullscreen triangle replaced by a spinning triangle/cube. Push constants for per-draw model matrix. Validates: UBO ring-buffering, descriptor sets per frame.

7. **Vertex/index buffers + depth buffer.** Load a simple mesh. Add depth attachment (per-frame or shared, depth is written and consumed within same frame). Validates: depth testing, vertex input.

8. **glTF model loading.** Use existing `gltf_loader`. Load DamagedHelmet or similar. Validates: mesh + material pipeline, texture binding.

9. **Textures + PBR fragment shader.** Base color, normal, metallic-roughness, AO, emissive. Reuse Vulkanstein3D's `fragment.frag` PBR logic. Debug mode push constant for visualizing individual channels. Validates: material descriptor sets, sRGB vs UNORM formats.

### Milestone 3 — HDR + Post-Processing (multi-pass pipeline)

10. **HDR render target.** Scene renders to R16G16B16A16Sfloat image instead of swapchain directly. Validates: offscreen render target, per-frame HDR image.

11. **Composite pass (tonemap + gamma).** Fullscreen triangle samples HDR image, applies exposure + tonemap + gamma, writes to swapchain. Two passes in the frame, chained by semaphore. Validates: multi-pass with semaphore DAG, HDR workflow.

12. **IBL — environment lighting.** Generate BRDF LUT, irradiance, prefiltered cubemaps (GPU compute). Shared read-only across all frames. Bind in scene pass descriptors. Validates: shared immutable resources, compute shader generation.

### Milestone 4 — Advanced Passes (full target pipeline)

13. **Depth prepass via raycast (compute).** Compute pass writes depth before the scene pass. Separate command buffer + queue submit. Scene pass waits on prepass semaphore. Validates: compute → graphics dependency, per-phase submission.

14. **SSS back-lighting in fragment shader.** Barré-Brisebois model using thickness. Alpha channel marks SSS materials. Validates: SSS material pipeline, thickness texture.

15. **SSS blur (compute pass).** Separable 13-tap blur in compute, reads/writes HDR image. Runs between scene pass and composite pass. Per-channel diffusion widths, alpha masking. Validates: graphics → compute → graphics chain, three-phase pipeline.

16. **Async compute queue.** SSS blur on dedicated compute queue. Overlaps with next frame's early passes on the graphics queue. Validates: true multi-queue GPU parallelism.

### Milestone 5 — Polish

17. **MSAA.** Multisample resolve in scene render pass.
18. **Back-face culling.** Dynamic cull mode via `VK_EXT_extended_dynamic_state`.
19. **Multiple models / scene composition.** Add objects at runtime, separate VBO/IBO per object, TLAS rebuild.
20. **Screenshot / capture system.**
21. **Resize without FIFO blink.** During drag-resize the main loop skips rendering (`continue` after `handle_resize()`) to avoid black frames from uninitialized HDR images. In immediate mode the next frame presents in microseconds so the skip is invisible. In FIFO mode the compositor waits for the next vsync (~16ms at 60Hz) before receiving a new frame, so stale buffer content blinks at the window edges during rapid resize. The fix is to render during resize: after recreating the swapchain and HDR images, do a full PBR+composite pass before continuing. This requires the freshly allocated HDR images to contain valid data — either pre-clear them to black via `vkCmdClearColorImage`, or accept one frame of black and render normally (removing the `continue`). The black-frame approach was tested and is worse — the entire window flashes black each resize event. Pre-clearing to a neutral color and rendering in the same iteration is the cleanest path.

### Refactoring — Zero-Arg Constructor + Setter Pattern

Convert remaining multi-arg constructors to zero-arg + setters + `init()` (same pattern as `Instance`):

- [ ] **Device** (8 params)
- [ ] **Swapchain** (7 params)
- [ ] **Window** (6 params)
- [ ] **ImGuiOverlay** (6 params)
- [ ] **Image** (6 params)
- [ ] **Texture** (6 params)
- [ ] **ExecutionGroup** (5 params)
- [ ] **Buffer** (5 params)
- [ ] **DepthStencilAttachment** (5 params)

Each conversion follows the same steps: add `m_modified` flag, replace constructor args with setters, move body to `init()`, assert `!m_modified` in accessors. Dependent types in calling code wrap in `std::optional` and use `emplace()`.

### Milestone 6 — Dual-GPU Composite (post-refactoring)

On PRIME/Optimus laptops the dGPU (NVIDIA) renders offscreen and the iGPU (Intel) owns the display controller. Currently the composite pass (tonemap fullscreen triangle) runs on the dGPU and the result is copied to the iGPU for presentation — wasting dGPU cycles on trivial work and adding a cross-GPU copy.

**Idea:** Run the composite pass on the iGPU directly. The PBR pass produces the HDR image on the dGPU, then `VK_KHR_external_memory` shares it with the iGPU. The iGPU runs the tonemap + ImGui overlay and presents. This frees the dGPU to start the next frame's PBR work immediately.

**Observations from dual-GPU testing:**
- In PRIME mode, only FIFO and Mailbox present modes are available (iGPU display controller limitation).
- With hwmux (NVIDIA-only), all four present modes are available.
- The composite pass is a single fullscreen triangle — negligible GPU load, well within iGPU capability.

**Requires:** Zero-arg constructor refactoring (Device, Swapchain, etc.) must be complete first, since this needs two Device instances with different physical devices and careful resource lifetime management.

## Troubleshooting

### NVIDIA GPU reports no presentation support (PRIME/Optimus laptops)

If the NVIDIA GPU doesn't appear in `vulkaninfo` or claims no presentation support after a reboot, the `nvidia_drm` kernel module likely didn't load. Verify with `lsmod | grep nvidia_drm`. If missing:

```bash
sudo modprobe nvidia_drm modeset=1
```

Confirm fix: `ls /dev/dri/` should show a second card/renderD node. To make persistent:

```bash
echo 'options nvidia_drm modeset=1' | sudo tee /etc/modprobe.d/nvidia-drm.conf
sudo update-initramfs -u
```

**Root cause:** On hybrid Intel+NVIDIA systems, `nvidia_drm` creates the DRI device nodes needed for Vulkan surface presentation. Without it, only the Intel iGPU is visible to the Vulkan loader. The `nvidia` and `nvidia_uvm` modules alone are not sufficient — `nvidia_drm` (which pulls in `nvidia_modeset`) is required for display output, especially on Wayland.

### Performance: Switch to NVIDIA-only mode with hwmux

On PRIME/Optimus laptops, switching to NVIDIA-only mode using `hwmux` increases framerate from ~1500 Hz to ~12000 Hz. The hybrid rendering path adds significant overhead; bypassing it gives the GPU direct display access.

## Build

```bash
cmake --preset linux-gcc
cmake --build build/linux-gcc --config Debug
```

## Project Structure

```
vkwave/
  config.h.in          # Generated compile-time config
  core/                 # Vulkan primitives (device, instance, buffer, image, etc.)
  pipeline/             # Pipeline, shaders, framebuffer, acceleration structures
  loaders/              # glTF, PLY, IBL loaders
app/
  main.cpp              # Application entry point
```

## Dependencies

- Vulkan SDK (system)
- spdlog (FetchContent fallback)
- GLFW (FetchContent fallback)
- Dear ImGui (FetchContent)
- cgltf (FetchContent, header-only)
- stb (FetchContent, stb_image)
