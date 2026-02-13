#include <vkwave/config.h>

#include <vkwave/core/device.h>
#include <vkwave/core/exception.h>
#include <vkwave/core/instance.h>
#include <vkwave/core/representation.h>

#include <optional>
#include <span>
#include <string>

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

namespace
{
std::vector<VkBool32> get_device_features_as_vector(const vk::PhysicalDeviceFeatures& features)
{
  std::vector<VkBool32> comparable_features(sizeof(vk::PhysicalDeviceFeatures) / sizeof(VkBool32));
  std::memcpy(comparable_features.data(), &features, sizeof(vk::PhysicalDeviceFeatures));
  return comparable_features;
}

std::string get_physical_device_name(const vk::PhysicalDevice physical_device)
{
  vk::PhysicalDeviceProperties properties = physical_device.getProperties();
  return properties.deviceName.data();
}

/// Check if a device extension is supported by a physical device
/// @param extensions The device extensions
/// @note If extensions is empty, this function returns ``false``
/// @param extension_name The extension name
/// @return ``true`` if the required device extension is supported
bool is_extension_supported(
  const std::vector<vk::ExtensionProperties>& extensions, const std::string& extension_name)
{
  return std::find_if(extensions.begin(), extensions.end(),
           [&](const vk::ExtensionProperties extension)
           { return extension_name == extension.extensionName.data(); }) != extensions.end();
}
}

namespace vkwave
{
constexpr float DEFAULT_QUEUE_PRIORITY = 1.0f;

void Device::log_device_properties(const vk::PhysicalDevice& device)
{
  vk::PhysicalDeviceProperties properties = device.getProperties();

  spdlog::trace("\tDevice name: {}", properties.deviceName.data());

  switch (properties.deviceType)
  {
    case (vk::PhysicalDeviceType::eCpu):
      spdlog::trace("\tDevice type: CPU");
      break;
    case (vk::PhysicalDeviceType::eDiscreteGpu):
      spdlog::trace("\tDevice type: Discrete GPU");
      break;
    case (vk::PhysicalDeviceType::eIntegratedGpu):
      spdlog::trace("\tDevice type: Integrated GPU");
      break;
    case (vk::PhysicalDeviceType::eVirtualGpu):
      spdlog::trace("\tDevice type: Virtual GPU");
      break;
    default:
      spdlog::trace("\tDevice type: Other");
  }
}

/// A function for rating physical devices by type
/// @param info The physical device info
/// @return A number from 0 to 2 which rates the physical device (higher is better)
std::uint32_t device_type_rating(const DeviceInfo& info)
{
  switch (info.type)
  {
    case vk::PhysicalDeviceType::eDiscreteGpu:
      return 2;
    case vk::PhysicalDeviceType::eIntegratedGpu:
      return 1;
    case vk::PhysicalDeviceType::eCpu:
    default:
      return 0;
  }
}

DeviceInfo build_device_info(const vk::PhysicalDevice physical_device, const vk::SurfaceKHR surface)
{
  vk::PhysicalDeviceProperties properties = physical_device.getProperties();

  vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();

  vk::PhysicalDeviceFeatures features = physical_device.getFeatures();

  vk::DeviceSize total_device_local = 0;
  for (std::size_t i = 0; i < memory_properties.memoryHeapCount; i++)
  {
    const auto& heap = memory_properties.memoryHeaps[i];
    if ((heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal))
    {
      total_device_local += heap.size;
    }
  }

  // Default to true in this case where a surface is not passed (and therefore presentation isn't
  // cared about)
  VkBool32 presentation_supported = VK_TRUE;
  {
    if (const auto result = vkGetPhysicalDeviceSurfaceSupportKHR(
          physical_device, 0, surface, &presentation_supported);
        result != VK_SUCCESS)
    {
      throw VulkanException("Error: vkGetPhysicalDeviceSurfaceSupportKHR failed!", result);
    }
  }

  // Additional check: actually try to get surface formats to verify presentation works
  // (workaround for NVIDIA PRIME driver bug where it claims support but fails)
  if (presentation_supported)
  {
    try
    {
      auto formats = physical_device.getSurfaceFormatsKHR(surface);
      if (formats.empty())
      {
        presentation_supported = VK_FALSE;
      }
    }
    catch (...)
    {
      spdlog::trace("Device {} failed getSurfaceFormatsKHR check", properties.deviceName.data());
      presentation_supported = VK_FALSE;
    }
  }

  const auto extensions = physical_device.enumerateDeviceExtensionProperties();

  const bool is_swapchain_supported =
    is_extension_supported(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  return DeviceInfo{
    .name = properties.deviceName.data(),
    .physical_device = physical_device,
    .type = properties.deviceType,
    .total_device_local = total_device_local,
    .features = features,
    .extensions = extensions,
    .presentation_supported = presentation_supported == VK_TRUE,
    .swapchain_supported = is_swapchain_supported,
  };
}

bool is_device_suitable(const DeviceInfo& info, const vk::PhysicalDeviceFeatures& required_features,
  const std::span<const char*> required_extensions, const bool print_info = false)
{
  const auto comparable_required_features = get_device_features_as_vector(required_features);
  const auto comparable_available_features = get_device_features_as_vector(info.features);
  constexpr auto FEATURE_COUNT = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);

  // Loop through all physical device features and check if a feature is required but not
  // supported
  for (std::size_t i = 0; i < FEATURE_COUNT; i++)
  {
    if (comparable_required_features[i] == VK_TRUE && comparable_available_features[i] == VK_FALSE)
    {
      if (print_info)
      {
        spdlog::info("Physical device {} does not support {}!", info.name,
          vkwave::utils::get_device_feature_description(i));
      }
      return false;
    }
  }
  // Loop through all device extensions and check if an extension is required but not supported
  for (const auto& extension : required_extensions)
  {
    if (!is_extension_supported(info.extensions, extension))
    {
      if (print_info)
      {
        spdlog::info("Physical device {} does not support extension {}!", info.name, extension);
      }
      return false;
    }
  }
  return info.presentation_supported && info.swapchain_supported;
}

bool compare_physical_devices(const vk::PhysicalDeviceFeatures& required_features,
  const std::span<const char*> required_extensions, const DeviceInfo& lhs, const DeviceInfo& rhs)
{
  if (!is_device_suitable(rhs, required_features, required_extensions))
  {
    return true;
  }
  if (!is_device_suitable(lhs, required_features, required_extensions))
  {
    return false;
  }
  if (device_type_rating(lhs) > device_type_rating(rhs))
  {
    return true;
  }
  if (device_type_rating(lhs) < device_type_rating(rhs))
  {
    return false;
  }
  // Device types equal, compare total amount of DEVICE_LOCAL memory
  return lhs.total_device_local >= rhs.total_device_local;
}

bool Device::is_presentation_supported(
  const vk::SurfaceKHR& surface, const std::uint32_t queue_family_index) const
{
  // Default to true in this case where a surface is not passed (and therefore presentation isn't
  // cared about)

  VkBool32 supported = VK_TRUE;
  {
    if (const auto result =
          vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, 0, surface, &supported);
        result != VK_SUCCESS)
    {
      throw VulkanException("Error: vkGetPhysicalDeviceSurfaceSupportKHR failed!", result);
    }
    return supported == VK_TRUE;
  }
}

vk::PhysicalDevice Device::pick_best_physical_device( //
  std::vector<DeviceInfo>&& physical_device_infos,
  const vk::PhysicalDeviceFeatures& required_features,
  const std::span<const char*> required_extensions)
{
  if (physical_device_infos.empty())
  {
    throw std::runtime_error("Error: There are no physical devices available!");
  }

  std::sort(physical_device_infos.begin(), physical_device_infos.end(),
    [&](const auto& lhs, const auto& rhs)
    { return compare_physical_devices(required_features, required_extensions, lhs, rhs); });
  if (!is_device_suitable(
        physical_device_infos.front(), required_features, required_extensions, true))
  {
    throw std::runtime_error("Error: Could not determine a suitable physical device!");
  }
  return physical_device_infos.front().physical_device;
}

vk::PhysicalDevice Device::pick_best_physical_device( //
  const Instance& inst, const vk::SurfaceKHR& surface,
  const vk::PhysicalDeviceFeatures& required_features, std::span<const char*> required_extensions,
  const std::string& preferred_gpu)
{
  std::vector<vk::PhysicalDevice> availableDevices = inst.instance().enumeratePhysicalDevices();

  // If a preferred GPU is specified, select it before querying surface info
  // (surface queries can hang on some drivers in hybrid GPU setups)
  if (!preferred_gpu.empty())
  {
    for (const auto& device : availableDevices)
    {
      auto props = device.getProperties();
      std::string name(props.deviceName.data());
      if (name.find(preferred_gpu) != std::string::npos)
      {
        spdlog::info("Preferred GPU '{}' found: selecting '{}'", preferred_gpu, name);
        return device;
      }
    }
    spdlog::warn("Preferred GPU '{}' not found, using default selection", preferred_gpu);
  }

  std::vector<DeviceInfo> infos(availableDevices.size());
  std::transform(availableDevices.begin(), availableDevices.end(), infos.begin(),
    [&](const vk::PhysicalDevice physical_device)
    { return build_device_info(physical_device, surface); });
  return Device::pick_best_physical_device(
    std::move(infos), required_features, required_extensions);
}

std::optional<std::uint32_t> Device::find_queue_family_index_if(
  const std::function<bool(std::uint32_t index, const vk::QueueFamilyProperties&)>& criteria_lambda)
{
  for (std::uint32_t index = 0;
       const auto queue_family : m_physical_device.getQueueFamilyProperties())
  {
    if (criteria_lambda(index, queue_family))
    {
      return index;
    }
    index++;
  }
  return std::nullopt;
}

Device::Device(const Instance& inst, vk::SurfaceKHR surface, bool prefer_distinct_transfer_queue,
  vk::PhysicalDevice physical_device,
  std::span<const char*> required_extensions, // contains swapchain
  const vk::PhysicalDeviceFeatures& required_features,
  const vk::PhysicalDeviceFeatures& optional_features,
  bool enable_ray_tracing)
  : m_physical_device(physical_device)
{
  if (!is_device_suitable(
        build_device_info(physical_device, surface), required_features, required_extensions))
  {
    throw std::runtime_error("Error: The chosen physical device {} is not suitable!");
  }

  m_gpu_name = get_physical_device_name(m_physical_device);
  spdlog::trace("Creating device using graphics card: {}", m_gpu_name);

  // Query ray tracing capabilities
  m_ray_tracing_capabilities = query_ray_tracing_capabilities(m_physical_device);

  // If ray tracing requested but not supported, disable it
  if (enable_ray_tracing && !m_ray_tracing_capabilities.supported)
  {
    spdlog::warn("Ray tracing requested but not supported on this device");
  }

  spdlog::trace("Creating Vulkan device queues");
  std::vector<vk::DeviceQueueCreateInfo> queues_to_create;

  if (prefer_distinct_transfer_queue)
  {
    spdlog::trace(
      "The application will try to use a distinct data transfer queue if it is available");
  }
  else
  {
    spdlog::warn("The application is forced not to use a distinct data transfer queue!");
  }

// Check if there is one queue family which can be used for both graphics and presentation
  auto queue_candidate = find_queue_family_index_if(
    [&](const std::uint32_t index, const vk::QueueFamilyProperties& queue_family)
    {
      return is_presentation_supported(surface, index) &&
        (queue_family.queueFlags & vk::QueueFlagBits::eGraphics);
    });

  if (!queue_candidate)
  {
    throw std::runtime_error("Error: Could not find a queue for both graphics and presentation!");
  }

  spdlog::trace("One queue for both graphics and presentation will be used");

  m_graphics_queue_family_index = *queue_candidate;
  m_present_queue_family_index = m_graphics_queue_family_index;

  // In this case, there is one queue family which can be used for both graphics and presentation
  queues_to_create.push_back(vk::DeviceQueueCreateInfo(
    vk::DeviceQueueCreateFlags(), *queue_candidate, 1, &vkwave::DEFAULT_QUEUE_PRIORITY));

  // Add another device queue just for data transfer
  queue_candidate = find_queue_family_index_if(
    [&](const std::uint32_t index, const vk::QueueFamilyProperties& queue_family)
    {
      return is_presentation_supported(surface, index) &&
        // No graphics bit, only transfer bit
        ((queue_family.queueFlags & vk::QueueFlagBits::eGraphics) == (vk::QueueFlagBits)0) &&
        (queue_family.queueFlags & vk::QueueFlagBits::eTransfer);
    });

  bool use_distinct_data_transfer_queue = false;

  if (queue_candidate && prefer_distinct_transfer_queue)
  {
    m_transfer_queue_family_index = *queue_candidate;

    spdlog::trace("A separate queue will be used for data transfer.");

    // We have the opportunity to use a separated queue for data transfer!
    use_distinct_data_transfer_queue = true;

    queues_to_create.push_back(vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(),
      m_transfer_queue_family_index, 1, &vkwave::DEFAULT_QUEUE_PRIORITY));
  }
  else
  {
    // We don't have the opportunity to use a separated queue for data transfer!
    // Do not create a new queue, use the graphics queue instead.
    use_distinct_data_transfer_queue = false;
  }

  if (!use_distinct_data_transfer_queue)
  {
    spdlog::warn("The application is forced to avoid distinct data transfer queues");
    spdlog::warn("Because of this, the graphics queue will be used for data transfer");

    m_transfer_queue_family_index = m_graphics_queue_family_index;
  }

  // TODO(Use C++)

  //  VkPhysicalDeviceFeatures available_features{};
  //  vkGetPhysicalDeviceFeatures(physical_device, &available_features);

  vk::PhysicalDeviceFeatures available_features = vk::PhysicalDeviceFeatures();

  const auto comparable_required_features = get_device_features_as_vector(required_features);
  const auto comparable_optional_features = get_device_features_as_vector(optional_features);
  const auto comparable_available_features = get_device_features_as_vector(available_features);

  constexpr auto FEATURE_COUNT = sizeof(vk::PhysicalDeviceFeatures) / sizeof(VkBool32);

  spdlog::trace("Number of features {}", FEATURE_COUNT);

  std::vector<VkBool32> features_to_enable(FEATURE_COUNT, VK_FALSE);

  for (std::size_t i = 0; i < FEATURE_COUNT; i++)
  {
    if (comparable_required_features[i] == VK_TRUE)
    {
      features_to_enable[i] = VK_TRUE;
    }
    if (comparable_optional_features[i] == VK_TRUE)
    {
      if (comparable_available_features[i] == VK_TRUE)
      {
        features_to_enable[i] = VK_TRUE;
      }
      else
      {
        spdlog::warn("The physical device {} does not support {}!",
          get_physical_device_name(physical_device),
          vkwave::utils::get_device_feature_description(i));
      }
    }
  }

  spdlog::trace("Number of features enabled {}", features_to_enable.size());

  std::memcpy(&m_enabled_features, features_to_enable.data(), features_to_enable.size());

  spdlog::trace("Creating physical device");

  std::vector<const char*> enabledLayers;

#ifdef VKWAVE_DEBUG
  enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  // Build list of extensions (required + optional ray tracing)
  std::vector<const char*> extensions_to_enable(required_extensions.begin(), required_extensions.end());

  // Extended dynamic state (for per-draw cull mode)
  extensions_to_enable.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);

  // Add ray tracing extensions if supported and requested
  if (enable_ray_tracing && m_ray_tracing_capabilities.supported)
  {
    extensions_to_enable.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    extensions_to_enable.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    extensions_to_enable.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    // Required by acceleration structure
    extensions_to_enable.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    // Required by ray tracing pipeline
    extensions_to_enable.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    extensions_to_enable.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

    spdlog::trace("Enabling ray tracing extensions");
  }

  // Extended dynamic state features (for per-draw cull mode)
  vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeatures{};
  extendedDynamicStateFeatures.extendedDynamicState = VK_TRUE;

  // Create device with extended features for ray tracing
  vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
  descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;

  vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
  bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
  bufferDeviceAddressFeatures.pNext = &descriptorIndexingFeatures;

  vk::PhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
  asFeatures.accelerationStructure = VK_TRUE;
  asFeatures.pNext = &bufferDeviceAddressFeatures;

  vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
  rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
  rtPipelineFeatures.pNext = &asFeatures;

  vk::DeviceCreateInfo deviceInfo = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), //
    queues_to_create.size(), queues_to_create.data(),                             //
    enabledLayers.size(), enabledLayers.data(),                                   //
    extensions_to_enable.size(), extensions_to_enable.data(),                     //
    &m_enabled_features);

  // Always chain extended dynamic state features
  deviceInfo.pNext = &extendedDynamicStateFeatures;

  // Chain ray tracing features if enabled
  if (enable_ray_tracing && m_ray_tracing_capabilities.supported)
  {
    extendedDynamicStateFeatures.pNext = &rtPipelineFeatures;
  }

  try
  {
    m_device = m_physical_device.createDevice(deviceInfo);
    spdlog::trace("GPU has been successfully abstracted!");
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("Device creation failed!");
    throw;
  }

  // Initialize default dispatcher with device (for device-level extension functions like ray tracing)
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

  // Consider using volkLoadDevice (bypass Vulkan loader dispatch code)
  m_dldi = vk::detail::DispatchLoaderDynamic(inst.instance(), vkGetInstanceProcAddr);
  m_dldi.init(m_device);

  spdlog::trace("Queue family indices:");
  spdlog::trace("   - Graphics: {}", m_graphics_queue_family_index);
  spdlog::trace("   - Present: {}", m_present_queue_family_index);
  spdlog::trace("   - Transfer: {}", m_transfer_queue_family_index);

  // Setup the queues for presentation and graphics.
  // Since we only have one queue per queue family, we acquire index 0.
  m_present_queue = m_device.getQueue(m_present_queue_family_index, 0);
  m_graphics_queue = m_device.getQueue(m_graphics_queue_family_index, 0);
}

RayTracingCapabilities Device::query_ray_tracing_capabilities(vk::PhysicalDevice physical_device)
{
  RayTracingCapabilities caps{};

  // Check for required extensions
  auto extensions = physical_device.enumerateDeviceExtensionProperties();

  bool has_acceleration_structure = false;
  bool has_ray_tracing_pipeline = false;
  bool has_deferred_host_ops = false;

  for (const auto& ext : extensions)
  {
    std::string name = ext.extensionName.data();
    if (name == VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
      has_acceleration_structure = true;
    if (name == VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
      has_ray_tracing_pipeline = true;
    if (name == VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
      has_deferred_host_ops = true;
  }

  caps.supported = has_acceleration_structure && has_ray_tracing_pipeline && has_deferred_host_ops;

  if (!caps.supported)
  {
    spdlog::trace("Ray tracing not supported: missing extensions");
    return caps;
  }

  // Query ray tracing pipeline properties
  vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rt_pipeline_props{};
  vk::PhysicalDeviceAccelerationStructurePropertiesKHR as_props{};

  vk::PhysicalDeviceProperties2 props2{};
  props2.pNext = &rt_pipeline_props;
  rt_pipeline_props.pNext = &as_props;

  physical_device.getProperties2(&props2);

  // Fill in pipeline properties
  caps.shaderGroupHandleSize = rt_pipeline_props.shaderGroupHandleSize;
  caps.maxRayRecursionDepth = rt_pipeline_props.maxRayRecursionDepth;
  caps.maxShaderGroupStride = rt_pipeline_props.maxShaderGroupStride;
  caps.shaderGroupBaseAlignment = rt_pipeline_props.shaderGroupBaseAlignment;
  caps.shaderGroupHandleAlignment = rt_pipeline_props.shaderGroupHandleAlignment;
  caps.maxRayHitAttributeSize = rt_pipeline_props.maxRayHitAttributeSize;

  // Fill in acceleration structure properties
  caps.maxGeometryCount = as_props.maxGeometryCount;
  caps.maxInstanceCount = as_props.maxInstanceCount;
  caps.maxPrimitiveCount = as_props.maxPrimitiveCount;
  caps.minAccelerationStructureScratchOffsetAlignment = as_props.minAccelerationStructureScratchOffsetAlignment;

  spdlog::trace("Ray tracing supported:");
  spdlog::trace("  - Max ray recursion depth: {}", caps.maxRayRecursionDepth);
  spdlog::trace("  - Max primitive count: {}", caps.maxPrimitiveCount);
  spdlog::trace("  - Shader group handle size: {}", caps.shaderGroupHandleSize);

  return caps;
}

vk::SampleCountFlagBits Device::max_usable_sample_count() const
{
  auto props = m_physical_device.getProperties();
  auto counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;

  for (auto s : { vk::SampleCountFlagBits::e64, vk::SampleCountFlagBits::e32,
         vk::SampleCountFlagBits::e16, vk::SampleCountFlagBits::e8,
         vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2 })
  {
    if (counts & s)
      return s;
  }
  return vk::SampleCountFlagBits::e1;
}

uint32_t Device::find_memory_type(
  uint32_t type_filter, vk::MemoryPropertyFlags properties) const
{
  vk::PhysicalDeviceMemoryProperties mem_properties = m_physical_device.getMemoryProperties();

  for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++)
  {
    if ((type_filter & (1 << i)) &&
        (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
    {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable memory type!");
}

Device::~Device()
{
  std::scoped_lock locker(m_mutex);

  // Because the device handle must be valid for the destruction of the command pools in the
  // CommandPool destructor, we must destroy the command pools manually here in order to ensure
  // the right order of destruction m_cmd_pools.clear();

  // Now that we destroyed the command pools, we can destroy the allocator and finally the device
  // itself
  // vmaDestroyAllocator(m_allocator);
  vkDestroyDevice(m_device, nullptr);
}

vk::SurfaceCapabilitiesKHR Device::surfaceCapabilities(const vk::SurfaceKHR& surface) const
{
  // TODO: May throw
  return m_physical_device.getSurfaceCapabilitiesKHR(surface);
}

void Device::wait_idle() const
{
  try
  {
    m_device.waitIdle();
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("wait_idle: {}", err.what());
    throw;
  }
}
void Device::create_fence(
  const vk::FenceCreateInfo& fenceCreateInfo, vk::Fence* pFence, const std::string& name) const
{
  try
  {
    *pFence = m_device.createFence(fenceCreateInfo);
    set_debug_name(reinterpret_cast<uint64_t>(static_cast<VkFence>(*pFence)), vk::ObjectType::eFence, name);
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("Failed to create fence");
    pFence = nullptr;
    throw;
  }
}

void Device::create_image_view(const vk::ImageViewCreateInfo& image_view_ci,
  vk::ImageView* pImageView, const std::string& name) const
{
  try
  {
    *pImageView = m_device.createImageView(image_view_ci);
    set_debug_name(reinterpret_cast<uint64_t>(static_cast<VkImageView>(*pImageView)), vk::ObjectType::eImageView, name);
  }
  catch (vk::SystemError err)
  {
    pImageView = nullptr;
    throw;
  }
}

void Device::create_semaphore(const vk::SemaphoreCreateInfo& semaphoreCreateInfo,
  vk::Semaphore* pSemaphore, const std::string& name) const
{
  try
  {
    *pSemaphore = m_device.createSemaphore(semaphoreCreateInfo);
    set_debug_name(reinterpret_cast<uint64_t>(static_cast<VkSemaphore>(*pSemaphore)), vk::ObjectType::eSemaphore, name);
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("Failed to create semaphore");
    pSemaphore = nullptr;
    throw;
  }
}

void Device::set_debug_name(
  uint64_t objectHandle, vk::ObjectType object_type, const std::string& name) const
{
#ifdef VKWAVE_DEBUG
  assert(objectHandle != 0);
  assert(!name.empty());

  vk::DebugUtilsObjectNameInfoEXT nameInfo{};
  nameInfo.objectType = object_type;
  nameInfo.objectHandle = objectHandle;
  nameInfo.pObjectName = name.c_str();

  m_device.setDebugUtilsObjectNameEXT(nameInfo, m_dldi);
#endif
}

void Device::begin_debug_label(
  vk::CommandBuffer cmd, const std::string& name, std::array<float, 4> color) const
{
#ifdef VKWAVE_DEBUG
  vk::DebugUtilsLabelEXT label{};
  label.pLabelName = name.c_str();
  for (size_t i = 0; i < 4; ++i)
  {
    label.color[i] = color[i];
  }
  cmd.beginDebugUtilsLabelEXT(label, m_dldi);
#endif
}

void Device::end_debug_label(vk::CommandBuffer cmd) const
{
#ifdef VKWAVE_DEBUG
  cmd.endDebugUtilsLabelEXT(m_dldi);
#endif
}

void Device::insert_debug_label(
  vk::CommandBuffer cmd, const std::string& name, std::array<float, 4> color) const
{
#ifdef VKWAVE_DEBUG
  vk::DebugUtilsLabelEXT label{};
  label.pLabelName = name.c_str();
  for (size_t i = 0; i < 4; ++i)
  {
    label.color[i] = color[i];
  }
  cmd.insertDebugUtilsLabelEXT(label, m_dldi);
#endif
}
}
