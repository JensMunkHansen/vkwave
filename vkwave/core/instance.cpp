#include <vkwave/config.h>

#include <vulkan/vulkan.hpp>

// Dynamic dispatch loader storage (must be in exactly one cpp file)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <vkwave/core/exception.h>
#include <vkwave/core/instance.h>
#include <vkwave/core/representation.h>
#include <utility>
#include <vulkan/vulkan_structs.hpp>

#include <iostream>

VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
  vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  vk::DebugUtilsMessageTypeFlagsEXT messageType,
  const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
  spdlog::trace("Validation Layer: {}", pCallbackData->pMessage);
  return vk::False;
}

namespace vkwave
{

Instance::Instance(Instance&& other) noexcept
{
  m_instance = std::exchange(other.m_instance, nullptr);
  m_debugMessenger = std::exchange(other.m_debugMessenger, nullptr);
}

Instance::~Instance()
{
#ifdef VKWAVE_DEBUG
  m_instance.destroy(m_debugMessenger, nullptr, m_dldi);
#endif
  m_instance.destroy();
}

bool Instance::is_layer_supported(const std::string& layer_name)
{
  std::vector<vk::LayerProperties> supportedLayers = vk::enumerateInstanceLayerProperties();
  return std::find_if(supportedLayers.begin(), supportedLayers.end(),
           [&](const vk::LayerProperties instance_layer)
           { return layer_name == instance_layer.layerName.data(); }) != supportedLayers.end();
}

bool Instance::is_extension_supported(const std::string& extension_name)
{
  std::vector<vk::ExtensionProperties> supportedExtensions =
    vk::enumerateInstanceExtensionProperties();

  return std::find_if(supportedExtensions.begin(), supportedExtensions.end(),
           [&](const vk::ExtensionProperties instance_extension) {
             return extension_name == instance_extension.extensionName.data();
           }) != supportedExtensions.end();
}

Instance::Instance(const std::string& application_name, const std::string& engine_name,
  const std::uint32_t application_version, const std::uint32_t engine_version,
  bool enable_validation_layers, bool enable_renderdoc_layer,
  const std::vector<std::string>& requested_instance_extensions,
  const std::vector<std::string>& requested_instance_layers)
{
  m_enable_validation_layers = enable_validation_layers;

  assert(!application_name.empty());
  assert(!engine_name.empty());

  spdlog::trace("Initializing Vulkan metaloader");

  // Initialize dynamic dispatch loader with vkGetInstanceProcAddr
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  uint32_t version{ 0 };
  vkEnumerateInstanceVersion(&version);

  spdlog::trace("System can support vulkan Variant: {}, Major: {}, Minor: {}, Patch: {}",
    VK_API_VERSION_VARIANT(version), VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
    VK_API_VERSION_PATCH(version));

  spdlog::trace("Initialising Vulkan instance");
  spdlog::trace("Application name: {}", application_name);
  spdlog::trace("Application version: {}.{}.{}", VK_API_VERSION_MAJOR(application_version),
    VK_API_VERSION_MINOR(application_version), VK_API_VERSION_PATCH(application_version));
  spdlog::trace("Engine name: {}", engine_name);
  spdlog::trace("Engine version: {}.{}.{}", VK_API_VERSION_MAJOR(engine_version),
    VK_API_VERSION_MINOR(engine_version), VK_API_VERSION_PATCH(engine_version));
  spdlog::trace("Requested Vulkan API version: {}.{}.{}",
    VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION), VK_API_VERSION_MINOR(REQUIRED_VK_API_VERSION),
    VK_API_VERSION_PATCH(REQUIRED_VK_API_VERSION));

  std::uint32_t available_api_version = 0;
  if (const VkResult result = vkEnumerateInstanceVersion(&available_api_version);
      result != VK_SUCCESS)
  {
    spdlog::error("Error: vkEnumerateInstanceVersion returned {}!", utils::as_string(result));
    return;
  }

  // This code will throw an exception if the required version of Vulkan API is not available on the
  // system
  if (VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION) > VK_API_VERSION_MAJOR(available_api_version) ||
    (VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION) == VK_API_VERSION_MAJOR(available_api_version) &&
      VK_API_VERSION_MINOR(REQUIRED_VK_API_VERSION) > VK_API_VERSION_MINOR(available_api_version)))
  {
    std::string exception_message = fmt::format(
      "Your system does not support the required version of Vulkan API. Required version: "
      "{}.{}.{}. Available "
      "Vulkan API version on this machine: {}.{}.{}. Please update your graphics drivers!",
      std::to_string(VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION)),
      std::to_string(VK_API_VERSION_MINOR(REQUIRED_VK_API_VERSION)),
      std::to_string(VK_API_VERSION_PATCH(REQUIRED_VK_API_VERSION)),
      std::to_string(VK_API_VERSION_MAJOR(available_api_version)),
      std::to_string(VK_API_VERSION_MINOR(available_api_version)),
      std::to_string(VK_API_VERSION_PATCH(available_api_version)));
    throw std::runtime_error(exception_message);
  }

  vk::ApplicationInfo applicationInfo;
  applicationInfo.pApplicationName = application_name.c_str();
  applicationInfo.applicationVersion = application_version;
  applicationInfo.pEngineName = engine_name.c_str();
  applicationInfo.engineVersion = engine_version;
  applicationInfo.apiVersion = REQUIRED_VK_API_VERSION;

  std::vector<const char*> instance_extension_wishlist = {
#ifdef VKWAVE_DEBUG
    // In debug mode, we use the following instance extensions:
    // This one is for assigning internal names to Vulkan resources.
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    // This one is for setting up a Vulkan debug report callback function.
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
  };

  std::uint32_t glfw_extension_count = 0;

  // Because this requires some dynamic libraries to be loaded, this may take even up to some
  // seconds!
  const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

  if (glfw_extension_count == 0)
  {
    throw std::runtime_error("Error: glfwGetRequiredInstanceExtensions results 0 as number of "
                             "required instance extensions!");
  }

  spdlog::trace("Required GLFW instance extensions:");

  // Add all instance extensions which are required by GLFW to our wishlist.
  for (std::size_t i = 0; i < glfw_extension_count; i++)
  {
    spdlog::trace("   - {}", glfw_extensions[i]);              // NOLINT
    instance_extension_wishlist.push_back(glfw_extensions[i]); // NOLINT
  }

  // We have to check which instance extensions of our wishlist are available on the current system!
  // Add requested instance extensions to wishlist.
  for (const auto& requested_instance_extension : requested_instance_extensions)
  {
    instance_extension_wishlist.push_back(requested_instance_extension.c_str());
  }

  std::vector<const char*> enabled_instance_extensions{};

  spdlog::trace("List of enabled instance extensions:");

  // Enumerate extensions once for efficiency
  std::vector<vk::ExtensionProperties> available_extensions =
    vk::enumerateInstanceExtensionProperties();

  auto is_ext_available = [&](const std::string& ext_name) {
    return std::find_if(available_extensions.begin(), available_extensions.end(),
             [&](const vk::ExtensionProperties& ext) {
               return ext_name == ext.extensionName.data();
             }) != available_extensions.end();
  };

  // We are not checking for duplicated entries but this is no problem.
  for (const auto& instance_extension : instance_extension_wishlist)
  {
    if (is_ext_available(instance_extension))
    {
      spdlog::trace("   - {} ", instance_extension);
      enabled_instance_extensions.push_back(instance_extension);
    }
    else
    {
      spdlog::error(
        "Requested instance extension {} is not available on this system!", instance_extension);
    }
  }

  std::vector<const char*> instance_layers_wishlist{};

  spdlog::trace("Instance layer wishlist:");

#ifdef VKWAVE_DEBUG
  // RenderDoc is a very useful open source graphics debugger for Vulkan and other APIs.
  // Not using it all the time during development is fine, but as soon as something crashes
  // you should enable it, take a snapshot and look up what's wrong.
  if (enable_renderdoc_layer)
  {
    spdlog::trace("   - VK_LAYER_RENDERDOC_Capture");
    instance_layers_wishlist.push_back("VK_LAYER_RENDERDOC_Capture");
  }

  // We can't stress enough how important it is to use validation layers during development!
  // Validation layers in Vulkan are in-depth error checks for the application's use of the API.
  // They check for a multitude of possible errors. They can be disabled easily for releases.
  // Understand that in contrary to other APIs, in Vulkan API the driver provides no error checks
  // for you! If you use Vulkan API incorrectly, your application will likely just crash.
  // To avoid this, you must use validation layers during development!
  if (enable_validation_layers)
  {
    spdlog::trace("   - VK_LAYER_KHRONOS_validation");
    instance_layers_wishlist.push_back("VK_LAYER_KHRONOS_validation");
  }

#endif

  // Add requested instance layers to wishlist.
  for (const auto& instance_layer : requested_instance_layers)
  {
    instance_layers_wishlist.push_back(instance_layer.c_str());
  }

  std::vector<const char*> enabled_instance_layers{};

  spdlog::trace("List of enabled instance layers:");

  // Enumerate layers once for efficiency
  std::vector<vk::LayerProperties> available_layers = vk::enumerateInstanceLayerProperties();

  auto is_layer_available = [&](const std::string& layer_name) {
    return std::find_if(available_layers.begin(), available_layers.end(),
             [&](const vk::LayerProperties& layer) {
               return layer_name == layer.layerName.data();
             }) != available_layers.end();
  };

  // We have to check which instance layers of our wishlist are available on the current system!
  // We are not checking for duplicated entries but this is no problem.
  for (const auto& current_layer : instance_layers_wishlist)
  {
    if (is_layer_available(current_layer))
    {
      spdlog::trace("   - {}", current_layer);
      enabled_instance_layers.push_back(current_layer);
    }
    else
    {
#ifndef VKWAVE_DEBUG
      if (std::string(current_layer) == VK_EXT_DEBUG_MARKER_EXTENSION_NAME)
      {
        spdlog::error("You can't use command line argument -renderdoc in release mode");
      }
#else
      spdlog::trace("Requested instance layer {} is not available on this system!", current_layer);
#endif
    }
  }

  // Crash here
  vk::InstanceCreateInfo instanceCreateInfo;
  instanceCreateInfo.setFlags(vk::InstanceCreateFlags());
  instanceCreateInfo.setPApplicationInfo(&applicationInfo);
  instanceCreateInfo.setPEnabledExtensionNames(enabled_instance_extensions);
  instanceCreateInfo.setPEnabledLayerNames(validationLayers);
  instanceCreateInfo.setEnabledLayerCount(static_cast<std::uint32_t>(validationLayers.size()));
  instanceCreateInfo.setEnabledExtensionCount(
    static_cast<std::uint32_t>(enabled_instance_extensions.size()));

  if (const vk::Result result = vk::createInstance(&instanceCreateInfo, nullptr, &m_instance);
      result != vk::Result::eSuccess)
  {
    spdlog::trace("Bum. {}", vk::to_string(result));
    throw VulkanException("Failed to create Vulkan instance.", static_cast<VkResult>(result));
  }

  // Initialize default dispatcher with instance (for instance-level extension functions)
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

#ifdef VKWAVE_DEBUG
  // We cannot use this in release mode
  vk::DebugUtilsMessengerCreateInfoEXT createInfo{
    {},
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
      vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
      vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
    debugCallback, nullptr};

  m_dldi = vk::detail::DispatchLoaderDynamic(m_instance, vkGetInstanceProcAddr);

  m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(createInfo, nullptr, m_dldi);
#endif
}

Instance::Instance(const std::string& application_name, const std::string& engine_name,
  const std::uint32_t application_version, const std::uint32_t engine_version,
  bool enable_validation_layers, bool enable_renderdoc_layer)
  : Instance(application_name, engine_name, application_version, engine_version,
      enable_validation_layers, enable_renderdoc_layer, {}, {})
{
}
void Instance::setup_vulkan_debug_callback()
{
  if (m_enable_validation_layers)
  {
    spdlog::trace("Khronos validation layers are enabled");
  }
  // TODO:::
}
}
