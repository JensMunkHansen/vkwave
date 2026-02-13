#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace vkwave
{

class Device;
class Mesh;

/// Wrapper for a Vulkan acceleration structure (BLAS or TLAS)
class AccelerationStructure
{
public:
  AccelerationStructure(const Device& device, const std::string& name);
  ~AccelerationStructure();

  // Non-copyable
  AccelerationStructure(const AccelerationStructure&) = delete;
  AccelerationStructure& operator=(const AccelerationStructure&) = delete;

  // Movable
  AccelerationStructure(AccelerationStructure&& other) noexcept;
  AccelerationStructure& operator=(AccelerationStructure&& other) noexcept;

  [[nodiscard]] vk::AccelerationStructureKHR handle() const { return m_handle; }
  [[nodiscard]] vk::DeviceAddress device_address() const { return m_device_address; }
  [[nodiscard]] vk::Buffer buffer() const { return m_buffer; }

  /// Build a Bottom Level Acceleration Structure from mesh geometry
  void build_blas(vk::CommandBuffer cmd, const Mesh& mesh);

  /// Build a Top Level Acceleration Structure from BLAS instances
  void build_tlas(vk::CommandBuffer cmd,
    const std::vector<std::pair<const AccelerationStructure*, glm::mat4>>& instances);

private:
  void create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage);
  void cleanup();

  const Device* m_device{ nullptr };
  std::string m_name;

  vk::AccelerationStructureKHR m_handle{ VK_NULL_HANDLE };
  vk::Buffer m_buffer{ VK_NULL_HANDLE };
  vk::DeviceMemory m_memory{ VK_NULL_HANDLE };
  vk::DeviceAddress m_device_address{ 0 };

  // Scratch buffer for building
  vk::Buffer m_scratch_buffer{ VK_NULL_HANDLE };
  vk::DeviceMemory m_scratch_memory{ VK_NULL_HANDLE };

  // Instance buffer for TLAS (must persist until command buffer completes)
  vk::Buffer m_instance_buffer{ VK_NULL_HANDLE };
  vk::DeviceMemory m_instance_memory{ VK_NULL_HANDLE };
};

/// Helper to get buffer device address
vk::DeviceAddress get_buffer_device_address(vk::Device device, vk::Buffer buffer);

} // namespace vkwave
