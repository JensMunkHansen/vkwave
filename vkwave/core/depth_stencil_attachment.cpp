#include <vkwave/core/depth_stencil_attachment.h>
#include <vkwave/core/device.h>

#include <spdlog/spdlog.h>

#include <utility>

namespace vkwave
{

static bool format_has_stencil(vk::Format fmt)
{
  return fmt == vk::Format::eD32SfloatS8Uint ||
         fmt == vk::Format::eD24UnormS8Uint ||
         fmt == vk::Format::eD16UnormS8Uint ||
         fmt == vk::Format::eS8Uint;
}

DepthStencilAttachment::DepthStencilAttachment(const Device& device, vk::Format format,
  vk::Extent2D extent, vk::SampleCountFlagBits samples,
  vk::ImageUsageFlags extraUsage)
  : m_vkDevice(device.device()), m_format(format), m_extent(extent)
{
  const bool stencil = format_has_stencil(format);

  // Determine aspect mask for the full image
  vk::ImageAspectFlags fullAspect = vk::ImageAspectFlagBits::eDepth;
  if (stencil)
    fullAspect |= vk::ImageAspectFlagBits::eStencil;

  // Create image
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
                  | vk::ImageUsageFlagBits::eSampled
                  | extraUsage;
  imageInfo.samples = samples;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_image = m_vkDevice.createImage(imageInfo);

  // Allocate and bind memory
  vk::MemoryRequirements memReqs = m_vkDevice.getImageMemoryRequirements(m_image);
  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = device.find_memory_type(
    memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_memory = m_vkDevice.allocateMemory(allocInfo);
  m_vkDevice.bindImageMemory(m_image, m_memory, 0);

  // Combined view (depth + stencil aspects)
  {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = m_image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = fullAspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    m_combinedView = m_vkDevice.createImageView(viewInfo);
  }

  // Depth-only view
  {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = m_image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    m_depthView = m_vkDevice.createImageView(viewInfo);
  }

  // Stencil-only view (if format has stencil)
  if (stencil)
  {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = m_image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    m_stencilView = m_vkDevice.createImageView(viewInfo);
  }

  spdlog::trace("Created DepthStencilAttachment {}x{} format={} stencil={}",
    extent.width, extent.height, vk::to_string(format), stencil);
}

DepthStencilAttachment::~DepthStencilAttachment()
{
  destroy();
}

DepthStencilAttachment::DepthStencilAttachment(DepthStencilAttachment&& other) noexcept
  : m_vkDevice(other.m_vkDevice),
    m_image(std::exchange(other.m_image, VK_NULL_HANDLE)),
    m_memory(std::exchange(other.m_memory, VK_NULL_HANDLE)),
    m_combinedView(std::exchange(other.m_combinedView, VK_NULL_HANDLE)),
    m_depthView(std::exchange(other.m_depthView, VK_NULL_HANDLE)),
    m_stencilView(std::exchange(other.m_stencilView, VK_NULL_HANDLE)),
    m_format(other.m_format),
    m_extent(other.m_extent)
{
}

DepthStencilAttachment& DepthStencilAttachment::operator=(DepthStencilAttachment&& other) noexcept
{
  if (this != &other)
  {
    destroy();
    m_vkDevice = other.m_vkDevice;
    m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
    m_memory = std::exchange(other.m_memory, VK_NULL_HANDLE);
    m_combinedView = std::exchange(other.m_combinedView, VK_NULL_HANDLE);
    m_depthView = std::exchange(other.m_depthView, VK_NULL_HANDLE);
    m_stencilView = std::exchange(other.m_stencilView, VK_NULL_HANDLE);
    m_format = other.m_format;
    m_extent = other.m_extent;
  }
  return *this;
}

bool DepthStencilAttachment::has_stencil() const
{
  return format_has_stencil(m_format);
}

void DepthStencilAttachment::destroy()
{
  if (!m_vkDevice)
    return;

  if (m_stencilView)
    m_vkDevice.destroyImageView(m_stencilView);
  if (m_depthView)
    m_vkDevice.destroyImageView(m_depthView);
  if (m_combinedView)
    m_vkDevice.destroyImageView(m_combinedView);
  if (m_image)
    m_vkDevice.destroyImage(m_image);
  if (m_memory)
    m_vkDevice.freeMemory(m_memory);

  m_stencilView = VK_NULL_HANDLE;
  m_depthView = VK_NULL_HANDLE;
  m_combinedView = VK_NULL_HANDLE;
  m_image = VK_NULL_HANDLE;
  m_memory = VK_NULL_HANDLE;
}

} // namespace vkwave
