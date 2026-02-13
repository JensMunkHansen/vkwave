#pragma once

#include <vkwave/core/buffer.h>

namespace vkwave
{

/// @brief Specialized buffer for uniform data.
///
/// Uniform buffers are used to pass data (matrices, parameters, etc.)
/// from CPU to shaders. They use HOST_VISIBLE | HOST_COHERENT memory
/// for efficient CPU updates.
///
/// @tparam T The uniform data structure type (e.g., UniformBufferObject).
template <typename T>
class UniformBuffer : public Buffer
{
public:
  /// @brief Create a uniform buffer for type T.
  /// @param device The Vulkan device wrapper.
  /// @param name Debug name for the buffer.
  UniformBuffer(const Device& device, const std::string& name)
    : Buffer(device, name, sizeof(T), vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
  {
  }

  /// @brief Update the entire uniform buffer with new data.
  /// @param data The uniform data to upload.
  void update(const T& data) { Buffer::update(&data, sizeof(T), 0); }

  /// @brief Get descriptor buffer info for binding.
  /// @return VkDescriptorBufferInfo suitable for descriptor set updates.
  [[nodiscard]] vk::DescriptorBufferInfo descriptor_info() const
  {
    return vk::DescriptorBufferInfo{ m_buffer, 0, sizeof(T) };
  }
};

} // namespace vkwave
