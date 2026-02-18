#include <vkwave/core/device.h>
#include <vkwave/core/fence.h>

#include <cassert>
#include <stdexcept>
#include <utility>

namespace vkwave
{

Fence::Fence(const Device& device, const std::string& name, const bool in_signaled_state)
  : m_device(device)
  , m_name(name)
{
  assert(!name.empty());
  assert(device.device());

  vk::FenceCreateInfo fenceInfo = {};
  if (in_signaled_state)
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

  m_device.create_fence(fenceInfo, &m_fence, m_name);
}

Fence::Fence(Fence&& other) noexcept
  : m_device(other.m_device)
{
  m_fence = std::exchange(other.m_fence, nullptr);
  m_name = std::move(other.m_name);
}

Fence::~Fence()
{
  m_device.device().destroyFence(m_fence);
}

void Fence::block(std::uint64_t timeout_limit) const
{
  vk::Result result = m_device.device().waitForFences(1, &m_fence, VK_TRUE, timeout_limit);
}

void Fence::reset() const
{
  vk::Result result = m_device.device().resetFences(1, &m_fence);
}

vk::Result Fence::status() const
{
  return m_device.device().getFenceStatus(m_fence);
}

} // namespace vkwave
