#pragma once
#include <cstdint>

#include <vkwave/config.h>
#include <vulkan/vulkan.hpp>

namespace vkwave
{
class Instance
{
private:
  vk::detail::DispatchLoaderDynamic m_dldi;
  vk::Instance m_instance{ VK_NULL_HANDLE };
  vk::DebugUtilsMessengerEXT m_debugMessenger{ nullptr };
  bool m_enable_validation_layers{ true };

  static constexpr std::uint32_t REQUIRED_VK_API_VERSION{ VK_MAKE_API_VERSION(0, 1, 2, 0) };
  static constexpr auto validationLayers = { "VK_LAYER_KHRONOS_validation" };

public:
  [[nodiscard]] static bool is_layer_supported(const std::string& layer_name);
  [[nodiscard]] static bool is_extension_supported(const std::string& extension_name);

  void setup_vulkan_debug_callback();
  Instance(const std::string& application_name, const std::string& engine_name,
    std::uint32_t application_version, std::uint32_t engine_version, bool enable_validation_layers,
    bool enable_renderdoc_layer, const std::vector<std::string>& requested_instance_extensions,
    const std::vector<std::string>& requested_instance_layers);

  Instance(const std::string& application_name, const std::string& engine_name,
    std::uint32_t application_version, std::uint32_t engine_version, bool enable_validation_layers,
    bool enable_renderdoc_layer);

  Instance(const Instance&) = delete;
  Instance(Instance&&) noexcept;

  ~Instance();

  Instance& operator=(const Instance&) = delete;
  Instance& operator=(Instance&&) = default;

  [[nodiscard]] const vk::Instance instance() const { return m_instance; }
};
}
