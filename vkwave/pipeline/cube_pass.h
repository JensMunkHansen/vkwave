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

  // Per-frame state (set by app before render_frame)
  glm::mat4 view_projection{ 1.0f };
  float time{ 0.0f };
  int debug_mode{ 0 };

  /// Returns the PipelineSpec for this pass (shader paths, vertex layout, etc.).
  static PipelineSpec pipeline_spec();

  /// Record a full frame: update UBO, set push constants, draw mesh.
  void record(vk::CommandBuffer cmd) const;
};

static_assert(std::is_trivially_destructible_v<CubePass>,
  "CubePass must be trivially destructible");

} // namespace vkwave
