#pragma once

#include <vulkan/vulkan.hpp>

#include <array>

namespace vkwave
{

/// A single texture binding: image view + sampler pair.
struct TextureBinding
{
  vk::ImageView view;
  vk::Sampler sampler;
};

/// The 11 texture bindings (shader bindings 1-11) for one material.
/// UBO (binding 0) is excluded — the graph supplies that separately
/// since it's the resource that differs per frame.
///
/// Index mapping:
///   [0] = baseColor       (binding 1)
///   [1] = normal           (binding 2)
///   [2] = metallicRoughness (binding 3)
///   [3] = emissive         (binding 4)
///   [4] = ao               (binding 5)
///   [5] = brdfLUT          (binding 6)
///   [6] = irradiance       (binding 7)
///   [7] = prefiltered      (binding 8)
///   [8] = iridescence      (binding 9)
///   [9] = iridescenceThickness (binding 10)
///   [10] = thickness       (binding 11)
struct MaterialTextureSet
{
  static constexpr uint32_t TEXTURE_COUNT = 11;
  std::array<TextureBinding, TEXTURE_COUNT> textures;
};

} // namespace vkwave
