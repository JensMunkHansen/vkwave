#include <vkwave/core/image.h>

#include <vkwave/core/device.h>

#include <cassert>

namespace vkwave
{

Image::Image(const Device& device, const vk::Format format, const vk::ImageUsageFlags image_usage,
  const vk::ImageAspectFlags aspect_flags, const vk::SampleCountFlagBits sample_count,
  const std::string& name, const vk::Extent2D image_extent)
  : m_device(device)
  , m_format(format)
  , m_name(name)
{
  assert(device.device());
  assert(device.physicalDevice());
  assert(image_extent.width > 0);
  assert(image_extent.height > 0);
  assert(!name.empty());
}
}
