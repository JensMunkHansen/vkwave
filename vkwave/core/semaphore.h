#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

namespace vkwave
{
// Forward declaration
class Device;

/// RAII wrapper class for VkSemaphore
class Semaphore
{
  const Device& m_device;
  vk::Semaphore m_semaphore{ VK_NULL_HANDLE };
  std::string m_name;

public:
  /// Default constructor
  /// @param device The const reference to a device RAII wrapper instance.
  /// @param name The internal debug marker name of the VkSemaphore.
  Semaphore(const Device& device, const std::string& name);
  Semaphore(const Semaphore&) = delete;
  Semaphore(Semaphore&&) noexcept;
  ~Semaphore();

  Semaphore& operator=(const Semaphore&) = delete;
  Semaphore& operator=(Semaphore&&) = delete;

  [[nodiscard]] const vk::Semaphore* semaphore() const { return &m_semaphore; }
};
}
