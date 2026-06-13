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
| F2 | Async compute queue | #6 Async compute | In progress |
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

**Plan.**
- Select a compute queue family in `Device` — prefer a *dedicated* async
  family (COMPUTE without GRAPHICS); fall back to the graphics family when
  none exists.
- Add `compute_queue()` + the family index.
- `SubmissionGroup::submit()` already takes a `vk::Queue`, so routing a compute
  group to the compute queue is largely graph-side; F1's DAG wires the
  graphics↔compute timeline synchronization.

**Out of scope.** IBL generation stays on the graphics queue. It is a one-time
precompute at load with nothing to overlap, so async buys nothing there (and a
dedicated queue would add a queue-family ownership transfer for images the
graphics pass samples). The genuine async win for IBL is *hitch-free runtime
HDR switching* (regenerate on compute while continuing to render the old
environment, then swap) — a separate, optional feature, not part of F2.

---

## F3 — Graph-owned, ring-buffered shared resources

Each `ExecutionGroup` currently owns its own depth buffer, and the HDR images
are owned ad-hoc by the application. For multiple passes to share a depth
buffer (e.g. opaque + transmission depth-testing the same buffer) and for the
graph to ring-buffer everything per slot, the depth buffer (and a unified
HDR + snapshot pool) should become graph-owned, per-slot resources that passes
reference by `frame_index`. This is the "Graph Owns All Vulkan Resources"
requirement made real.

---

## F4 — `Image` mips + compute downsample

`Image` is currently single-mip (`mipLevels = 1`), and IBL rolls its own
mipped-image creation with per-mip views. F4 adds mip-level support (and
per-mip views) to `Image`, plus an HDR-quality compute downsample (running on
F2's queue) to build mip chains. This unblocks roughness-based blur (for
transmission) and lets IBL's bespoke mip/transition code — and its
`begin/end_single_time_commands` helpers — migrate onto the shared
infrastructure (`commands.h::submit_one_shot`).
