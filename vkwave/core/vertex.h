#pragma once

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

#include <array>

namespace vkwave
{

/// @brief Vertex structure for mesh rendering.
///
/// Matches the vertex shader input layout:
///   layout(location = 0) in vec3 inPosition;
///   layout(location = 1) in vec3 inNormal;
///   layout(location = 2) in vec3 inColor;
///   layout(location = 3) in vec2 inTexCoord;
///   layout(location = 4) in vec4 inTangent;
struct Vertex
{
  glm::vec3 position{ 0.0f };
  glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
  glm::vec3 color{ 1.0f };
  glm::vec2 texCoord{ 0.0f };
  glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };  // xyz=tangent, w=handedness

  /// @brief Get the vertex binding description.
  /// Describes how to read vertex data from the buffer.
  static vk::VertexInputBindingDescription binding_description()
  {
    vk::VertexInputBindingDescription description{};
    description.binding = 0;
    description.stride = sizeof(Vertex);
    description.inputRate = vk::VertexInputRate::eVertex;
    return description;
  }

  /// @brief Get the vertex attribute descriptions.
  /// Describes the layout of each vertex attribute.
  /// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes
  static std::array<vk::VertexInputAttributeDescription, 5> attribute_descriptions()
  {
    std::array<vk::VertexInputAttributeDescription, 5> descriptions{};

    // Position at location 0
    descriptions[0].binding = 0;
    descriptions[0].location = 0;
    descriptions[0].format = vk::Format::eR32G32B32Sfloat;
    descriptions[0].offset = offsetof(Vertex, position);

    // Normal at location 1
    descriptions[1].binding = 0;
    descriptions[1].location = 1;
    descriptions[1].format = vk::Format::eR32G32B32Sfloat;
    descriptions[1].offset = offsetof(Vertex, normal);

    // Color at location 2
    descriptions[2].binding = 0;
    descriptions[2].location = 2;
    descriptions[2].format = vk::Format::eR32G32B32Sfloat;
    descriptions[2].offset = offsetof(Vertex, color);

    // TexCoord at location 3
    descriptions[3].binding = 0;
    descriptions[3].location = 3;
    descriptions[3].format = vk::Format::eR32G32Sfloat;
    descriptions[3].offset = offsetof(Vertex, texCoord);

    // Tangent at location 4 (vec4: xyz=tangent, w=handedness)
    // glTF 2.0 spec: TANGENT is VEC4, w component is handedness (+1 or -1)
    descriptions[4].binding = 0;
    descriptions[4].location = 4;
    descriptions[4].format = vk::Format::eR32G32B32A32Sfloat;
    descriptions[4].offset = offsetof(Vertex, tangent);

    return descriptions;
  }
};

} // namespace vkwave
