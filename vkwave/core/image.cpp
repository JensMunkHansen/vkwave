#include <vkwave/core/image.h>
#include <vkwave/core/device.h>

#include <spdlog/spdlog.h>

#include <utility>

namespace vkwave
{

Image::Image(const Device& device, vk::Format format, vk::Extent2D extent,
  vk::ImageUsageFlags usage, const std::string& name,
  vk::SampleCountFlagBits samples)
  : m_device(device.device()), m_format(format), m_extent(extent)
{
  // Multisample images are transient (content discarded after resolve)
  if (samples != vk::SampleCountFlagBits::e1)
    usage |= vk::ImageUsageFlagBits::eTransientAttachment;

  // Create image
  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.extent = vk::Extent3D{ extent.width, extent.height, 1 };
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.format = format;
  image_info.tiling = vk::ImageTiling::eOptimal;
  image_info.initialLayout = vk::ImageLayout::eUndefined;
  image_info.usage = usage;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.samples = samples;

  m_image = m_device.createImage(image_info);

  // Allocate and bind device-local memory
  auto mem_reqs = m_device.getImageMemoryRequirements(m_image);
  vk::MemoryAllocateInfo alloc_info{};
  alloc_info.allocationSize = mem_reqs.size;
  alloc_info.memoryTypeIndex = device.find_memory_type(
    mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_memory = m_device.allocateMemory(alloc_info);
  m_device.bindImageMemory(m_image, m_memory, 0);

  // Create image view
  vk::ImageViewCreateInfo view_info{};
  view_info.image = m_image;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.format = format;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;

  device.create_image_view(view_info, &m_view, name);
  device.set_debug_name(
    reinterpret_cast<uint64_t>(static_cast<VkImage>(m_image)),
    vk::ObjectType::eImage, name);

  spdlog::trace("Created Image '{}' ({}x{} {})", name,
    extent.width, extent.height, vk::to_string(format));
}

Image::~Image()
{
  destroy();
}

Image::Image(Image&& other) noexcept
  : m_device(other.m_device)
  , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
  , m_memory(std::exchange(other.m_memory, VK_NULL_HANDLE))
  , m_view(std::exchange(other.m_view, VK_NULL_HANDLE))
  , m_format(other.m_format)
  , m_extent(other.m_extent)
{
}

Image& Image::operator=(Image&& other) noexcept
{
  if (this != &other)
  {
    destroy();
    m_device = other.m_device;
    m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
    m_memory = std::exchange(other.m_memory, VK_NULL_HANDLE);
    m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
    m_format = other.m_format;
    m_extent = other.m_extent;
  }
  return *this;
}

void Image::destroy()
{
  if (!m_device)
    return;

  if (m_view)
    m_device.destroyImageView(m_view);
  if (m_image)
    m_device.destroyImage(m_image);
  if (m_memory)
    m_device.freeMemory(m_memory);

  m_view = VK_NULL_HANDLE;
  m_image = VK_NULL_HANDLE;
  m_memory = VK_NULL_HANDLE;
}

} // namespace vkwave
