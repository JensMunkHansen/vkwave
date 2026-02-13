#include <vkwave/core/semaphore.h>

#include <vkwave/core/device.h>

#include <cassert>
#include <utility>
#include <vulkan/vulkan_enums.hpp>

namespace vkwave
{

Semaphore::Semaphore(const Device& device, const std::string& name)
  : m_device(device)
  , m_name(name)
{
  assert(!name.empty());
  vk::SemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.flags = vk::SemaphoreCreateFlags();
  device.create_semaphore(semaphoreInfo, &m_semaphore, m_name);
}

Semaphore::Semaphore(Semaphore&& other) noexcept
  : m_device(other.m_device)
{
  m_semaphore = std::exchange(other.m_semaphore, VK_NULL_HANDLE);
  m_name = std::move(other.m_name);
}

Semaphore::~Semaphore()
{
  vkDestroySemaphore(m_device.device(), m_semaphore, nullptr);
}

}
