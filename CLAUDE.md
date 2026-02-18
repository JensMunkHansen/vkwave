# vkwave - Async GPU Rendering Engine

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

### Milestone 1 — Prove the Architecture (single pass, overlapping frames)

1. **Fullscreen triangle + push constant color.** One render pass writes to the swapchain. Push constant holds a color that changes each frame (e.g. cycle hue by frame number). Validates: pipeline, render pass, swapchain present.

2. **Per-swapchain-image resource sets.** N command buffers, N fences. CPU acquires image, waits on that slot's fence, records, submits, presents. Validates: ring-buffered resources, no single-frame bottleneck.

3. **`eImmediate` present mode.** No vsync, frames go as fast as possible. This is where overlap becomes observable — the GPU should be working on multiple frames simultaneously.

4. **Window resize.** Drain all fences, recreate swapchain + size-dependent resources, resume. Validates: teardown/rebuild path.

5. **ImGui overlay.** Render ImGui as the last pass in the frame. Push constant debug controls (color picker, frame counter display). Validates: multi-pass within a frame, ImGui integration.

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
