#pragma once

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <string>

namespace vkwave
{

class Device;

/// RAII wrapper for a Vulkan timeline semaphore.
///
/// Timeline semaphores replace per-slot fences for CPU-GPU synchronization.
/// A single timeline semaphore can drain all in-flight frames with one
/// vkWaitSemaphores call (vs looping over N fences).
class TimelineSemaphore
{
  const Device& m_device;
  vk::Semaphore m_semaphore{ VK_NULL_HANDLE };
  std::string m_name;

public:
  TimelineSemaphore(const Device& device, const std::string& name,
                    uint64_t initial_value = 0);
  TimelineSemaphore(TimelineSemaphore&&) noexcept;
  ~TimelineSemaphore();

  // Non-copyable
  TimelineSemaphore(const TimelineSemaphore&) = delete;
  TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;
  TimelineSemaphore& operator=(TimelineSemaphore&&) = delete;

  /// Block until the semaphore reaches at least @p value.
  void wait(uint64_t value, uint64_t timeout = UINT64_MAX) const;

  /// Query the current counter value.
  [[nodiscard]] uint64_t current_value() const;

  /// Raw handle.
  [[nodiscard]] vk::Semaphore get() const { return m_semaphore; }

  /// Pointer to raw handle (for Vulkan info structs).
  [[nodiscard]] const vk::Semaphore* ptr() const { return &m_semaphore; }
};

} // namespace vkwave
