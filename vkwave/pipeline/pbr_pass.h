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

/// PBR pass: Cook-Torrance BRDF with normal mapping and IBL.
///
/// Draws a mesh with full PBR material. Textures and IBL maps are
/// bound as image descriptors (written once after build). The UBO
/// (camera + light) is ring-buffered per frame.
///
/// Holds only raw pointers and POD -- trivially destructible.
struct PBRPass : Pass<PBRPass>
{
  ExecutionGroup* group{ nullptr };
  const Mesh* mesh{ nullptr };

  // Per-frame state (set by app before render_frame)
  glm::mat4 view_projection{ 1.0f };
  glm::vec3 cam_position{};
  glm::mat4 model{ 1.0f };
  glm::vec4 base_color_factor{ 1.0f };
  float metallic_factor{ 1.0f };
  float roughness_factor{ 1.0f };
  float time{ 0.0f };
  int debug_mode{ 0 };

  // Feature toggles
  bool enable_normal_mapping{ true };
  bool enable_emissive{ true };

  // Light (directional)
  glm::vec3 light_direction{ 1.0f, 1.0f, 1.0f };
  float light_intensity{ 3.0f };
  glm::vec3 light_color{ 1.0f };

  // Per-primitive scene data (null = single-draw legacy path)
  const ScenePrimitive* primitives{ nullptr };
  const SceneMaterial* materials{ nullptr };
  uint32_t primitive_count{ 0 };
  uint32_t material_count{ 0 };

  /// Returns the PipelineSpec for this pass (shader paths, vertex layout, etc.).
  static PipelineSpec pipeline_spec();

  /// Record a full frame: update UBO, set push constants, draw mesh.
  void record(vk::CommandBuffer cmd) const;
};

static_assert(std::is_trivially_destructible_v<PBRPass>,
  "PBRPass must be trivially destructible");

} // namespace vkwave
