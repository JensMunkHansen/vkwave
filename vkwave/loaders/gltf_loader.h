#pragma once

#include <vkwave/core/mesh.h>
#include <vkwave/core/texture.h>

#include <glm/glm.hpp>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace vkwave
{

class Device;

/// @brief Axis-aligned bounding box.
struct AABB
{
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::lowest()};

  void expand(const glm::vec3& point)
  {
    min = glm::min(min, point);
    max = glm::max(max, point);
  }

  [[nodiscard]] bool valid() const { return min.x <= max.x; }

  void to_bounds(float bounds[6]) const
  {
    bounds[0] = min.x; bounds[1] = max.x;
    bounds[2] = min.y; bounds[3] = max.y;
    bounds[4] = min.z; bounds[5] = max.z;
  }
};

/// @brief glTF alpha rendering mode.
/// @see https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#alpha-coverage
enum class AlphaMode : uint32_t
{
  Opaque = 0,
  Mask = 1,
  Blend = 2
};

/// @brief Complete glTF model with mesh and textures.
/// @see glTF 2.0 PBR: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials
struct GltfModel
{
  std::unique_ptr<Mesh> mesh;
  std::unique_ptr<Texture> baseColorTexture;         // nullptr if no texture
  std::unique_ptr<Texture> normalTexture;            // nullptr if no normal map
  std::unique_ptr<Texture> metallicRoughnessTexture; // nullptr if no PBR texture (G=roughness, B=metallic)
  std::unique_ptr<Texture> emissiveTexture;          // nullptr if no emissive (RGB glow)
  std::unique_ptr<Texture> aoTexture;                // nullptr if no ambient occlusion (R channel)
};

/// @brief Load a glTF 2.0 mesh file.
///
/// Supports .gltf (JSON) and .glb (binary) files.
/// Extracts vertex positions, normals, UVs, colors, and indices.
/// Uses cgltf for parsing.
///
/// @param device The Vulkan device wrapper.
/// @param filepath Path to the glTF file.
/// @return Loaded mesh, or nullptr on failure.
std::unique_ptr<Mesh> load_gltf(const Device& device, const std::string& filepath);

/// @brief Load a glTF 2.0 model with textures.
///
/// Supports .gltf (JSON) and .glb (binary) files.
/// Extracts mesh geometry and base color texture from materials.
///
/// @param device The Vulkan device wrapper.
/// @param filepath Path to the glTF file.
/// @return GltfModel with mesh and optional textures.
GltfModel load_gltf_model(const Device& device, const std::string& filepath);

/// @brief A single draw call within a scene.
struct ScenePrimitive
{
  uint32_t firstIndex;
  uint32_t indexCount;
  int32_t vertexOffset;
  uint32_t materialIndex;
  glm::mat4 modelMatrix;  // pre-computed world transform from node hierarchy
  glm::vec3 centroid{0.0f};  // object-space centroid for depth sorting
};

/// @brief Material data for a scene primitive.
struct SceneMaterial
{
  std::unique_ptr<Texture> baseColorTexture;         // nullptr -> use default
  std::unique_ptr<Texture> normalTexture;
  std::unique_ptr<Texture> metallicRoughnessTexture;
  std::unique_ptr<Texture> emissiveTexture;
  std::unique_ptr<Texture> aoTexture;
  std::unique_ptr<Texture> iridescenceTexture;          // factor mask (R channel)
  std::unique_ptr<Texture> iridescenceThicknessTexture;  // thickness map (G channel)
  glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor{1.0f};
  float roughnessFactor{1.0f};
  AlphaMode alphaMode{AlphaMode::Opaque};
  float alphaCutoff{0.5f};
  bool doubleSided{false};
  float iridescenceFactor{0.0f};
  float iridescenceIor{1.3f};
  float iridescenceThicknessMin{100.0f};
  float iridescenceThicknessMax{400.0f};

  // KHR_materials_volume
  std::unique_ptr<Texture> thicknessTexture;
  float thicknessFactor{0.0f};
  glm::vec3 attenuationColor{1.0f};
  float attenuationDistance{0.0f};  // 0 = infinite (no attenuation)

  // KHR_materials_transmission (proxy for diffuse_transmission)
  float transmissionFactor{0.0f};

  // When true, no explicit transmission data exists — shader derives
  // per-pixel transmission from thickness texture via exp(-thickness * k).
  // When false, transmissionFactor (and future transmission texture) are authoritative.
  bool deriveTransmissionFromThickness{false};
};

/// @brief Complete scene with merged geometry and per-primitive materials.
struct GltfScene
{
  std::unique_ptr<Mesh> mesh;              // merged vertex/index buffer
  std::vector<SceneMaterial> materials;    // one per glTF material
  std::vector<ScenePrimitive> primitives;  // one per draw call
  AABB bounds;                             // world-space bounding box
};

/// @brief Load a glTF 2.0 scene with per-primitive materials and transforms.
///
/// Traverses node hierarchy, merges all geometry into a single mesh,
/// and records per-primitive draw info (material index, model matrix).
///
/// @param device The Vulkan device wrapper.
/// @param filepath Path to the glTF file.
/// @return GltfScene with mesh, materials, and primitives.
GltfScene load_gltf_scene(const Device& device, const std::string& filepath);

} // namespace vkwave
