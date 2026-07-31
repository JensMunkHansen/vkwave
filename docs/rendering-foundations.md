# Rendering Architecture Foundations

This document tracks the foundational engine work that advanced rendering
passes (transmission/volume, subsurface-scattering blur, async compute) are
built on. The guiding decision is **architecture first**: build these
foundations properly so each later pass *composes* on top of them, rather than
bolting on one-off machinery per feature.

Each foundation maps to a core requirement in `CLAUDE.md`:

| # | Foundation | Requirement | Status |
|---|------------|-------------|--------|
| F1 | Explicit pass-dependency DAG | #2 Semaphore DAG for pass ordering | **Merged** (PR #4) |
| —  | One-shot command unification | (tidy-up) | **Merged** (PR #5) |
| F2 | Async compute queue | #6 Async compute | **This branch** |
| F3 | Graph-owned, ring-buffered resources | #3 Graph owns all Vulkan resources | Planned |
| F4 | `Image` mips + compute downsample | — | Planned |
| —  | Transmission / volume (glass) | consumer of F1–F4 | Optional — see [transmission.md](transmission.md) |

Build order: **F1 → F2 → F3 → F4 → (transmission)**. Each lands as its own
branch/PR off the merged trunk, since F1 is the substrate the rest wire
through.

---

## F1 — Explicit pass-dependency DAG (merged)

**Problem.** The render graph hardcoded its ordering: offscreen groups
submitted with no inter-dependencies, and the present group's wait was pinned
to "the last offscreen group." There was no way to declare *"pass B depends on
pass A,"* which every multi-pass / multi-queue feature needs.

**Solution.**
- `SubmissionGroup` gained `depends_on()` / `clear_dependencies()` /
  `dependencies()` — groups declare the predecessors they must run after.
- `topo_order.h` provides a unit-tested `topological_order()` (Kahn's;
  smallest-index-first, so *no edges == identity order*; throws on cycle).
- `RenderGraph::build()` derives a topological submission order of the
  offscreen groups from the declared edges (indices only — storage order, and
  thus `offscreen_group(i)`, stays stable). `render_frame()` submits in that
  order, each group waiting on its predecessors' timeline signals; the present
  group waits on its declared dependencies, with a legacy fallback to the last
  offscreen group when none are declared.

**Why it matters here.** The waits are timeline-semaphore waits, which work
**across queues**. So F2's compute queue wires through the same `depends_on`
edges without re-touching the ordering core.

---

## F2 — Async compute queue

**Goal.** A dedicated compute queue so compute work (SSS blur, transmission
mip generation) can run *concurrently with* graphics work across frames — the
heart of the engine's overlap requirement.

**Scope — establish the queue only.** F2 selects and creates the queue and
exposes it; it deliberately does *not* add the per-group routing or a
compute-queue one-shot variant, because there is no compute consumer yet and
unused plumbing is speculative dead code. Those land with the first consumer
(F4's compute downsample / SSS blur), where they are testable.

- Select a compute queue family in `Device` — prefer a *dedicated* async
  family (COMPUTE without GRAPHICS); fall back to the graphics family when none
  exists (the spec guarantees graphics families also support compute, so the
  fallback always works).
- Expose `compute_queue()` and `has_dedicated_compute_queue()`.
- Skip requesting a duplicate `VkQueue` when the compute family coincides with
  an already-requested family.

When the first compute group is added: `SubmissionGroup::submit()` already
takes a `vk::Queue`, so routing is largely graph-side, and F1's DAG wires the
graphics↔compute timeline synchronization.

**Out of scope.** IBL generation stays on the graphics queue. It is a one-time
precompute at load with nothing to overlap, so async buys nothing there (and a
dedicated queue would add a queue-family ownership transfer for images the
graphics pass samples). The genuine async win for IBL is *hitch-free runtime
HDR switching* (regenerate on compute while continuing to render the old
environment, then swap) — a separate, optional feature, not part of F2.

---

## F3 — Graph-owned, ring-buffered shared resources

This is the broad foundation: it moves resource *ownership*, not just adds a
capability. Today ownership is split across three layers and the app/graph seam
is tangled:

- `ExecutionGroup` owns **its own depth buffer** (`m_depth_buffer`) — so two
  passes cannot share one depth buffer.
- `ScenePipeline` (the app's scene manager) owns **GPU resources** the graph's
  passes consume: the HDR images, the HDR sampler, and the scene/composite
  render passes, which it hands to groups via `set_color_views()` and
  `existing_renderpass`.

Requirement #3 ("Graph Owns All Vulkan Resources") wants the ring-buffered GPU
resources owned by the graph, with passes *referencing* them — and the scene
manager reduced to a pure orchestrator (it describes passes + dependencies +
per-frame data, and owns no `VkImage`/`VkRenderPass`).

### Design: a graph-owned `FrameResourcePool`

A new class owned by `RenderGraph`, holding **ring-buffered** image resources
(one copy per slot), keyed by an opaque handle:

```cpp
class FrameResourcePool {
  // Register a per-slot image resource (declared once, at setup).
  ImageHandle add_image(std::string name, vk::Format, vk::ImageUsageFlags,
                        uint32_t mip_levels = 1);

  // Resolve per-slot views/handles (valid after create()).
  vk::ImageView view(ImageHandle, uint32_t slot, uint32_t mip = 0) const;
  vk::Image     image(ImageHandle, uint32_t slot) const;

  // Lifecycle, driven by RenderGraph::build()/resize().
  void create(vk::Extent2D, uint32_t count);
  void destroy();
};
```

- The scene manager **registers** what it needs at setup (an HDR color target;
  a depth target; later, the transmission snapshot) and keeps only the handles.
- `RenderGraph` (re)creates the pool at the current extent on `build()` /
  `resize()` — one place, replacing the app's `create_hdr_images` +
  `set_resize_fn` dance.
- Passes/groups are configured with pool **handles** instead of raw views or a
  self-owned depth: `group.set_color_attachment(handle)`,
  `group.set_depth_attachment(handle)`. At framebuffer-assembly time the group
  resolves `handle → view(slot)` from the pool.

Sharing falls out naturally: opaque and transmission groups reference the
**same** depth handle, so they depth-test the same buffer.

### Migration order (each phase leaves a working build)

1. **Scaffold.** Introduce `FrameResourcePool`, owned by `RenderGraph`, wired
   into `build()`/`resize()`. Unused at first — no behavior change.
2. **HDR color → pool.** Register the HDR target in the pool; the pbr group
   takes it as its color attachment; composite samples it. Delete
   `ScenePipeline::hdr_images` and the bespoke resize callback. (Lower risk —
   the HDR images are already a per-slot vector; this establishes the pattern.)
3. **Depth → pool.** `ExecutionGroup` stops owning `m_depth_buffer`; depth is a
   pool resource; framebuffers resolve the depth view from the pool. This is
   the structural change that unblocks shared depth (and thus glass).
4. **(Optional, scope decision) Render passes → graph.** Have the graph derive
   render passes from attachment formats/MSAA instead of the app creating and
   sharing them. Completes req #3 but is not required for glass; can be a
   follow-up.

### End state

A clean seam: `Device` (queues) ← `RenderGraph` (**all** ring-buffered GPU
resources + ordering) ← scene manager (description + per-frame data). Adding a
new pass — transmission, SSS blur — becomes "register any resources it needs,
declare the pass + its `depends_on` edges, provide a record callback," with no
new app-owned GPU state.

**Scope (decided):** F3 lands **steps 1–3**. Render-pass ownership (step 4) is
a separate follow-up — render passes stay app-created and shared via
`existing_renderpass`. Steps 1–3 already deliver everything glass needs
(graph-owned, shareable depth + HDR, with the snapshot as a future sibling).

---

## F4 — `Image` mips + compute downsample

`Image` is currently single-mip (`mipLevels = 1`), and IBL rolls its own
mipped-image creation with per-mip views. F4 adds mip-level support (and
per-mip views) to `Image`, plus an HDR-quality compute downsample (running on
F2's queue) to build mip chains. This unblocks roughness-based blur (for
transmission) and lets IBL's bespoke mip/transition code — and its
`begin/end_single_time_commands` helpers — migrate onto the shared
infrastructure (`commands.h::submit_one_shot`).

**Follow-up — block-compressed material textures.** Mips fixed the *locality* of
texture reads; they did not shrink the reads themselves. `Texture::upload_pixels`
uploads RGBA8 at 4 bytes/texel, so DamagedHelmet's five 2048² images occupy
~80 MiB in VRAM (~107 MiB with the chain) even though they ship as ~3.2 MB of
JPEG — JPEG is a storage codec and is fully decoded before upload. BC7 (colour)
and BC5 (normals) are GPU-native fixed-rate formats at 1 byte/texel, decompressed
per-texel in the texture unit, so the same set becomes ~20 MiB (~27 MiB mipped) —
a flat 4:1 on both footprint and bytes-per-cache-line-fill, at every LOD. The
glTF-native route is KTX2 + `KHR_texture_basisu`; transcoding once at configure
time next to the fetched `.glb` may be simpler than adding a loader dependency.
Caveats: 4:1 is a bandwidth/footprint ratio, not a promised frame-time win (the
mipped path is already largely cache-resident); and compressing
already-decoded JPEG compounds generation loss, so the BC7/BC5 split by texture
role matters.

---

## F5 — Split run-ahead depth from residency depth

`RenderGraph::offscreen_depth()` (`render_graph.cpp:91`) returns one number that
is used for two unrelated purposes, and they want opposite values:

- **Run-ahead depth** — how many frames the CPU may record before stalling.
  `render_graph.cpp:241` computes `slot = m_cpu_frame % os_depth`, and
  `submission_group.cpp:118` host-waits on that slot's timeline value. Backed by
  per-slot command buffers, reflected UBOs/SSBOs, and descriptor sets: a few MB
  at any depth. This is what produces the frame rate, and it wants to be deep.
- **Residency depth** — how many copies of the large per-slot images exist. At
  4k: HDR ~66 MB/slot, depth ~33 MB (1x) to ~265 MB (8x), MSAA colour scratch
  ~531 MB (8x). Roughly 100 MB/slot at 1x versus ~860 MB/slot at 8x. This is what
  OOMs, and it wants to be shallow *only* at high sample counts.

Conflating them forced a flat `kDefaultMaxInFlight = 4` cap, which fixed the
8x/4k OOM by charging every other configuration for it — including 1x, where a
slot costs ~100 MB and there is nothing to protect against. The resulting
frame-rate regression was measured and is scene- and MSAA-independent.

**Design.** Ring the cheap CPU-facing resources at `N` (swapchain count) and the
large images at `K` (2–3). Frames `i` and `i+K` then share an image, which needs
an explicit edge: a **GPU-side** timeline wait in frame `i`'s submit against the
value signalled by frame `i-K`. It must not be a host wait — that would
reintroduce the stall this exists to remove. The submit path already assembles
wait lists for the DAG, so it is one more entry. Sync validation (Debug) is the
backstop if the edge is wrong.

Descriptors mostly fall out for free: `scene.cpp:161` and `scene.cpp:177` already
rewrite image descriptors every frame from `last_offscreen_slot()`, so they would
resolve against the K-ring instead. Each of the N descriptor sets is distinct, so
there is no write-while-in-use hazard.

**Constraint.** Any depth that varies must be *sticky*. `offscreen_depth()` is
read both at build (`render_graph.cpp:106`) and in `rebuild_for_msaa`
(`scene_pipeline.cpp:447`), and the incremental MSAA path deliberately keeps the
HDR/composite/semaphores from the original build. A sample-count-dependent depth
would produce a pbr group ringed at one value against an HDR ringed at another —
out-of-range slot indexing. Compute once for the worst reachable sample count,
then cache.

**Open question, and the cheap way to settle it.** It is not yet established
whether the frame rate comes from CPU run-ahead or from GPU co-residency. With
one graphics queue, genuine co-execution is the tail of frame `i` overlapping the
head of `i+1` — realistically 2–3 frames, not 8 — which would mean `K` can be
clamped freely. Splitting the two knobs makes this measurable: hold `N` at the
swapchain count, sweep `K` from 2 upward at 1x, and see whether the rate moves.
If it is flat, clamp `K` at high MSAA and keep `N` deep. If it drops, the
opposite, and now for a known reason. Note that `K` does bound how many scene
passes can execute simultaneously — that cost is real, it is just paid only in
the configurations that are already bandwidth-bound.

---

## Minor follow-ups

- **MSAA depth could be transient.** The multisample depth buffer is only used
  within the scene pass — never resolved or sampled — so it could carry
  `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT` (the MSAA color attachment already
  does, via the `Image` constructor). With lazily-allocated memory this avoids
  backing store; a minor bandwidth/memory win, mostly on tile-based GPUs.
  `DepthStencilAttachment` currently always uses `eDepthStencilAttachment` only.

- **`reset_camera` does not reset `m_view_angle`.** It restores position, focal
  point, and clipping range, and it *derives* the framing distance from the
  current FOV (`camera.cpp:259`: `distance = radius / sin(view_angle/2)`), but
  never resets the FOV itself. Since plain scroll is `zoom()` (FOV) and
  `reset_camera` runs on every model load (`main.cpp:173`, `scene.cpp:207`), a
  zoom-then-switch-model cycle silently spends zoom range: the model is re-framed
  to look identically sized while the remaining travel to the 1° clamp shrinks
  (60° frames at 2×radius, 10° at 11.5×, 1° at 114.6×). Switch back and forth a
  few times and you can no longer zoom close. The orthographic branch already
  resets its own zoom state (`camera.cpp:254` writes `m_parallel_scale = radius`)
  — the perspective branch only reads. Fixing it makes the two branches
  consistent; set the FOV *before* line 259 reads it, and share the `60.0f`
  default with the member initialiser at `camera.h:167`. Nothing else owns the
  value: `set_view_angle` has no callers and there is no CLI/TOML/GUI control.

- **Asset loading is synchronous and blocks the main thread.** Each texture
  uploads via its own `submit_one_shot` → `waitIdle` round-trip (now plus a
  blit-chain for mips), so a texture-heavy model (e.g. ABeautifulGame) freezes
  the window during load ("application not responding"). Fixes: batch all
  uploads into one command buffer / submission, and/or load on a worker thread.
  A natural place to reuse the F4 mip-generation and the compute queue (F2).

- **Synchronization validation (enabled in Debug).** The standard validation
  layer catches API misuse (wrong layouts, missing usage flags, object
  lifetimes) but **not synchronization hazards** — a missing or too-narrow
  barrier produces *correct-looking API calls* that race on the GPU. The engine
  declares ordering (`depends_on`) and barriers (render-pass layouts; explicit
  `vkCmdPipelineBarrier` for transfers/compute) **by hand** — there is no
  auto-derived frame graph — so a missed barrier is a developer error, not a
  compile or standard-validation error. Vulkan's **synchronization validation**
  (`VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`) detects
  RAW/WAR/WAW hazards across draws, copies, and dispatches. It is now enabled via
  `VkValidationFeaturesEXT` on the instance `pNext` whenever the validation layer
  is active (i.e. `VKWAVE_DEBUG` / Debug config). It would have caught the
  pbr→composite wait-stage bug (which produced torn frames with *zero*
  standard-validation errors), and is the backstop for the hand-written barriers
  transmission's cross-queue snapshot pass will add.

  > Note: `VKWAVE_DEBUG` had regressed to *always off* (a stray unconditional
  > `set(VKWAVE_DEBUG OFF)` in CMake clobbered the auto-enable), so **no**
  > validation ran in any build. It is now a per-config compile definition
  > (`$<$<CONFIG:Debug>:VKWAVE_DEBUG>`) — required because the multi-config
  > generator shares one generated `config.h` across configs, so a
  > `#cmakedefine` cannot vary Debug vs Release.
