#pragma once
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include <vkwave/config.h>
#include <vulkan/vulkan.hpp>

namespace vkwave
{
class Instance
{
public:
  static constexpr std::uint32_t REQUIRED_VK_API_VERSION{ VK_MAKE_API_VERSION(0, 1, 2, 0) };

  [[nodiscard]] static bool is_layer_supported(const std::string& layer_name);
  [[nodiscard]] static bool is_extension_supported(const std::string& extension_name);

  Instance() = default;

  Instance(const Instance&) = delete;
  Instance(Instance&&) noexcept;

  ~Instance();

  Instance& operator=(const Instance&) = delete;
  Instance& operator=(Instance&&) = default;

  // --- Setters (call before init()) ---

  void set_application_name(const std::string& name) { m_app_name = name; m_modified = true; }
  void set_engine_name(const std::string& name) { m_engine_name = name; m_modified = true; }
  void set_application_version(std::uint32_t v) { m_app_version = v; m_modified = true; }
  void set_engine_version(std::uint32_t v) { m_engine_version = v; m_modified = true; }
  void set_validation_layers(bool enable) { m_enable_validation_layers = enable; m_modified = true; }
  void set_renderdoc_layer(bool enable) { m_enable_renderdoc_layer = enable; m_modified = true; }

  void add_extension(const std::string& ext) { m_requested_extensions.push_back(ext); m_modified = true; }
  void add_layer(const std::string& layer) { m_requested_layers.push_back(layer); m_modified = true; }

  // --- Initialization ---

  void init();

  // --- Accessor ---

  [[nodiscard]] const vk::Instance instance() const
  {
    assert(!m_modified && "Instance not initialized — call init() first");
    return m_instance;
  }

private:
  // Vulkan handles
  vk::detail::DispatchLoaderDynamic m_dldi;
  vk::Instance m_instance{ VK_NULL_HANDLE };
  vk::DebugUtilsMessengerEXT m_debugMessenger{ nullptr };

  // Configuration (defaults)
  std::string m_app_name{ "vkwave" };
  std::string m_engine_name{ "vkwave" };
  std::uint32_t m_app_version{ VK_MAKE_API_VERSION(0, 0, 1, 0) };
  std::uint32_t m_engine_version{ VK_MAKE_API_VERSION(0, 0, 1, 0) };
  bool m_enable_validation_layers{ true };
  bool m_enable_renderdoc_layer{ false };
  std::vector<std::string> m_requested_extensions;
  std::vector<std::string> m_requested_layers;

  bool m_modified{ true };
};
}
