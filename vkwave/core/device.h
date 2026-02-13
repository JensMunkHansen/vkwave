#pragma once

#include <vulkan/vulkan.hpp>

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <spdlog/spdlog.h>

namespace vkwave
{

class Instance;

struct DeviceInfo
{
  std::string name;
  vk::PhysicalDevice physical_device{ nullptr };
  vk::PhysicalDeviceType type{ VK_PHYSICAL_DEVICE_TYPE_OTHER };
  vk::DeviceSize total_device_local{ 0 };
  vk::PhysicalDeviceFeatures features{};
  std::vector<vk::ExtensionProperties> extensions;
  bool presentation_supported{ false };
  bool swapchain_supported{ false };
};

/// Ray tracing capabilities and properties
struct RayTracingCapabilities
{
  bool supported{ false };

  // Pipeline properties
  uint32_t shaderGroupHandleSize{ 0 };
  uint32_t maxRayRecursionDepth{ 0 };
  uint32_t maxShaderGroupStride{ 0 };
  uint32_t shaderGroupBaseAlignment{ 0 };
  uint32_t shaderGroupHandleAlignment{ 0 };
  uint32_t maxRayHitAttributeSize{ 0 };

  // Acceleration structure properties
  uint64_t maxGeometryCount{ 0 };
  uint64_t maxInstanceCount{ 0 };
  uint64_t maxPrimitiveCount{ 0 };
  uint32_t minAccelerationStructureScratchOffsetAlignment{ 0 };
};

/*
 * Vulkan separates the concept of physical and logical devices.
 *
 * A physical device usually represents a single complete
 * implementation of Vulkan (excluding instance-level functionality)
 * available to the host, of which there are a finite number.
 *
 * A logical device represents an instance of that implementation
 * with its own state and resources independent of other logical devices.
 */
class Device
{
public:
  /// Find a queue family index that suits a specific criteria
  /// @param criteria_lambda The lambda to sort out unsuitable queue families
  /// @return The queue family index which was found (if any), ``std::nullopt`` otherwise
  std::optional<std::uint32_t> find_queue_family_index_if(
    const std::function<bool(std::uint32_t index, const vk::QueueFamilyProperties&)>&
      criteria_lambda);

  bool is_presentation_supported(
    const vk::SurfaceKHR& surface, const std::uint32_t queue_family_index) const;

  static vk::PhysicalDevice pick_best_physical_device( //
    const Instance& inst, const vk::SurfaceKHR& surface,
    const vk::PhysicalDeviceFeatures& required_features,
    std::span<const char*> required_extensions,
    const std::string& preferred_gpu = "");

  static void log_device_properties(const vk::PhysicalDevice& device);

  static vk::PhysicalDevice pick_best_physical_device(
    std::vector<DeviceInfo>&& physical_device_infos,
    const vk::PhysicalDeviceFeatures& required_features,
    const std::span<const char*> required_extensions);

  Device(const Instance& inst, vk::SurfaceKHR surface, bool prefer_distinct_transfer_queue,
    vk::PhysicalDevice physical_device, std::span<const char*> required_extensions,
    const vk::PhysicalDeviceFeatures& required_features,
    const vk::PhysicalDeviceFeatures& optional_features = {},
    bool enable_ray_tracing = true);

  Device(const Device&) = delete;
  Device(Device&&) noexcept;
  ~Device();

  Device& operator=(const Device&) = delete;
  Device& operator=(Device&&) = delete;

  [[nodiscard]] vk::Device device() const { return m_device; }
  [[nodiscard]] vk::PhysicalDevice physicalDevice() const { return m_physical_device; }

  [[nodiscard]] const std::string& gpu_name() const { return m_gpu_name; }

  [[nodiscard]] vk::Queue graphics_queue() const { return m_graphics_queue; }

  [[nodiscard]] vk::Queue present_queue() const { return m_present_queue; }

  [[nodiscard]] vk::Queue transfer_queue() const { return m_transfer_queue; }

  void wait_idle() const;

  vk::SurfaceCapabilitiesKHR surfaceCapabilities(const vk::SurfaceKHR& surface) const;

  void create_semaphore(const vk::SemaphoreCreateInfo& semaphoreCreateInfo,
    vk::Semaphore* pSemaphore, const std::string& name) const;

  void create_fence(
    const vk::FenceCreateInfo& fenceCreateInfor, vk::Fence* pFence, const std::string& name) const;

  void create_image_view(const vk::ImageViewCreateInfo& image_view_ci, vk::ImageView* image_view,
    const std::string& name) const;

  void set_debug_name(uint64_t objectHandle, vk::ObjectType object_type, const std::string& name) const;

  /// Find a suitable memory type for allocation
  /// @param type_filter Bitmask of acceptable memory types
  /// @param properties Required memory properties
  /// @return Index of suitable memory type
  [[nodiscard]] uint32_t find_memory_type(
    uint32_t type_filter, vk::MemoryPropertyFlags properties) const;

  /// Check if ray tracing is supported and query capabilities
  static RayTracingCapabilities query_ray_tracing_capabilities(vk::PhysicalDevice physical_device);

  /// Get ray tracing capabilities (call after device creation)
  [[nodiscard]] const RayTracingCapabilities& ray_tracing_capabilities() const
  {
    return m_ray_tracing_capabilities;
  }

  /// Check if ray tracing is available on this device
  [[nodiscard]] bool supports_ray_tracing() const { return m_ray_tracing_capabilities.supported; }

  /// Query the maximum usable MSAA sample count (intersection of color and depth)
  [[nodiscard]] vk::SampleCountFlagBits max_usable_sample_count() const;

  void begin_debug_label(vk::CommandBuffer cmd, const std::string& name,
    std::array<float, 4> color = { 1.0f, 1.0f, 1.0f, 1.0f }) const;
  void end_debug_label(vk::CommandBuffer cmd) const;
  void insert_debug_label(vk::CommandBuffer cmd, const std::string& name,
    std::array<float, 4> color = { 1.0f, 1.0f, 1.0f, 1.0f }) const;

private:
  //  mutable std::vector<std::unique_ptr<CommandPool>> m_cmd_pools;

  vk::Device m_device{ VK_NULL_HANDLE };
  vk::PhysicalDevice m_physical_device{ VK_NULL_HANDLE };
  std::string m_gpu_name;

  vk::PhysicalDeviceFeatures m_enabled_features{};
  RayTracingCapabilities m_ray_tracing_capabilities{};

  vk::Queue m_graphics_queue{ VK_NULL_HANDLE };
  vk::Queue m_present_queue{ VK_NULL_HANDLE };
  vk::Queue m_transfer_queue{ VK_NULL_HANDLE };

public:
  // Find other way to expose to swapchain
  std::uint32_t m_present_queue_family_index{ 0 };
  std::uint32_t m_graphics_queue_family_index{ 0 };
  std::uint32_t m_transfer_queue_family_index{ 0 };

private:
  mutable std::vector<std::unique_ptr<vk::CommandPool>> m_cmd_pools;
  mutable std::mutex m_mutex;

  vk::detail::DispatchLoaderDynamic m_dldi;
};
}
