#include <vkwave/pipeline/raytracing_pipeline.h>
#include <vkwave/core/device.h>
#include <vkwave/pipeline/shaders.h>

#include <spdlog/spdlog.h>

#include <fstream>
#include <cstring>

namespace vkwave
{

RayTracingPipeline::RayTracingPipeline(const Device& device)
  : m_device(&device)
{
}

RayTracingPipeline::~RayTracingPipeline()
{
  if (m_device == nullptr)
    return;

  auto dev = m_device->device();

  if (m_pipeline)
    dev.destroyPipeline(m_pipeline);
  if (m_layout)
    dev.destroyPipelineLayout(m_layout);
  if (m_sbt_buffer)
    dev.destroyBuffer(m_sbt_buffer);
  if (m_sbt_memory)
    dev.freeMemory(m_sbt_memory);
}

vk::ShaderModule RayTracingPipeline::create_shader_module(const std::string& path)
{
  return vkwave::createModule(path, m_device->device(), true);
}

void RayTracingPipeline::create(const std::string& raygen_path, const std::string& miss_path,
  const std::string& closesthit_path, vk::DescriptorSetLayout descriptor_set_layout,
  uint32_t vertex_stride_floats)
{
  auto dev = m_device->device();

  // Create shader modules
  vk::ShaderModule raygenModule = create_shader_module(raygen_path);
  vk::ShaderModule missModule = create_shader_module(miss_path);
  vk::ShaderModule chitModule = create_shader_module(closesthit_path);

  // Specialization constant for closesthit: vertex stride in floats
  vk::SpecializationMapEntry specEntry{};
  specEntry.constantID = 0;
  specEntry.offset = 0;
  specEntry.size = sizeof(uint32_t);

  vk::SpecializationInfo specInfo{};
  specInfo.mapEntryCount = 1;
  specInfo.pMapEntries = &specEntry;
  specInfo.dataSize = sizeof(uint32_t);
  specInfo.pData = &vertex_stride_floats;

  // Shader stages
  std::vector<vk::PipelineShaderStageCreateInfo> stages(3);

  stages[0].stage = vk::ShaderStageFlagBits::eRaygenKHR;
  stages[0].module = raygenModule;
  stages[0].pName = "main";

  stages[1].stage = vk::ShaderStageFlagBits::eMissKHR;
  stages[1].module = missModule;
  stages[1].pName = "main";

  stages[2].stage = vk::ShaderStageFlagBits::eClosestHitKHR;
  stages[2].module = chitModule;
  stages[2].pName = "main";
  stages[2].pSpecializationInfo = &specInfo;

  // Shader groups
  std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups(3);

  // Raygen group
  groups[0].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
  groups[0].generalShader = 0;
  groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
  groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
  groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

  // Miss group
  groups[1].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
  groups[1].generalShader = 1;
  groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
  groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
  groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

  // Closest hit group
  groups[2].type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
  groups[2].generalShader = VK_SHADER_UNUSED_KHR;
  groups[2].closestHitShader = 2;
  groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
  groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

  // Pipeline layout
  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &descriptor_set_layout;

  m_layout = dev.createPipelineLayout(layoutInfo);

  // Create ray tracing pipeline
  vk::RayTracingPipelineCreateInfoKHR pipelineInfo{};
  pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
  pipelineInfo.pStages = stages.data();
  pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
  pipelineInfo.pGroups = groups.data();
  pipelineInfo.maxPipelineRayRecursionDepth = 1;
  pipelineInfo.layout = m_layout;

  auto result = dev.createRayTracingPipelineKHR(nullptr, nullptr, pipelineInfo);
  if (result.result != vk::Result::eSuccess)
  {
    throw std::runtime_error("Failed to create ray tracing pipeline");
  }
  m_pipeline = result.value;

  // Cleanup shader modules
  dev.destroyShaderModule(raygenModule);
  dev.destroyShaderModule(missModule);
  dev.destroyShaderModule(chitModule);

  // Create shader binding table
  create_shader_binding_table();

  spdlog::trace("Created ray tracing pipeline");
}

void RayTracingPipeline::create_shader_binding_table()
{
  auto dev = m_device->device();
  const auto& rtCaps = m_device->ray_tracing_capabilities();

  uint32_t handleSize = rtCaps.shaderGroupHandleSize;
  uint32_t handleAlignment = rtCaps.shaderGroupHandleAlignment;
  uint32_t baseAlignment = rtCaps.shaderGroupBaseAlignment;

  // Align handle size
  uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

  // Calculate regions
  m_raygen_region.stride = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);
  m_raygen_region.size = m_raygen_region.stride; // Only one raygen shader

  m_miss_region.stride = handleSizeAligned;
  m_miss_region.size = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);

  m_hit_region.stride = handleSizeAligned;
  m_hit_region.size = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);

  m_callable_region.stride = 0;
  m_callable_region.size = 0;

  // Total SBT size
  vk::DeviceSize sbtSize = m_raygen_region.size + m_miss_region.size + m_hit_region.size;

  // Get shader group handles
  uint32_t groupCount = 3;
  std::vector<uint8_t> handleData(handleSize * groupCount);
  auto result = dev.getRayTracingShaderGroupHandlesKHR(
    m_pipeline, 0, groupCount, handleData.size(), handleData.data());
  if (result != vk::Result::eSuccess)
  {
    throw std::runtime_error("Failed to get ray tracing shader group handles");
  }

  // Create SBT buffer
  vk::BufferCreateInfo bufferInfo{};
  bufferInfo.size = sbtSize;
  bufferInfo.usage = vk::BufferUsageFlagBits::eShaderBindingTableKHR |
    vk::BufferUsageFlagBits::eShaderDeviceAddress;

  m_sbt_buffer = dev.createBuffer(bufferInfo);

  vk::MemoryRequirements memReqs = dev.getBufferMemoryRequirements(m_sbt_buffer);

  vk::MemoryAllocateFlagsInfo allocFlags{};
  allocFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.pNext = &allocFlags;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memReqs.memoryTypeBits,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

  m_sbt_memory = dev.allocateMemory(allocInfo);
  dev.bindBufferMemory(m_sbt_buffer, m_sbt_memory, 0);

  // Get buffer device address
  vk::BufferDeviceAddressInfo addressInfo{};
  addressInfo.buffer = m_sbt_buffer;
  vk::DeviceAddress sbtAddress = dev.getBufferAddress(addressInfo);

  m_raygen_region.deviceAddress = sbtAddress;
  m_miss_region.deviceAddress = sbtAddress + m_raygen_region.size;
  m_hit_region.deviceAddress = sbtAddress + m_raygen_region.size + m_miss_region.size;

  // Copy handles to SBT buffer
  void* data = dev.mapMemory(m_sbt_memory, 0, sbtSize);
  uint8_t* pData = static_cast<uint8_t*>(data);

  // Raygen
  std::memcpy(pData, handleData.data(), handleSize);
  pData += m_raygen_region.size;

  // Miss
  std::memcpy(pData, handleData.data() + handleSize, handleSize);
  pData += m_miss_region.size;

  // Hit
  std::memcpy(pData, handleData.data() + handleSize * 2, handleSize);

  dev.unmapMemory(m_sbt_memory);

  spdlog::trace("Created shader binding table: {} bytes", sbtSize);
}

void RayTracingPipeline::trace_rays(vk::CommandBuffer cmd, uint32_t width, uint32_t height)
{
  cmd.traceRaysKHR(
    m_raygen_region,
    m_miss_region,
    m_hit_region,
    m_callable_region,
    width, height, 1);
}

} // namespace vkwave
