#pragma once

#include <vkwave/core/mesh.h>
#include <vkwave/core/pass.h>
#include <vkwave/pipeline/pipeline.h>

#include <glm/glm.hpp>

namespace vkwave
{

class ExecutionGroup;

/// 3D cube pass: draws a mesh with camera UBO and model push constants.
///
/// Holds only raw pointers (not owned -- graph owns the RAII wrappers).
/// Queries the ExecutionGroup for pipeline, layout, extent, UBO, and
/// descriptor sets — no manual handle copying needed.
struct CubePass : Pass<CubePass>
{
  ExecutionGroup* group{ nullptr };
  const Mesh* mesh{ nullptr };

  /// Returns the PipelineSpec for this pass (shader paths, vertex layout, etc.).
  /// Knows about the Vertex type — eventually a loader would provide this.
  static PipelineSpec pipeline_spec();

  /// Record a full frame: update UBO, set push constants, draw mesh.
  /// Called inside the record callback with an active render pass.
  void record_frame(vk::CommandBuffer cmd,
    const glm::mat4& viewProj, float time, int debug_mode) const;
};

static_assert(std::is_trivially_destructible_v<CubePass>,
  "CubePass must be trivially destructible");

} // namespace vkwave
