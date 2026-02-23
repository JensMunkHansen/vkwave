#include <vkwave/core/buffer.h>
#include <vkwave/core/device.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <stdexcept>

namespace vkwave
{

Buffer::Buffer(const Device& device, const std::string& name, vk::DeviceSize size,
  vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
  : m_device(&device)
  , m_name(name)
  , m_size(size)
{
  // Create buffer
  vk::BufferCreateInfo buffer_info{};
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = vk::SharingMode::eExclusive;

  m_buffer = m_device->device().createBuffer(buffer_info);

  // Get memory requirements
  vk::MemoryRequirements mem_requirements =
    m_device->device().getBufferMemoryRequirements(m_buffer);

  // Allocate memory
  vk::MemoryAllocateInfo alloc_info{};
  alloc_info.allocationSize = mem_requirements.size;
  alloc_info.memoryTypeIndex =
    m_device->find_memory_type(mem_requirements.memoryTypeBits, properties);

  // If shader device address is requested, add the allocation flag
  vk::MemoryAllocateFlagsInfo alloc_flags_info{};
  if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)
  {
    alloc_flags_info.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
    alloc_info.pNext = &alloc_flags_info;
  }

  m_memory = m_device->device().allocateMemory(alloc_info);

  // Bind memory to buffer
  m_device->device().bindBufferMemory(m_buffer, m_memory, 0);

  // For host-visible memory, keep it persistently mapped
  if (properties & vk::MemoryPropertyFlagBits::eHostVisible)
  {
    m_persistent_mapping = true;
    map();
  }

  // Set debug name
  m_device->set_debug_name(
    reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_buffer)), vk::ObjectType::eBuffer, name);

  spdlog::trace("Created buffer '{}' ({} bytes)", name, size);
}

Buffer::~Buffer()
{
  if (m_device == nullptr)
  {
    return;
  }

  if (m_mapped_data != nullptr)
  {
    unmap();
  }

  if (m_buffer)
  {
    m_device->device().destroyBuffer(m_buffer);
    m_buffer = VK_NULL_HANDLE;
  }

  if (m_memory)
  {
    m_device->device().freeMemory(m_memory);
    m_memory = VK_NULL_HANDLE;
  }

  spdlog::trace("Destroyed buffer '{}'", m_name);
}

Buffer::Buffer(Buffer&& other) noexcept
  : m_device(other.m_device)
  , m_name(std::move(other.m_name))
  , m_buffer(other.m_buffer)
  , m_memory(other.m_memory)
  , m_size(other.m_size)
  , m_mapped_data(other.m_mapped_data)
  , m_persistent_mapping(other.m_persistent_mapping)
{
  other.m_device = nullptr;
  other.m_buffer = VK_NULL_HANDLE;
  other.m_memory = VK_NULL_HANDLE;
  other.m_size = 0;
  other.m_mapped_data = nullptr;
  other.m_persistent_mapping = false;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
  if (this != &other)
  {
    // Clean up existing resources
    if (m_device != nullptr)
    {
      if (m_mapped_data != nullptr)
      {
        unmap();
      }
      if (m_buffer)
      {
        m_device->device().destroyBuffer(m_buffer);
      }
      if (m_memory)
      {
        m_device->device().freeMemory(m_memory);
      }
    }

    // Move from other
    m_device = other.m_device;
    m_name = std::move(other.m_name);
    m_buffer = other.m_buffer;
    m_memory = other.m_memory;
    m_size = other.m_size;
    m_mapped_data = other.m_mapped_data;
    m_persistent_mapping = other.m_persistent_mapping;

    // Invalidate other
    other.m_device = nullptr;
    other.m_buffer = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_size = 0;
    other.m_mapped_data = nullptr;
    other.m_persistent_mapping = false;
  }
  return *this;
}

void Buffer::map()
{
  if (m_mapped_data != nullptr)
  {
    return; // Already mapped
  }

  m_mapped_data = m_device->device().mapMemory(m_memory, 0, m_size);
}

void Buffer::unmap()
{
  if (m_mapped_data == nullptr)
  {
    return; // Not mapped
  }

  m_device->device().unmapMemory(m_memory);
  m_mapped_data = nullptr;
}

void Buffer::update(const void* data, vk::DeviceSize size, vk::DeviceSize offset)
{
  if (size + offset > m_size)
  {
    throw std::runtime_error("Buffer update exceeds buffer size");
  }

  bool was_mapped = is_mapped();
  if (!was_mapped)
  {
    map();
  }

  std::memcpy(static_cast<char*>(m_mapped_data) + offset, data, size);

  if (!was_mapped && !m_persistent_mapping)
  {
    unmap();
  }
}

std::unique_ptr<Buffer> Buffer::create_device_local(
  const Device& device, const std::string& name,
  const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage)
{
  // Staging buffer: HOST_VISIBLE for CPU write, TRANSFER_SRC for copy
  Buffer staging(device, name + " staging", size,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  staging.update(data, size);

  // Device-local buffer: TRANSFER_DST for copy target + actual usage
  auto buffer = std::make_unique<Buffer>(device, name, size,
    usage | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal);

  // One-shot command buffer for the copy
  auto dev = device.device();

  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);

  vk::CommandBufferAllocateInfo alloc_info{};
  alloc_info.commandPool = cmd_pool;
  alloc_info.level = vk::CommandBufferLevel::ePrimary;
  alloc_info.commandBufferCount = 1;

  auto cmd = dev.allocateCommandBuffers(alloc_info)[0];

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd.begin(begin_info);

  vk::BufferCopy copy_region{};
  copy_region.size = size;
  cmd.copyBuffer(staging.buffer(), buffer->buffer(), copy_region);

  cmd.end();

  vk::SubmitInfo submit_info{};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd;

  device.graphics_queue().submit(submit_info, nullptr);
  device.graphics_queue().waitIdle();

  dev.freeCommandBuffers(cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  return buffer;
}

} // namespace vkwave
