#pragma once

#include <vkwave/core/pass.h>
#include <vkwave/pipeline/pbr_pass.h>   // PBRContext (shared scene/camera/materials)
#include <vkwave/pipeline/pipeline.h>

namespace vkwave
{

class ExecutionGroup;

/// Transmission (refraction) pass — runs in its **own** ExecutionGroup with its
/// own pipeline/descriptors and a separate submission (Requirement #5), ordered
/// after the opaque pass via the graph DAG. Draws only transmissive primitives
/// (materials with transmissionFactor > 0) into the HDR target (LOAD),
/// depth-testing against the shared opaque depth.
///
/// Reads scene geometry/camera/materials from the shared PBRContext but binds the
/// transmission group's own pipeline + UBO (set 0) + material SSBO (set 1) —
/// descriptor sets separate from the pbr group. Phase 1 uses a dummy flat-tint
/// shader to validate the plumbing; the snapshot sampling + refraction follow.
///
/// Holds only raw pointers -- trivially destructible.
struct TransmissionPass : Pass<TransmissionPass>
{
  const PBRContext* ctx{ nullptr };  // scene geometry, camera, materials
  ExecutionGroup* group{ nullptr };  // the transmission group (own pipeline/descriptors)

  /// Returns the PipelineSpec for the transmission pass (reuses pbr.vert).
  static PipelineSpec pipeline_spec();

  /// Record: update UBO, bind state, draw transmissive primitives.
  void record(vk::CommandBuffer cmd) const;
};

static_assert(std::is_trivially_destructible_v<TransmissionPass>,
  "TransmissionPass must be trivially destructible");

} // namespace vkwave
