#pragma once

#include <vkwave/core/mesh.h>
#include <vkwave/core/pass.h>
#include <vkwave/pipeline/pipeline.h>

#include <glm/glm.hpp>

namespace vkwave
{

class ExecutionGroup;
struct ScenePrimitive;
struct SceneMaterial;

/// Shared state for all PBR-related passes.
///
/// Owned by Scene, read by PBRPass and BlendPass.
/// All raw pointers and POD -- trivially destructible.
struct PBRContext
{
  ExecutionGroup* group{ nullptr };
  const Mesh* mesh{ nullptr };

  // Scene geometry
  const ScenePrimitive* primitives{ nullptr };
  const SceneMaterial* materials{ nullptr };
  uint32_t primitive_count{ 0 };
  uint32_t material_count{ 0 };
  bool has_transparent{ false };

  // Camera (updated per-frame by Scene::update)
  glm::mat4 view_projection{ 1.0f };
  glm::vec3 cam_position{};

  // Per-frame
  float time{ 0.0f };
  int debug_mode{ 0 };

  // Feature toggles
  bool enable_normal_mapping{ true };
  bool enable_emissive{ true };

  // Light (directional)
  glm::vec3 light_direction{ 1.0f, 1.0f, 1.0f };
  float light_intensity{ 3.0f };
  glm::vec3 light_color{ 1.0f };
};

static_assert(std::is_trivially_destructible_v<PBRContext>,
  "PBRContext must be trivially destructible");

/// PBR opaque pass: Cook-Torrance BRDF with normal mapping and IBL.
///
/// Updates the UBO, binds pipeline/viewport/scissor/descriptors,
/// and draws opaque primitives. BlendPass runs after this.
///
/// Holds only raw pointers and POD -- trivially destructible.
struct PBRPass : Pass<PBRPass>
{
  const PBRContext* ctx{ nullptr };

  // Legacy single-draw fallback state (no primitives path)
  glm::mat4 model{ 1.0f };
  glm::vec4 base_color_factor{ 1.0f };
  float metallic_factor{ 1.0f };
  float roughness_factor{ 1.0f };

  /// Returns the PipelineSpec for this pass (shader paths, vertex layout, etc.).
  static PipelineSpec pipeline_spec();

  /// Record: update UBO, bind pipeline state, draw opaque primitives.
  void record(vk::CommandBuffer cmd) const;
};

static_assert(std::is_trivially_destructible_v<PBRPass>,
  "PBRPass must be trivially destructible");

/// Transparent blend pass: draws alpha-blended primitives back-to-front.
///
/// Shares pipeline, descriptors, mesh, and materials with PBRPass via PBRContext.
/// Assumes PBRPass has already bound the pipeline, viewport, scissor,
/// sets 0 and 2, and the mesh. Only binds set 1 per material change.
///
/// Skipped entirely when ctx->has_transparent is false.
struct BlendPass : Pass<BlendPass>
{
  const PBRContext* ctx{ nullptr };

  void record(vk::CommandBuffer cmd) const;
};

static_assert(std::is_trivially_destructible_v<BlendPass>,
  "BlendPass must be trivially destructible");

} // namespace vkwave
