#include <vkwave/pipeline/acceleration_structure.h>
#include <vkwave/core/device.h>
#include <vkwave/core/mesh.h>
#include <vkwave/core/vertex.h>

#include <spdlog/spdlog.h>

#include <cstring>

namespace vkwave
{

vk::DeviceAddress get_buffer_device_address(vk::Device device, vk::Buffer buffer)
{
  vk::BufferDeviceAddressInfo info{};
  info.buffer = buffer;
  return device.getBufferAddress(info);
}

AccelerationStructure::AccelerationStructure(const Device& device, const std::string& name)
  : m_device(&device)
  , m_name(name)
{
}

AccelerationStructure::~AccelerationStructure()
{
  cleanup();
}

void AccelerationStructure::cleanup()
{
  if (m_device == nullptr)
    return;

  auto dev = m_device->device();

  if (m_handle)
  {
    dev.destroyAccelerationStructureKHR(m_handle);
    m_handle = VK_NULL_HANDLE;
  }
  if (m_buffer)
  {
    dev.destroyBuffer(m_buffer);
    m_buffer = VK_NULL_HANDLE;
  }
  if (m_memory)
  {
    dev.freeMemory(m_memory);
    m_memory = VK_NULL_HANDLE;
  }
  if (m_scratch_buffer)
  {
    dev.destroyBuffer(m_scratch_buffer);
    m_scratch_buffer = VK_NULL_HANDLE;
  }
  if (m_scratch_memory)
  {
    dev.freeMemory(m_scratch_memory);
    m_scratch_memory = VK_NULL_HANDLE;
  }
  if (m_instance_buffer)
  {
    dev.destroyBuffer(m_instance_buffer);
    m_instance_buffer = VK_NULL_HANDLE;
  }
  if (m_instance_memory)
  {
    dev.freeMemory(m_instance_memory);
    m_instance_memory = VK_NULL_HANDLE;
  }
}

AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
  : m_device(other.m_device)
  , m_name(std::move(other.m_name))
  , m_handle(other.m_handle)
  , m_buffer(other.m_buffer)
  , m_memory(other.m_memory)
  , m_device_address(other.m_device_address)
  , m_scratch_buffer(other.m_scratch_buffer)
  , m_scratch_memory(other.m_scratch_memory)
  , m_instance_buffer(other.m_instance_buffer)
  , m_instance_memory(other.m_instance_memory)
{
  other.m_device = nullptr;
  other.m_handle = VK_NULL_HANDLE;
  other.m_buffer = VK_NULL_HANDLE;
  other.m_memory = VK_NULL_HANDLE;
  other.m_device_address = 0;
  other.m_scratch_buffer = VK_NULL_HANDLE;
  other.m_scratch_memory = VK_NULL_HANDLE;
  other.m_instance_buffer = VK_NULL_HANDLE;
  other.m_instance_memory = VK_NULL_HANDLE;
}

AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& other) noexcept
{
  if (this != &other)
  {
    cleanup();

    m_device = other.m_device;
    m_name = std::move(other.m_name);
    m_handle = other.m_handle;
    m_buffer = other.m_buffer;
    m_memory = other.m_memory;
    m_device_address = other.m_device_address;
    m_scratch_buffer = other.m_scratch_buffer;
    m_scratch_memory = other.m_scratch_memory;
    m_instance_buffer = other.m_instance_buffer;
    m_instance_memory = other.m_instance_memory;

    other.m_device = nullptr;
    other.m_handle = VK_NULL_HANDLE;
    other.m_buffer = VK_NULL_HANDLE;
    other.m_memory = VK_NULL_HANDLE;
    other.m_device_address = 0;
    other.m_scratch_buffer = VK_NULL_HANDLE;
    other.m_scratch_memory = VK_NULL_HANDLE;
    other.m_instance_buffer = VK_NULL_HANDLE;
    other.m_instance_memory = VK_NULL_HANDLE;
  }
  return *this;
}

void AccelerationStructure::create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage)
{
  auto dev = m_device->device();

  vk::BufferCreateInfo bufferInfo{};
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = vk::SharingMode::eExclusive;

  m_buffer = dev.createBuffer(bufferInfo);

  vk::MemoryRequirements memReqs = dev.getBufferMemoryRequirements(m_buffer);

  vk::MemoryAllocateFlagsInfo allocFlagsInfo{};
  allocFlagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.pNext = &allocFlagsInfo;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_memory = dev.allocateMemory(allocInfo);
  dev.bindBufferMemory(m_buffer, m_memory, 0);
}

void AccelerationStructure::build_blas(vk::CommandBuffer cmd, const Mesh& mesh)
{
  auto dev = m_device->device();

  // Geometry description
  vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
  triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
  triangles.vertexData.deviceAddress = get_buffer_device_address(dev, mesh.vertex_buffer());
  triangles.vertexStride = sizeof(Vertex);
  triangles.maxVertex = mesh.vertex_count() - 1;

  // Handle indexed vs non-indexed meshes
  uint32_t primitiveCount;
  if (mesh.is_indexed())
  {
    triangles.indexType = vk::IndexType::eUint32;
    triangles.indexData.deviceAddress = get_buffer_device_address(dev, mesh.index_buffer());
    primitiveCount = mesh.index_count() / 3;
  }
  else
  {
    triangles.indexType = vk::IndexType::eNoneKHR;
    triangles.indexData.deviceAddress = 0;
    primitiveCount = mesh.vertex_count() / 3;
  }

  vk::AccelerationStructureGeometryKHR geometry{};
  geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
  geometry.geometry.triangles = triangles;
  geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

  // Build info
  vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
  buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
  buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
  buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
  buildInfo.geometryCount = 1;
  buildInfo.pGeometries = &geometry;

  // Get size requirements

  vk::AccelerationStructureBuildSizesInfoKHR sizeInfo =
    dev.getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, primitiveCount);

  // Create acceleration structure buffer
  create_buffer(sizeInfo.accelerationStructureSize,
    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
    vk::BufferUsageFlagBits::eShaderDeviceAddress);

  // Create acceleration structure
  vk::AccelerationStructureCreateInfoKHR createInfo{};
  createInfo.buffer = m_buffer;
  createInfo.size = sizeInfo.accelerationStructureSize;
  createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;

  m_handle = dev.createAccelerationStructureKHR(createInfo);

  // Get device address
  vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
  addressInfo.accelerationStructure = m_handle;
  m_device_address = dev.getAccelerationStructureAddressKHR(addressInfo);

  // Create scratch buffer
  vk::BufferCreateInfo scratchBufferInfo{};
  scratchBufferInfo.size = sizeInfo.buildScratchSize;
  scratchBufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eShaderDeviceAddress;

  m_scratch_buffer = dev.createBuffer(scratchBufferInfo);

  vk::MemoryRequirements scratchMemReqs = dev.getBufferMemoryRequirements(m_scratch_buffer);

  vk::MemoryAllocateFlagsInfo scratchAllocFlags{};
  scratchAllocFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

  vk::MemoryAllocateInfo scratchAllocInfo{};
  scratchAllocInfo.pNext = &scratchAllocFlags;
  scratchAllocInfo.allocationSize = scratchMemReqs.size;
  scratchAllocInfo.memoryTypeIndex = m_device->find_memory_type(
    scratchMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_scratch_memory = dev.allocateMemory(scratchAllocInfo);
  dev.bindBufferMemory(m_scratch_buffer, m_scratch_memory, 0);

  vk::DeviceAddress scratchAddress = get_buffer_device_address(dev, m_scratch_buffer);

  // Build
  buildInfo.dstAccelerationStructure = m_handle;
  buildInfo.scratchData.deviceAddress = scratchAddress;

  vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
  rangeInfo.primitiveCount = primitiveCount;
  rangeInfo.primitiveOffset = 0;
  rangeInfo.firstVertex = 0;
  rangeInfo.transformOffset = 0;

  const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

  cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

  // Memory barrier
  vk::MemoryBarrier barrier{};
  barrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
  barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR;

  cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
    {}, barrier, {}, {});

  spdlog::trace("Built BLAS '{}': {} triangles", m_name, primitiveCount);
}

void AccelerationStructure::build_tlas(vk::CommandBuffer cmd,
  const std::vector<std::pair<const AccelerationStructure*, glm::mat4>>& instances)
{
  auto dev = m_device->device();

  // Create instance buffer
  std::vector<vk::AccelerationStructureInstanceKHR> asInstances;
  asInstances.reserve(instances.size());

  for (size_t i = 0; i < instances.size(); ++i)
  {
    const auto& [blas, transform] = instances[i];

    vk::AccelerationStructureInstanceKHR instance{};

    // Copy transform (row-major 3x4)
    auto& t = instance.transform.matrix;
    t[0][0] = transform[0][0]; t[0][1] = transform[1][0]; t[0][2] = transform[2][0]; t[0][3] = transform[3][0];
    t[1][0] = transform[0][1]; t[1][1] = transform[1][1]; t[1][2] = transform[2][1]; t[1][3] = transform[3][1];
    t[2][0] = transform[0][2]; t[2][1] = transform[1][2]; t[2][2] = transform[2][2]; t[2][3] = transform[3][2];

    instance.instanceCustomIndex = static_cast<uint32_t>(i);
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = blas->device_address();

    asInstances.push_back(instance);
  }

  // Create instance buffer
  vk::DeviceSize instanceBufferSize = sizeof(vk::AccelerationStructureInstanceKHR) * asInstances.size();

  vk::BufferCreateInfo instanceBufferInfo{};
  instanceBufferInfo.size = instanceBufferSize;
  instanceBufferInfo.usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
    vk::BufferUsageFlagBits::eShaderDeviceAddress;

  // Clean up any previous instance buffer
  if (m_instance_buffer)
  {
    dev.destroyBuffer(m_instance_buffer);
    m_instance_buffer = VK_NULL_HANDLE;
  }
  if (m_instance_memory)
  {
    dev.freeMemory(m_instance_memory);
    m_instance_memory = VK_NULL_HANDLE;
  }

  m_instance_buffer = dev.createBuffer(instanceBufferInfo);

  vk::MemoryRequirements instanceMemReqs = dev.getBufferMemoryRequirements(m_instance_buffer);

  vk::MemoryAllocateFlagsInfo instanceAllocFlags{};
  instanceAllocFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

  vk::MemoryAllocateInfo instanceAllocInfo{};
  instanceAllocInfo.pNext = &instanceAllocFlags;
  instanceAllocInfo.allocationSize = instanceMemReqs.size;
  instanceAllocInfo.memoryTypeIndex = m_device->find_memory_type(
    instanceMemReqs.memoryTypeBits,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

  m_instance_memory = dev.allocateMemory(instanceAllocInfo);
  dev.bindBufferMemory(m_instance_buffer, m_instance_memory, 0);

  // Copy instance data
  void* data = dev.mapMemory(m_instance_memory, 0, instanceBufferSize);
  std::memcpy(data, asInstances.data(), instanceBufferSize);
  dev.unmapMemory(m_instance_memory);

  vk::DeviceAddress instanceAddress = get_buffer_device_address(dev, m_instance_buffer);

  // Geometry description
  vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
  instancesData.arrayOfPointers = VK_FALSE;
  instancesData.data.deviceAddress = instanceAddress;

  vk::AccelerationStructureGeometryKHR geometry{};
  geometry.geometryType = vk::GeometryTypeKHR::eInstances;
  geometry.geometry.instances = instancesData;

  // Build info
  vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
  buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
  buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
  buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
  buildInfo.geometryCount = 1;
  buildInfo.pGeometries = &geometry;

  uint32_t instanceCount = static_cast<uint32_t>(instances.size());

  vk::AccelerationStructureBuildSizesInfoKHR sizeInfo =
    dev.getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, instanceCount);

  // Create acceleration structure buffer
  create_buffer(sizeInfo.accelerationStructureSize,
    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
    vk::BufferUsageFlagBits::eShaderDeviceAddress);

  // Create acceleration structure
  vk::AccelerationStructureCreateInfoKHR createInfo{};
  createInfo.buffer = m_buffer;
  createInfo.size = sizeInfo.accelerationStructureSize;
  createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;

  m_handle = dev.createAccelerationStructureKHR(createInfo);

  // Get device address
  vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
  addressInfo.accelerationStructure = m_handle;
  m_device_address = dev.getAccelerationStructureAddressKHR(addressInfo);

  // Create scratch buffer
  vk::BufferCreateInfo scratchBufferInfo{};
  scratchBufferInfo.size = sizeInfo.buildScratchSize;
  scratchBufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eShaderDeviceAddress;

  m_scratch_buffer = dev.createBuffer(scratchBufferInfo);

  vk::MemoryRequirements scratchMemReqs = dev.getBufferMemoryRequirements(m_scratch_buffer);

  vk::MemoryAllocateFlagsInfo scratchAllocFlags{};
  scratchAllocFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

  vk::MemoryAllocateInfo scratchAllocInfo{};
  scratchAllocInfo.pNext = &scratchAllocFlags;
  scratchAllocInfo.allocationSize = scratchMemReqs.size;
  scratchAllocInfo.memoryTypeIndex = m_device->find_memory_type(
    scratchMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_scratch_memory = dev.allocateMemory(scratchAllocInfo);
  dev.bindBufferMemory(m_scratch_buffer, m_scratch_memory, 0);

  vk::DeviceAddress scratchAddress = get_buffer_device_address(dev, m_scratch_buffer);

  // Build
  buildInfo.dstAccelerationStructure = m_handle;
  buildInfo.scratchData.deviceAddress = scratchAddress;

  vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
  rangeInfo.primitiveCount = instanceCount;

  const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

  cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

  // Note: Instance buffer must persist until command buffer execution completes
  // It will be cleaned up in cleanup() or when build_tlas() is called again

  spdlog::trace("Built TLAS '{}': {} instances", m_name, instanceCount);
}

} // namespace vkwave
