#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <vkwave/core/texture.h>
#include <vkwave/core/buffer.h>
#include <vkwave/core/device.h>

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace vkwave
{

Texture::Texture(const Device& device, const std::string& name, const uint8_t* pixels,
  uint32_t width, uint32_t height, bool linear)
  : m_device(&device)
  , m_name(name)
  , m_width(width)
  , m_height(height)
  , m_format(linear ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb)
{
  create_image();
  create_image_view();
  create_sampler();
  upload_pixels(pixels);

  spdlog::trace("Created texture '{}' ({}x{})", name, width, height);
}

Texture::Texture(const Device& device, const std::string& name, const std::string& filepath,
  bool linear)
  : m_device(&device)
  , m_name(name)
  , m_format(linear ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb)
{
  // Load image with stb_image
  int width, height, channels;
  stbi_uc* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

  if (!pixels)
  {
    throw std::runtime_error("Failed to load texture: " + filepath);
  }

  m_width = static_cast<uint32_t>(width);
  m_height = static_cast<uint32_t>(height);

  create_image();
  create_image_view();
  create_sampler();
  upload_pixels(pixels);

  stbi_image_free(pixels);

  spdlog::trace("Created texture '{}' from {} ({}x{})", name, filepath, m_width, m_height);
}

Texture::~Texture()
{
  if (m_device == nullptr)
  {
    return;
  }

  auto dev = m_device->device();

  if (m_sampler)
  {
    dev.destroySampler(m_sampler);
    m_sampler = VK_NULL_HANDLE;
  }

  if (m_image_view)
  {
    dev.destroyImageView(m_image_view);
    m_image_view = VK_NULL_HANDLE;
  }

  if (m_image)
  {
    dev.destroyImage(m_image);
    m_image = VK_NULL_HANDLE;
  }

  if (m_memory)
  {
    dev.freeMemory(m_memory);
    m_memory = VK_NULL_HANDLE;
  }

  spdlog::trace("Destroyed texture '{}'", m_name);
}

Texture::Texture(Texture&& other) noexcept
  : m_device(other.m_device)
  , m_name(std::move(other.m_name))
  , m_image(other.m_image)
  , m_memory(other.m_memory)
  , m_image_view(other.m_image_view)
  , m_sampler(other.m_sampler)
  , m_width(other.m_width)
  , m_height(other.m_height)
  , m_format(other.m_format)
{
  other.m_device = nullptr;
  other.m_image = VK_NULL_HANDLE;
  other.m_memory = VK_NULL_HANDLE;
  other.m_image_view = VK_NULL_HANDLE;
  other.m_sampler = VK_NULL_HANDLE;
  other.m_width = 0;
  other.m_height = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept
{
  if (this != &other)
  {
    // Clean up existing resources
    if (m_device != nullptr)
    {
      auto dev = m_device->device();
      if (m_sampler)
        dev.destroySampler(m_sampler);
      if (m_image_view)
        dev.destroyImageView(m_image_view);
      if (m_image)
        dev.destroyImage(m_image);
      if (m_memory)
        dev.freeMemory(m_memory);
    }

    // Move from other
    m_device = other.m_device;
    m_name = std::move(other.m_name);
    m_image = other.m_image;
    m_memory = other.m_memory;
    m_image_view = other.m_image_view;
    m_sampler = other.m_sampler;
    m_width = other.m_width;
    m_height = other.m_height;
    m_format = other.m_format;

    // Invalidate other
    other.m_device = nullptr;
    other.m_image = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_image_view = VK_NULL_HANDLE;
    other.m_sampler = VK_NULL_HANDLE;
    other.m_width = 0;
    other.m_height = 0;
  }
  return *this;
}

void Texture::create_image()
{
  auto dev = m_device->device();

  // Create image
  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.extent.width = m_width;
  image_info.extent.height = m_height;
  image_info.extent.depth = 1;
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.format = m_format;
  image_info.tiling = vk::ImageTiling::eOptimal;
  image_info.initialLayout = vk::ImageLayout::eUndefined;
  image_info.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.samples = vk::SampleCountFlagBits::e1;

  m_image = dev.createImage(image_info);

  // Allocate memory
  vk::MemoryRequirements mem_reqs = dev.getImageMemoryRequirements(m_image);

  vk::MemoryAllocateInfo alloc_info{};
  alloc_info.allocationSize = mem_reqs.size;
  alloc_info.memoryTypeIndex =
    m_device->find_memory_type(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_memory = dev.allocateMemory(alloc_info);
  dev.bindImageMemory(m_image, m_memory, 0);

  // Set debug name
  m_device->set_debug_name(
    reinterpret_cast<uint64_t>(static_cast<VkImage>(m_image)), vk::ObjectType::eImage, m_name);
}

void Texture::create_image_view()
{
  vk::ImageViewCreateInfo view_info{};
  view_info.image = m_image;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.format = m_format;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;

  m_device->create_image_view(view_info, &m_image_view, m_name + " view");
}

void Texture::create_sampler()
{
  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
  sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
  sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
  sampler_info.anisotropyEnable = VK_FALSE;
  sampler_info.maxAnisotropy = 1.0f;
  sampler_info.borderColor = vk::BorderColor::eIntOpaqueBlack;
  sampler_info.unnormalizedCoordinates = VK_FALSE;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = vk::CompareOp::eAlways;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;

  m_sampler = m_device->device().createSampler(sampler_info);

  m_device->set_debug_name(reinterpret_cast<uint64_t>(static_cast<VkSampler>(m_sampler)),
    vk::ObjectType::eSampler, m_name + " sampler");
}

void Texture::transition_layout(
  vk::CommandBuffer cmd, vk::ImageLayout old_layout, vk::ImageLayout new_layout)
{
  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_image;
  barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  vk::PipelineStageFlags src_stage;
  vk::PipelineStageFlags dst_stage;

  if (old_layout == vk::ImageLayout::eUndefined &&
      new_layout == vk::ImageLayout::eTransferDstOptimal)
  {
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
    dst_stage = vk::PipelineStageFlagBits::eTransfer;
  }
  else if (old_layout == vk::ImageLayout::eTransferDstOptimal &&
           new_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
  {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    src_stage = vk::PipelineStageFlagBits::eTransfer;
    dst_stage = vk::PipelineStageFlagBits::eFragmentShader;
  }
  else
  {
    throw std::runtime_error("Unsupported layout transition");
  }

  cmd.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
}

void Texture::upload_pixels(const uint8_t* pixels)
{
  auto dev = m_device->device();
  vk::DeviceSize image_size = m_width * m_height * 4; // RGBA

  // Create staging buffer
  Buffer staging(*m_device, m_name + " staging", image_size,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

  staging.update(pixels, image_size);

  // Create command buffer for transfer
  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device->m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;

  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);

  vk::CommandBufferAllocateInfo alloc_info{};
  alloc_info.commandPool = cmd_pool;
  alloc_info.level = vk::CommandBufferLevel::ePrimary;
  alloc_info.commandBufferCount = 1;

  auto cmd_buffers = dev.allocateCommandBuffers(alloc_info);
  vk::CommandBuffer cmd = cmd_buffers[0];

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd.begin(begin_info);

  // Transition to transfer destination
  transition_layout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

  // Copy buffer to image
  vk::BufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = vk::Offset3D{ 0, 0, 0 };
  region.imageExtent = vk::Extent3D{ m_width, m_height, 1 };

  cmd.copyBufferToImage(staging.buffer(), m_image, vk::ImageLayout::eTransferDstOptimal, region);

  // Transition to shader read
  transition_layout(
    cmd, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

  cmd.end();

  // Submit and wait
  vk::SubmitInfo submit_info{};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd;

  m_device->graphics_queue().submit(submit_info, nullptr);
  m_device->graphics_queue().waitIdle();

  // Cleanup
  dev.freeCommandBuffers(cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);
}

} // namespace vkwave
