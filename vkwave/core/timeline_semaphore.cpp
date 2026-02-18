#include <vkwave/core/timeline_semaphore.h>

#include <vkwave/core/device.h>

#include <cassert>
#include <utility>

namespace vkwave
{

TimelineSemaphore::TimelineSemaphore(
  const Device& device, const std::string& name, uint64_t initial_value)
  : m_device(device)
  , m_name(name)
{
  assert(!name.empty());

  vk::SemaphoreTypeCreateInfo type_info{};
  type_info.semaphoreType = vk::SemaphoreType::eTimeline;
  type_info.initialValue = initial_value;

  vk::SemaphoreCreateInfo ci{};
  ci.pNext = &type_info;

  device.create_semaphore(ci, &m_semaphore, m_name);
}

TimelineSemaphore::TimelineSemaphore(TimelineSemaphore&& other) noexcept
  : m_device(other.m_device)
{
  m_semaphore = std::exchange(other.m_semaphore, VK_NULL_HANDLE);
  m_name = std::move(other.m_name);
}

TimelineSemaphore::~TimelineSemaphore()
{
  if (m_semaphore)
    vkDestroySemaphore(m_device.device(), m_semaphore, nullptr);
}

void TimelineSemaphore::wait(uint64_t value, uint64_t timeout) const
{
  vk::SemaphoreWaitInfo wait_info{};
  wait_info.semaphoreCount = 1;
  wait_info.pSemaphores = &m_semaphore;
  wait_info.pValues = &value;

  auto result = m_device.device().waitSemaphores(wait_info, timeout);
  if (result != vk::Result::eSuccess && result != vk::Result::eTimeout)
  {
    throw std::runtime_error("TimelineSemaphore::wait failed");
  }
}

uint64_t TimelineSemaphore::current_value() const
{
  return m_device.device().getSemaphoreCounterValue(m_semaphore);
}

} // namespace vkwave
