#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>
#include <string>

namespace vkwave
{

class Device;

/// @brief Base class for GPU memory buffers.
///
/// Provides RAII management of VkBuffer and VkDeviceMemory.
/// Supports persistent mapping for CPU-writable buffers.
class Buffer
{
public:
  /// @brief Create a GPU buffer.
  /// @param device The Vulkan device wrapper.
  /// @param name Debug name for the buffer.
  /// @param size Size in bytes.
  /// @param usage Vulkan buffer usage flags.
  /// @param properties Memory property flags (e.g., HOST_VISIBLE | HOST_COHERENT).
  Buffer(const Device& device, const std::string& name, vk::DeviceSize size,
    vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

  virtual ~Buffer();

  // Non-copyable
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  // Movable
  Buffer(Buffer&& other) noexcept;
  Buffer& operator=(Buffer&& other) noexcept;

  /// @brief Get the underlying Vulkan buffer handle.
  [[nodiscard]] vk::Buffer buffer() const { return m_buffer; }

  /// @brief Get the buffer size in bytes.
  [[nodiscard]] vk::DeviceSize size() const { return m_size; }

  /// @brief Get the debug name.
  [[nodiscard]] const std::string& name() const { return m_name; }

  /// @brief Check if buffer memory is mapped.
  [[nodiscard]] bool is_mapped() const { return m_mapped_data != nullptr; }

  /// @brief Get pointer to mapped memory.
  /// @return Pointer to mapped memory, or nullptr if not mapped.
  [[nodiscard]] void* mapped_data() const { return m_mapped_data; }

  /// @brief Map buffer memory for CPU access.
  /// @note Only valid for HOST_VISIBLE memory.
  void map();

  /// @brief Unmap buffer memory.
  void unmap();

  /// @brief Copy data to the buffer.
  /// @param data Source data pointer.
  /// @param size Size in bytes to copy.
  /// @param offset Offset in buffer to write to.
  /// @note Buffer must be mapped or will be temporarily mapped.
  void update(const void* data, vk::DeviceSize size, vk::DeviceSize offset = 0);

  /// @brief Create a DEVICE_LOCAL buffer via staging upload.
  /// Allocates a temporary HOST_VISIBLE staging buffer, copies data into it,
  /// then issues a one-shot vkCmdCopyBuffer to a DEVICE_LOCAL buffer.
  /// @param device The Vulkan device wrapper.
  /// @param name Debug name for the buffer.
  /// @param data Source data pointer.
  /// @param size Size in bytes.
  /// @param usage Buffer usage flags (TRANSFER_DST is added automatically).
  static std::unique_ptr<Buffer> create_device_local(
    const Device& device, const std::string& name,
    const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage);

protected:
  const Device* m_device{ nullptr };
  std::string m_name;

  vk::Buffer m_buffer{ VK_NULL_HANDLE };
  vk::DeviceMemory m_memory{ VK_NULL_HANDLE };
  vk::DeviceSize m_size{ 0 };

  void* m_mapped_data{ nullptr };
  bool m_persistent_mapping{ false };
};

} // namespace vkwave
