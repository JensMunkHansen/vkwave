# Transmission / Volume (planned)

Design notes for `KHR_materials_transmission` + `KHR_materials_volume` (glass).
This is the eventual **consumer** of the rendering foundations
([rendering-foundations.md](rendering-foundations.md)) — it should be built
*after* F1–F4 exist, so it composes cleanly rather than special-casing
machinery. The loader already parses `transmissionFactor`, thickness and
attenuation; `ior` and the GPU-side fields remain to be added.

## It is a new pass, not inline blending

The existing `BlendPass` records *additional draws into the same render pass*
as the opaque pass. That works because hardware alpha blending never reads the
framebuffer — the shader outputs `(color, alpha)` and the fixed-function
blender computes `src·a + dst·(1−a)`.

Transmission is **refraction**: the shader must *sample the opaque scene behind
the surface* — distorted by the surface normal + IOR + thickness, blurred by
roughness (via mip levels), and tinted by Beer-Lambert absorption over the
volume thickness. You cannot read the render target you are writing, and you
need a blurred/mipped copy. So the opaque result must first be snapshotted to a
sampleable texture.

Pass chain:

```
Opaque  →  [snapshot opaque HDR → sampleable image + mip chain]  →  Transmission (samples snapshot)  →  Blend  →  Composite
```

## Ring-buffer the snapshot — the overlap linchpin

The snapshot image **must be per-slot**, lifecycle-identical to the existing
HDR images (a vector sized to the swapchain image count, recreated on resize,
indexed by the per-frame slot), with `mipLevels > 1` and
`eSampled | eTransferDst` usage. A single shared snapshot would serialize
`Snapshot[F+1]` against `Transmission[F]` and destroy cross-frame overlap.

The intra-frame chain `Opaque → Snapshot → Transmission` is a real serial
dependency — it adds frame *latency*, not throughput loss, because Frame F+1's
opaque pass overlaps Frame F's tail on its own resource set. The snapshot
barrier must be fine-grained (`TRANSFER → FRAGMENT_SHADER` on that one image),
never a global `ALL_COMMANDS` barrier.

## Opt-in — pay only when there is glass

A scene-level `has_transmission` flag (scan materials at load, like the
existing `has_transparent`). **Gate at graph-build time, not record time:**
the transmission pass owns per-slot snapshot images (real VRAM), so a
record-time early-return would still pay the allocation. When there are no
transmissive materials, don't allocate the snapshot resources or create the
pass at all — the dependency graph is then identical to the opaque-only
`Opaque → Blend → Composite`. Decide per scene (re-evaluate on model switch),
never per frame.

## Phasing

1. **Sharp transmission** — IOR + thickness refraction offset, Beer-Lambert
   volume tint, single-sample snapshot. No mips, no async.
2. **Roughness blur** — sample snapshot LOD by roughness (needs F4's `Image`
   mips + compute downsample).
3. **Async compute** — move the snapshot mip generation onto the dedicated
   compute queue (F2), overlapping it with the next frame's graphics work.

## Implementation status

**Phase 1 (sharp transmission) — done.** The transmission pass is its own
`ExecutionGroup` + submission, ordered `pbr → transmission → composite` via the
DAG. It draws only transmissive primitives (the opaque/blend passes skip them via
`PBRContext::defer_transmissive`) into the HDR, depth-testing against the stored
opaque depth. The opaque HDR is snapshotted per-slot before the glass draws; the
shader samples it at an IOR/thickness-bent screen coordinate with Beer-Lambert
absorption + a Fresnel rim. Opt-in is gated at build time and re-evaluated on
model switch (structural rebuild); the snapshot resource persists for any glass
scene so an MSAA toggle only adds/removes the *group*.

Only `KHR_materials_transmission` (specular) drives `transmissionFactor`.
`KHR_materials_diffuse_transmission` is translucency/SSS — captured separately,
**not** routed into this pass (conflating them dropped LowerJawScan's colors).

**Per-pixel transmission — done.** `transmissionFactor` is multiplied by the
`transmissionTexture` R channel (per-material, set 2), so only the textured
regions refract (e.g. TransmissionTest's pattern); the rest stays opaque. White
fallback for materials without a mask. Mask is sampled with UV0 (texture-transform
/ UV1 on the mask is a refinement).

**IBL Fresnel reflection — done.** The glass reflects the prefiltered environment
cubemap along `reflect(-V, N)` at the Fresnel rim (sharp, mip 0) instead of flat
white, so it mirrors its surroundings at grazing angles.

### Follow-ups (next iterations)

1. **Roughness blur (phase 2)** — sample the snapshot (and the env reflection) at
   a mip selected by roughness, for frosted glass. Needs a mip chain on the
   snapshot (the pool can already allocate `full_mips`) + a downsample (reuse F4
   mip-gen / F2 compute). The bigger remaining item.
2. **Mask UV-set / texture-transform** — the transmission mask is sampled with
   UV0 + no transform; honor TEXCOORD_1 / KHR_texture_transform like the PBR
   textures for masks that need it.
3. **MSAA + glass** — the transmission pass is single-sample only, because it
   can't share the multisample opaque depth. A depth-resolve (MSAA depth →
   single-sample) would let glass coexist with MSAA.
4. **Shared MSAA scratch** — the intra-pass MSAA color/depth scratch is
   ring-buffered per slot, but it's written-and-resolved within one serialized
   scene pass and never read across frames, so a single shared copy would cut
   MSAA memory by the ring depth (today capped at 4 in flight). Confirmed to need
   no new sync — the existing per-frame fence + render-pass external dependency
   already order the cross-frame scratch reuse.

## Cheaper alternative (rejected for correctness)

Transmissive materials could be treated as plain alpha-blend (constant tint,
no refraction or background blur), which fits inline in `BlendPass` with no new
pass. It looks acceptable for thin clear glass but wrong for thick, curved, or
strongly-refractive glass (e.g. a concept-car canopy), so it is not the target.
