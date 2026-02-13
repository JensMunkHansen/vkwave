#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <vector>

namespace vkwave
{

class Device;

/// Ray tracing pipeline and shader binding table
class RayTracingPipeline
{
public:
  RayTracingPipeline(const Device& device);
  ~RayTracingPipeline();

  // Non-copyable
  RayTracingPipeline(const RayTracingPipeline&) = delete;
  RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;

  /// Create the ray tracing pipeline
  /// @param raygen_path Path to ray generation shader SPIR-V
  /// @param miss_path Path to miss shader SPIR-V
  /// @param closesthit_path Path to closest hit shader SPIR-V
  /// @param descriptor_set_layout Descriptor set layout for the pipeline
  /// @param vertex_stride_floats Number of floats per vertex (for closesthit specialization constant)
  void create(const std::string& raygen_path, const std::string& miss_path,
    const std::string& closesthit_path, vk::DescriptorSetLayout descriptor_set_layout,
    uint32_t vertex_stride_floats);

  /// Trace rays
  /// @param cmd Command buffer
  /// @param width Image width
  /// @param height Image height
  void trace_rays(vk::CommandBuffer cmd, uint32_t width, uint32_t height);

  [[nodiscard]] vk::Pipeline pipeline() const { return m_pipeline; }
  [[nodiscard]] vk::PipelineLayout layout() const { return m_layout; }

private:
  void create_shader_binding_table();
  vk::ShaderModule create_shader_module(const std::string& path);

  const Device* m_device{ nullptr };

  vk::Pipeline m_pipeline{ VK_NULL_HANDLE };
  vk::PipelineLayout m_layout{ VK_NULL_HANDLE };

  // Shader binding table
  vk::Buffer m_sbt_buffer{ VK_NULL_HANDLE };
  vk::DeviceMemory m_sbt_memory{ VK_NULL_HANDLE };

  vk::StridedDeviceAddressRegionKHR m_raygen_region{};
  vk::StridedDeviceAddressRegionKHR m_miss_region{};
  vk::StridedDeviceAddressRegionKHR m_hit_region{};
  vk::StridedDeviceAddressRegionKHR m_callable_region{};
};

} // namespace vkwave
