#include <vkwave/loaders/ibl.h>
#include <vkwave/core/buffer.h>
#include <vkwave/config.h>
#include <vkwave/core/device.h>
#include <vkwave/pipeline/shader_compiler.h>
#include <vkwave/pipeline/shaders.h>

#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <cmath>
#include <stdexcept>

namespace vkwave
{

namespace
{

// Helper to transition image layout
void transition_image_layout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout old_layout,
  vk::ImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count,
  vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage,
  vk::AccessFlags src_access, vk::AccessFlags dst_access,
  vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
{
  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspect;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mip_levels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = layer_count;
  barrier.srcAccessMask = src_access;
  barrier.dstAccessMask = dst_access;

  cmd.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
}

// Overload for single mip level barrier
void transition_mip_layout(vk::CommandBuffer cmd, vk::Image image,
  vk::ImageLayout old_layout, vk::ImageLayout new_layout,
  uint32_t mip_level, uint32_t layer_count,
  vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage,
  vk::AccessFlags src_access, vk::AccessFlags dst_access)
{
  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barrier.subresourceRange.baseMipLevel = mip_level;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = layer_count;
  barrier.srcAccessMask = src_access;
  barrier.dstAccessMask = dst_access;

  cmd.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
}

// Create command buffer, begin recording
vk::CommandBuffer begin_single_time_commands(const Device& device, vk::CommandPool pool)
{
  vk::CommandBufferAllocateInfo alloc_info{};
  alloc_info.commandPool = pool;
  alloc_info.level = vk::CommandBufferLevel::ePrimary;
  alloc_info.commandBufferCount = 1;

  auto cmd_buffers = device.device().allocateCommandBuffers(alloc_info);
  vk::CommandBuffer cmd = cmd_buffers[0];

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd.begin(begin_info);

  return cmd;
}

// End recording, submit and wait
void end_single_time_commands(
  const Device& device, vk::CommandPool pool, vk::CommandBuffer cmd)
{
  cmd.end();

  vk::SubmitInfo submit_info{};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd;

  device.graphics_queue().submit(submit_info, nullptr);
  device.graphics_queue().waitIdle();

  device.device().freeCommandBuffers(pool, cmd);
}

// Create a GPU image with memory
void create_image(const Device& device, vk::Image& image, vk::DeviceMemory& memory,
  uint32_t width, uint32_t height, uint32_t mip_levels, uint32_t array_layers,
  vk::Format format, vk::ImageUsageFlags usage, vk::ImageCreateFlags flags = {})
{
  auto dev = device.device();

  vk::ImageCreateInfo info{};
  info.imageType = vk::ImageType::e2D;
  info.format = format;
  info.extent = vk::Extent3D{ width, height, 1 };
  info.mipLevels = mip_levels;
  info.arrayLayers = array_layers;
  info.samples = vk::SampleCountFlagBits::e1;
  info.tiling = vk::ImageTiling::eOptimal;
  info.usage = usage;
  info.sharingMode = vk::SharingMode::eExclusive;
  info.initialLayout = vk::ImageLayout::eUndefined;
  info.flags = flags;

  image = dev.createImage(info);

  auto mem_reqs = dev.getImageMemoryRequirements(image);
  vk::MemoryAllocateInfo alloc{};
  alloc.allocationSize = mem_reqs.size;
  alloc.memoryTypeIndex =
    device.find_memory_type(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  memory = dev.allocateMemory(alloc);
  dev.bindImageMemory(image, memory, 0);
}

// Helper struct for compute pipeline + layout + descriptor set layout
struct ComputePipeline
{
  vk::Pipeline pipeline;
  vk::PipelineLayout layout;
  vk::DescriptorSetLayout desc_layout;
};

ComputePipeline create_compute_pipeline(vk::Device dev, const std::string& shader_path,
  std::vector<vk::DescriptorSetLayoutBinding> bindings, uint32_t push_constant_size)
{
  ComputePipeline result{};

  // Descriptor set layout
  vk::DescriptorSetLayoutCreateInfo dsl_ci{};
  dsl_ci.bindingCount = static_cast<uint32_t>(bindings.size());
  dsl_ci.pBindings = bindings.data();
  result.desc_layout = dev.createDescriptorSetLayout(dsl_ci);

  // Pipeline layout
  vk::PushConstantRange push_range{};
  push_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
  push_range.offset = 0;
  push_range.size = push_constant_size;

  vk::PipelineLayoutCreateInfo pl_ci{};
  pl_ci.setLayoutCount = 1;
  pl_ci.pSetLayouts = &result.desc_layout;
  pl_ci.pushConstantRangeCount = 1;
  pl_ci.pPushConstantRanges = &push_range;
  result.layout = dev.createPipelineLayout(pl_ci);

  // Compile from GLSL source and create shader module
  auto compiled = ShaderCompiler::compile(shader_path, vk::ShaderStageFlagBits::eCompute);
  auto module = ShaderCompiler::create_module(dev, compiled.spirv);

  vk::PipelineShaderStageCreateInfo stage{};
  stage.stage = vk::ShaderStageFlagBits::eCompute;
  stage.module = module;
  stage.pName = "main";

  vk::ComputePipelineCreateInfo ci{};
  ci.stage = stage;
  ci.layout = result.layout;

  result.pipeline = dev.createComputePipeline(nullptr, ci).value;

  dev.destroyShaderModule(module);

  return result;
}

void destroy_compute_pipeline(vk::Device dev, ComputePipeline& cp)
{
  dev.destroyPipeline(cp.pipeline);
  dev.destroyPipelineLayout(cp.layout);
  dev.destroyDescriptorSetLayout(cp.desc_layout);
}

} // namespace

IBL::IBL(const Device& device)
  : m_device(device)
  , m_resolution(64)
  , m_mip_levels(1)
{
  spdlog::info("Creating default neutral IBL environment");
  create_default_environment();
}

IBL::IBL(const Device& device, const std::string& hdr_path, const IBLSettings& settings)
  : m_device(device)
  , m_settings(settings)
  , m_resolution(settings.resolution)
  , m_mip_levels(static_cast<uint32_t>(std::floor(std::log2(settings.resolution))) + 1)
{
  spdlog::info("Creating IBL from HDR: {} (resolution: {}, mips: {}, samples: irr={}, pf={}, brdf={})",
    hdr_path, m_resolution, m_mip_levels,
    m_settings.irradiance_samples, m_settings.prefilter_samples, m_settings.brdf_samples);

  load_hdr_environment(hdr_path);
  upload_hdr_to_gpu();
  create_ibl_images();
  run_compute_generation();

  // Cleanup CPU data and HDR GPU texture
  m_hdr_data.clear();
  m_hdr_data.shrink_to_fit();

  auto dev = m_device.device();
  if (m_hdr_sampler)
    dev.destroySampler(m_hdr_sampler);
  if (m_hdr_view)
    dev.destroyImageView(m_hdr_view);
  if (m_hdr_image)
    dev.destroyImage(m_hdr_image);
  if (m_hdr_memory)
    dev.freeMemory(m_hdr_memory);
  m_hdr_sampler = VK_NULL_HANDLE;
  m_hdr_view = VK_NULL_HANDLE;
  m_hdr_image = VK_NULL_HANDLE;
  m_hdr_memory = VK_NULL_HANDLE;
}

IBL::~IBL()
{
  auto dev = m_device.device();

  // BRDF LUT cleanup
  if (m_brdf_lut_sampler)
    dev.destroySampler(m_brdf_lut_sampler);
  if (m_brdf_lut_view)
    dev.destroyImageView(m_brdf_lut_view);
  if (m_brdf_lut_image)
    dev.destroyImage(m_brdf_lut_image);
  if (m_brdf_lut_memory)
    dev.freeMemory(m_brdf_lut_memory);

  // Irradiance cleanup
  if (m_irradiance_sampler)
    dev.destroySampler(m_irradiance_sampler);
  if (m_irradiance_view)
    dev.destroyImageView(m_irradiance_view);
  if (m_irradiance_image)
    dev.destroyImage(m_irradiance_image);
  if (m_irradiance_memory)
    dev.freeMemory(m_irradiance_memory);

  // Pre-filtered cleanup
  if (m_prefiltered_sampler)
    dev.destroySampler(m_prefiltered_sampler);
  if (m_prefiltered_view)
    dev.destroyImageView(m_prefiltered_view);
  if (m_prefiltered_image)
    dev.destroyImage(m_prefiltered_image);
  if (m_prefiltered_memory)
    dev.freeMemory(m_prefiltered_memory);

  // HDR source cleanup (may already be freed)
  if (m_hdr_sampler)
    dev.destroySampler(m_hdr_sampler);
  if (m_hdr_view)
    dev.destroyImageView(m_hdr_view);
  if (m_hdr_image)
    dev.destroyImage(m_hdr_image);
  if (m_hdr_memory)
    dev.freeMemory(m_hdr_memory);

  spdlog::trace("IBL resources destroyed");
}

void IBL::load_hdr_environment(const std::string& hdr_path)
{
  int width, height, channels;
  float* hdr_data = stbi_loadf(hdr_path.c_str(), &width, &height, &channels, 4);

  if (!hdr_data)
  {
    throw std::runtime_error("Failed to load HDR environment: " + hdr_path);
  }

  spdlog::info("Loaded HDR: {}x{} (channels: {})", width, height, channels);

  m_hdr_width = static_cast<uint32_t>(width);
  m_hdr_height = static_cast<uint32_t>(height);
  m_hdr_data.resize(width * height * 4);
  std::memcpy(m_hdr_data.data(), hdr_data, width * height * 4 * sizeof(float));

  stbi_image_free(hdr_data);
}

void IBL::upload_hdr_to_gpu()
{
  auto dev = m_device.device();

  // Create HDR GPU texture (R32G32B32A32Sfloat, sampled)
  create_image(m_device, m_hdr_image, m_hdr_memory,
    m_hdr_width, m_hdr_height, 1, 1,
    vk::Format::eR32G32B32A32Sfloat,
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);

  // Upload via staging buffer
  vk::DeviceSize data_size = m_hdr_width * m_hdr_height * 4 * sizeof(float);
  Buffer staging(m_device, "HDR staging", data_size, vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  staging.update(m_hdr_data.data(), data_size);

  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);

  auto cmd = begin_single_time_commands(m_device, cmd_pool);

  transition_image_layout(cmd, m_hdr_image,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1, 1,
    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
    {}, vk::AccessFlagBits::eTransferWrite);

  vk::BufferImageCopy region{};
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = vk::Extent3D{ m_hdr_width, m_hdr_height, 1 };

  cmd.copyBufferToImage(staging.buffer(), m_hdr_image,
    vk::ImageLayout::eTransferDstOptimal, region);

  transition_image_layout(cmd, m_hdr_image,
    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 1,
    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
    vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);

  end_single_time_commands(m_device, cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  // Create image view
  vk::ImageViewCreateInfo view_info{};
  view_info.image = m_hdr_image;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.format = vk::Format::eR32G32B32A32Sfloat;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;

  m_device.create_image_view(view_info, &m_hdr_view, "HDR equirect view");

  // Create sampler
  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.maxLod = 1.0f;

  m_hdr_sampler = dev.createSampler(sampler_info);

  spdlog::info("Uploaded HDR to GPU ({}x{})", m_hdr_width, m_hdr_height);
}

void IBL::create_ibl_images()
{
  auto dev = m_device.device();

  constexpr uint32_t IRR_SIZE = 32;
  constexpr uint32_t LUT_SIZE = 128;
  constexpr float MAX_REFLECTION_LOD = 4.0f;

  // --- Environment cubemap (storage write + sampled read + transfer for mip gen) ---
  create_image(m_device, m_prefiltered_image, m_prefiltered_memory,
    m_resolution, m_resolution, m_mip_levels, 6,
    vk::Format::eR32G32B32A32Sfloat,
    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
    vk::ImageCreateFlagBits::eCubeCompatible);

  // --- Irradiance cubemap ---
  create_image(m_device, m_irradiance_image, m_irradiance_memory,
    IRR_SIZE, IRR_SIZE, 1, 6,
    vk::Format::eR32G32B32A32Sfloat,
    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
    vk::ImageCreateFlagBits::eCubeCompatible);

  // --- BRDF LUT ---
  create_image(m_device, m_brdf_lut_image, m_brdf_lut_memory,
    LUT_SIZE, LUT_SIZE, 1, 1,
    vk::Format::eR8G8B8A8Unorm,
    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);

  // Create views

  // Prefiltered cubemap view (all mips)
  vk::ImageViewCreateInfo view_info{};
  view_info.image = m_prefiltered_image;
  view_info.viewType = vk::ImageViewType::eCube;
  view_info.format = vk::Format::eR32G32B32A32Sfloat;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = m_mip_levels;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 6;
  m_device.create_image_view(view_info, &m_prefiltered_view, "Prefiltered cubemap view");

  // Irradiance cubemap view
  view_info.image = m_irradiance_image;
  view_info.subresourceRange.levelCount = 1;
  m_device.create_image_view(view_info, &m_irradiance_view, "Irradiance cubemap view");

  // BRDF LUT view
  vk::ImageViewCreateInfo lut_view_info{};
  lut_view_info.image = m_brdf_lut_image;
  lut_view_info.viewType = vk::ImageViewType::e2D;
  lut_view_info.format = vk::Format::eR8G8B8A8Unorm;
  lut_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  lut_view_info.subresourceRange.baseMipLevel = 0;
  lut_view_info.subresourceRange.levelCount = 1;
  lut_view_info.subresourceRange.baseArrayLayer = 0;
  lut_view_info.subresourceRange.layerCount = 1;
  m_device.create_image_view(lut_view_info, &m_brdf_lut_view, "BRDF LUT view");

  // Create samplers
  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.maxLod = static_cast<float>(m_mip_levels);

  m_prefiltered_sampler = dev.createSampler(sampler_info);

  sampler_info.maxLod = 1.0f;
  m_irradiance_sampler = dev.createSampler(sampler_info);
  m_brdf_lut_sampler = dev.createSampler(sampler_info);

  spdlog::info("Created IBL images (cubemap {}x{} {} mips, irradiance {}x{}, BRDF LUT {}x{})",
    m_resolution, m_resolution, m_mip_levels, IRR_SIZE, IRR_SIZE, LUT_SIZE, LUT_SIZE);
}

void IBL::run_compute_generation()
{
  auto dev = m_device.device();

  constexpr uint32_t IRR_SIZE = 32;
  constexpr uint32_t LUT_SIZE = 128;
  const uint32_t PREFILTER_SAMPLES = m_settings.prefilter_samples;
  const uint32_t IRR_SAMPLES = m_settings.irradiance_samples;
  const uint32_t BRDF_SAMPLES = m_settings.brdf_samples;
  constexpr float MAX_REFLECTION_LOD = 4.0f;

  // --- Create descriptor pool ---
  // Sets: equirect(1) + irradiance(1) + brdf(1) + prefilter per-mip(m_mip_levels-1)
  uint32_t prefilter_mip_count = m_mip_levels - 1; // mips 1..N
  uint32_t total_sets = 3 + prefilter_mip_count;
  // Combined image samplers: equirect(1) + irradiance(1) + prefilter per-mip(prefilter_mip_count)
  uint32_t total_samplers = 2 + prefilter_mip_count;
  // Storage images: equirect(1) + irradiance(1) + prefilter per-mip(prefilter_mip_count) + brdf(1)
  uint32_t total_storage = 3 + prefilter_mip_count;

  std::array<vk::DescriptorPoolSize, 2> pool_sizes = {
    vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, total_samplers },
    vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage, total_storage }
  };

  vk::DescriptorPoolCreateInfo pool_ci{};
  pool_ci.maxSets = total_sets;
  pool_ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
  pool_ci.pPoolSizes = pool_sizes.data();
  auto desc_pool = dev.createDescriptorPool(pool_ci);

  // --- Create compute pipelines ---

  // 1. Equirect to cubemap: sampler2D + imageCube
  auto equirect_pipeline = create_compute_pipeline(dev, SHADER_DIR "equirect_to_cubemap.comp",
    {
      { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute },
      { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }
    },
    8); // face(4) + resolution(4)

  // 2. Irradiance: samplerCube + imageCube
  auto irradiance_pipeline = create_compute_pipeline(dev, SHADER_DIR "irradiance.comp",
    {
      { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute },
      { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }
    },
    16); // face(4) + resolution(4) + sampleCount(4) + envResolution(4)

  // 3. Prefilter: samplerCube + imageCube
  auto prefilter_pipeline = create_compute_pipeline(dev, SHADER_DIR "prefilter_env.comp",
    {
      { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute },
      { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }
    },
    20); // face(4) + resolution(4) + roughness(4) + sampleCount(4) + envResolution(4)

  // 4. BRDF LUT: image2D only
  auto brdf_pipeline = create_compute_pipeline(dev, SHADER_DIR "brdf_lut.comp",
    {
      { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }
    },
    8); // resolution(4) + sampleCount(4)

  // --- Allocate descriptor sets ---
  // 3 base sets + per-mip prefilter sets
  std::vector<vk::DescriptorSetLayout> layouts;
  layouts.push_back(equirect_pipeline.desc_layout);   // [0] equirect
  layouts.push_back(irradiance_pipeline.desc_layout);  // [1] irradiance
  layouts.push_back(brdf_pipeline.desc_layout);        // [2] brdf
  for (uint32_t mip = 1; mip < m_mip_levels; ++mip)
    layouts.push_back(prefilter_pipeline.desc_layout); // [3..] prefilter per-mip

  vk::DescriptorSetAllocateInfo ds_alloc{};
  ds_alloc.descriptorPool = desc_pool;
  ds_alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
  ds_alloc.pSetLayouts = layouts.data();
  auto desc_sets = dev.allocateDescriptorSets(ds_alloc);
  // desc_sets[0] = equirect, [1] = irradiance, [2] = brdf, [3+mip-1] = prefilter mip

  // We need per-mip image views for the prefiltered cubemap storage writes
  // For equirect_to_cubemap, we write to mip 0 of the prefiltered cubemap
  // For prefilter_env, we need a separate view per mip level

  // Create mip 0 storage view for cubemap (equirect writes here)
  vk::ImageViewCreateInfo mip0_view_ci{};
  mip0_view_ci.image = m_prefiltered_image;
  mip0_view_ci.viewType = vk::ImageViewType::eCube;
  mip0_view_ci.format = vk::Format::eR32G32B32A32Sfloat;
  mip0_view_ci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  mip0_view_ci.subresourceRange.baseMipLevel = 0;
  mip0_view_ci.subresourceRange.levelCount = 1;
  mip0_view_ci.subresourceRange.baseArrayLayer = 0;
  mip0_view_ci.subresourceRange.layerCount = 6;
  vk::ImageView cubemap_mip0_view{};
  m_device.create_image_view(mip0_view_ci, &cubemap_mip0_view, "Cubemap mip 0 storage view");

  // Per-mip views for prefilter (mip 1+)
  std::vector<vk::ImageView> prefilter_mip_views(m_mip_levels);
  for (uint32_t mip = 0; mip < m_mip_levels; ++mip)
  {
    vk::ImageViewCreateInfo vi{};
    vi.image = m_prefiltered_image;
    vi.viewType = vk::ImageViewType::eCube;
    vi.format = vk::Format::eR32G32B32A32Sfloat;
    vi.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    vi.subresourceRange.baseMipLevel = mip;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount = 6;
    m_device.create_image_view(vi, &prefilter_mip_views[mip],
      "Prefilter mip " + std::to_string(mip) + " view");
  }

  // --- Write descriptor sets ---

  // DS 0: equirect_to_cubemap (HDR sampler + cubemap mip 0 storage)
  {
    vk::DescriptorImageInfo hdr_info{};
    hdr_info.sampler = m_hdr_sampler;
    hdr_info.imageView = m_hdr_view;
    hdr_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::DescriptorImageInfo cubemap_info{};
    cubemap_info.imageView = cubemap_mip0_view;
    cubemap_info.imageLayout = vk::ImageLayout::eGeneral;

    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0].dstSet = desc_sets[0];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[0].pImageInfo = &hdr_info;

    writes[1].dstSet = desc_sets[0];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = vk::DescriptorType::eStorageImage;
    writes[1].pImageInfo = &cubemap_info;

    dev.updateDescriptorSets(writes, {});
  }

  // DS 1: irradiance (cubemap sampler + irradiance storage)
  {
    vk::DescriptorImageInfo env_info{};
    env_info.sampler = m_prefiltered_sampler;
    env_info.imageView = m_prefiltered_view;
    env_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::DescriptorImageInfo irr_info{};
    irr_info.imageView = m_irradiance_view;
    irr_info.imageLayout = vk::ImageLayout::eGeneral;

    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0].dstSet = desc_sets[1];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[0].pImageInfo = &env_info;

    writes[1].dstSet = desc_sets[1];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = vk::DescriptorType::eStorageImage;
    writes[1].pImageInfo = &irr_info;

    dev.updateDescriptorSets(writes, {});
  }

  // DS 2: BRDF LUT (storage only)
  {
    vk::DescriptorImageInfo lut_info{};
    lut_info.imageView = m_brdf_lut_view;
    lut_info.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet write{};
    write.dstSet = desc_sets[2];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.pImageInfo = &lut_info;

    dev.updateDescriptorSets(write, {});
  }

  // DS 3+: Prefilter per-mip (cubemap sampler + per-mip storage view)
  // Sampler layout is eGeneral because during the per-mip prefilter loop,
  // the target mip is in GENERAL while the sampler view covers all mips.
  for (uint32_t mip = 1; mip < m_mip_levels; ++mip)
  {
    uint32_t ds_idx = 3 + (mip - 1); // desc_sets[3] = mip 1, [4] = mip 2, etc.

    vk::DescriptorImageInfo env_info{};
    env_info.sampler = m_prefiltered_sampler;
    env_info.imageView = m_prefiltered_view;
    env_info.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo storage_info{};
    storage_info.imageView = prefilter_mip_views[mip];
    storage_info.imageLayout = vk::ImageLayout::eGeneral;

    std::array<vk::WriteDescriptorSet, 2> writes{};
    writes[0].dstSet = desc_sets[ds_idx];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[0].pImageInfo = &env_info;

    writes[1].dstSet = desc_sets[ds_idx];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = vk::DescriptorType::eStorageImage;
    writes[1].pImageInfo = &storage_info;

    dev.updateDescriptorSets(writes, {});
  }

  // --- Record compute command buffer ---
  vk::CommandPoolCreateInfo cmd_pool_ci{};
  cmd_pool_ci.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  cmd_pool_ci.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(cmd_pool_ci);

  auto cmd = begin_single_time_commands(m_device, cmd_pool);

  // ========= Stage 1: Equirect -> Cubemap mip 0 =========
  transition_image_layout(cmd, m_prefiltered_image,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, m_mip_levels, 6,
    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
    {}, vk::AccessFlagBits::eShaderWrite);

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, equirect_pipeline.pipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, equirect_pipeline.layout,
    0, desc_sets[0], {});

  struct EquirectPC { uint32_t face; uint32_t resolution; };
  for (uint32_t face = 0; face < 6; ++face)
  {
    EquirectPC pc{ face, m_resolution };
    cmd.pushConstants(equirect_pipeline.layout, vk::ShaderStageFlagBits::eCompute,
      0, sizeof(pc), &pc);
    cmd.dispatch((m_resolution + 7) / 8, (m_resolution + 7) / 8, 1);
  }

  // ========= Stage 2: Generate cubemap mip chain via blit =========
  // Transition mip 0 to transfer src
  transition_mip_layout(cmd, m_prefiltered_image,
    vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal, 0, 6,
    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);

  for (uint32_t mip = 1; mip < m_mip_levels; ++mip)
  {
    // Transition this mip to transfer dst
    transition_mip_layout(cmd, m_prefiltered_image,
      vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal, mip, 6,
      vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
      {}, vk::AccessFlagBits::eTransferWrite);

    uint32_t src_size = std::max(1u, m_resolution >> (mip - 1));
    uint32_t dst_size = std::max(1u, m_resolution >> mip);

    vk::ImageBlit blit{};
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.srcSubresource.mipLevel = mip - 1;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 6;
    blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
    blit.srcOffsets[1] = vk::Offset3D{
      static_cast<int32_t>(src_size), static_cast<int32_t>(src_size), 1 };

    blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.dstSubresource.mipLevel = mip;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 6;
    blit.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
    blit.dstOffsets[1] = vk::Offset3D{
      static_cast<int32_t>(dst_size), static_cast<int32_t>(dst_size), 1 };

    cmd.blitImage(m_prefiltered_image, vk::ImageLayout::eTransferSrcOptimal,
      m_prefiltered_image, vk::ImageLayout::eTransferDstOptimal,
      blit, vk::Filter::eLinear);

    // Transition this mip to transfer src (for next mip's blit)
    transition_mip_layout(cmd, m_prefiltered_image,
      vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, mip, 6,
      vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
      vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead);
  }

  // Transition all mips to shader read for irradiance/prefilter sampling
  transition_image_layout(cmd, m_prefiltered_image,
    vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
    m_mip_levels, 6,
    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
    vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eShaderRead);

  // ========= Stage 3: Irradiance convolution =========
  transition_image_layout(cmd, m_irradiance_image,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, 6,
    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
    {}, vk::AccessFlagBits::eShaderWrite);

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, irradiance_pipeline.pipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, irradiance_pipeline.layout,
    0, desc_sets[1], {});

  struct IrradiancePC { uint32_t face; uint32_t resolution; uint32_t sampleCount; uint32_t envResolution; };
  for (uint32_t face = 0; face < 6; ++face)
  {
    IrradiancePC pc{ face, IRR_SIZE, IRR_SAMPLES, m_resolution };
    cmd.pushConstants(irradiance_pipeline.layout, vk::ShaderStageFlagBits::eCompute,
      0, sizeof(pc), &pc);
    cmd.dispatch((IRR_SIZE + 7) / 8, (IRR_SIZE + 7) / 8, 1);
  }

  // Transition irradiance to shader read
  transition_image_layout(cmd, m_irradiance_image,
    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 6,
    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

  // ========= Stage 4: Prefiltered GGX (per mip level, skip mip 0 = raw copy) =========
  // All mips go to GENERAL for the duration of the prefilter loop.
  // The sampler view covers all mips, so they must all be in the same layout
  // as declared in the descriptor (eGeneral).
  transition_image_layout(cmd, m_prefiltered_image,
    vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral, m_mip_levels, 6,
    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
    vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, prefilter_pipeline.pipeline);

  for (uint32_t mip = 1; mip < m_mip_levels; ++mip)
  {
    float roughness = std::min(1.0f, static_cast<float>(mip) / MAX_REFLECTION_LOD);
    uint32_t mip_size = std::max(1u, m_resolution >> mip);
    uint32_t ds_idx = 3 + (mip - 1);

    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, prefilter_pipeline.layout,
      0, desc_sets[ds_idx], {});

    struct PrefilterPC {
      uint32_t face; uint32_t resolution; float roughness;
      uint32_t sampleCount; uint32_t envResolution;
    };
    for (uint32_t face = 0; face < 6; ++face)
    {
      PrefilterPC pc{ face, mip_size, roughness, PREFILTER_SAMPLES, m_resolution };
      cmd.pushConstants(prefilter_pipeline.layout, vk::ShaderStageFlagBits::eCompute,
        0, sizeof(pc), &pc);
      cmd.dispatch((mip_size + 7) / 8, (mip_size + 7) / 8, 1);
    }

    // Memory barrier: ensure writes to this mip are visible before next mip reads it
    vk::ImageMemoryBarrier barrier{};
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    barrier.oldLayout = vk::ImageLayout::eGeneral;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.image = m_prefiltered_image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = mip;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
      vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, barrier);
  }

  // Transition all mips to shader read for fragment sampling
  transition_image_layout(cmd, m_prefiltered_image,
    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, m_mip_levels, 6,
    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

  // ========= Stage 5: BRDF LUT =========
  transition_image_layout(cmd, m_brdf_lut_image,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, 1,
    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
    {}, vk::AccessFlagBits::eShaderWrite);

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, brdf_pipeline.pipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, brdf_pipeline.layout,
    0, desc_sets[2], {});

  struct BrdfPC { uint32_t resolution; uint32_t sampleCount; };
  BrdfPC brdf_pc{ LUT_SIZE, BRDF_SAMPLES };
  cmd.pushConstants(brdf_pipeline.layout, vk::ShaderStageFlagBits::eCompute,
    0, sizeof(brdf_pc), &brdf_pc);
  cmd.dispatch((LUT_SIZE + 7) / 8, (LUT_SIZE + 7) / 8, 1);

  // Transition BRDF LUT to shader read
  transition_image_layout(cmd, m_brdf_lut_image,
    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 1,
    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

  // ========= Submit =========
  end_single_time_commands(m_device, cmd_pool, cmd);

  spdlog::info("GPU IBL generation complete");

  // --- Cleanup compute resources ---
  dev.destroyCommandPool(cmd_pool);

  dev.destroyImageView(cubemap_mip0_view);
  for (auto& view : prefilter_mip_views)
    dev.destroyImageView(view);

  dev.destroyDescriptorPool(desc_pool);

  destroy_compute_pipeline(dev, equirect_pipeline);
  destroy_compute_pipeline(dev, irradiance_pipeline);
  destroy_compute_pipeline(dev, prefilter_pipeline);
  destroy_compute_pipeline(dev, brdf_pipeline);
}

void IBL::create_default_environment()
{
  // Create minimal irradiance and prefiltered maps with neutral gray
  auto dev = m_device.device();

  constexpr uint32_t CUBE_SIZE = 32;

  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.format = vk::Format::eR8G8B8A8Unorm;
  image_info.extent = vk::Extent3D{ CUBE_SIZE, CUBE_SIZE, 1 };
  image_info.mipLevels = 1;
  image_info.arrayLayers = 6;
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.tiling = vk::ImageTiling::eOptimal;
  image_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.initialLayout = vk::ImageLayout::eUndefined;
  image_info.flags = vk::ImageCreateFlagBits::eCubeCompatible;

  // Create both cubemap images
  m_irradiance_image = dev.createImage(image_info);
  m_prefiltered_image = dev.createImage(image_info);

  // Allocate memory for irradiance
  auto irr_mem_reqs = dev.getImageMemoryRequirements(m_irradiance_image);
  vk::MemoryAllocateInfo irr_alloc{};
  irr_alloc.allocationSize = irr_mem_reqs.size;
  irr_alloc.memoryTypeIndex =
    m_device.find_memory_type(irr_mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
  m_irradiance_memory = dev.allocateMemory(irr_alloc);
  dev.bindImageMemory(m_irradiance_image, m_irradiance_memory, 0);

  // Allocate memory for prefiltered
  auto pf_mem_reqs = dev.getImageMemoryRequirements(m_prefiltered_image);
  vk::MemoryAllocateInfo pf_alloc{};
  pf_alloc.allocationSize = pf_mem_reqs.size;
  pf_alloc.memoryTypeIndex =
    m_device.find_memory_type(pf_mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
  m_prefiltered_memory = dev.allocateMemory(pf_alloc);
  dev.bindImageMemory(m_prefiltered_image, m_prefiltered_memory, 0);

  // Create neutral gray pixel data for all 6 faces
  std::vector<uint8_t> gray_data(CUBE_SIZE * CUBE_SIZE * 4 * 6);
  for (size_t i = 0; i < gray_data.size(); i += 4)
  {
    gray_data[i + 0] = 128;
    gray_data[i + 1] = 128;
    gray_data[i + 2] = 128;
    gray_data[i + 3] = 255;
  }

  Buffer staging(m_device, "Cubemap staging", gray_data.size(),
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  staging.update(gray_data.data(), gray_data.size());

  std::vector<vk::BufferImageCopy> regions(6);
  for (uint32_t face = 0; face < 6; ++face)
  {
    regions[face].bufferOffset = face * CUBE_SIZE * CUBE_SIZE * 4;
    regions[face].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    regions[face].imageSubresource.mipLevel = 0;
    regions[face].imageSubresource.baseArrayLayer = face;
    regions[face].imageSubresource.layerCount = 1;
    regions[face].imageExtent = vk::Extent3D{ CUBE_SIZE, CUBE_SIZE, 1 };
  }

  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);
  auto cmd = begin_single_time_commands(m_device, cmd_pool);

  transition_image_layout(cmd, m_irradiance_image,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1, 6,
    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
    {}, vk::AccessFlagBits::eTransferWrite);
  cmd.copyBufferToImage(staging.buffer(), m_irradiance_image,
    vk::ImageLayout::eTransferDstOptimal, regions);
  transition_image_layout(cmd, m_irradiance_image,
    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 6,
    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
    vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);

  transition_image_layout(cmd, m_prefiltered_image,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1, 6,
    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
    {}, vk::AccessFlagBits::eTransferWrite);
  cmd.copyBufferToImage(staging.buffer(), m_prefiltered_image,
    vk::ImageLayout::eTransferDstOptimal, regions);
  transition_image_layout(cmd, m_prefiltered_image,
    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 6,
    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
    vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);

  end_single_time_commands(m_device, cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  // Create views and samplers
  vk::ImageViewCreateInfo view_info{};
  view_info.viewType = vk::ImageViewType::eCube;
  view_info.format = vk::Format::eR8G8B8A8Unorm;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 6;

  view_info.image = m_irradiance_image;
  m_device.create_image_view(view_info, &m_irradiance_view, "Irradiance cubemap view");

  view_info.image = m_prefiltered_image;
  m_device.create_image_view(view_info, &m_prefiltered_view, "Prefiltered cubemap view");

  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.maxLod = 1.0f;

  m_irradiance_sampler = dev.createSampler(sampler_info);
  m_prefiltered_sampler = dev.createSampler(sampler_info);

  // Also create BRDF LUT for default environment
  constexpr uint32_t LUT_SIZE = 128;

  create_image(m_device, m_brdf_lut_image, m_brdf_lut_memory,
    LUT_SIZE, LUT_SIZE, 1, 1,
    vk::Format::eR8G8B8A8Unorm,
    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);

  vk::ImageViewCreateInfo lut_view_info{};
  lut_view_info.image = m_brdf_lut_image;
  lut_view_info.viewType = vk::ImageViewType::e2D;
  lut_view_info.format = vk::Format::eR8G8B8A8Unorm;
  lut_view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  lut_view_info.subresourceRange.baseMipLevel = 0;
  lut_view_info.subresourceRange.levelCount = 1;
  lut_view_info.subresourceRange.baseArrayLayer = 0;
  lut_view_info.subresourceRange.layerCount = 1;
  m_device.create_image_view(lut_view_info, &m_brdf_lut_view, "BRDF LUT view");

  sampler_info.maxLod = 1.0f;
  m_brdf_lut_sampler = dev.createSampler(sampler_info);

  // Generate BRDF LUT via compute shader
  auto brdf_pipeline = create_compute_pipeline(dev, SHADER_DIR "brdf_lut.comp",
    { { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute } },
    8);

  std::array<vk::DescriptorPoolSize, 1> pool_sizes = {
    vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage, 1 }
  };
  vk::DescriptorPoolCreateInfo dpool_ci{};
  dpool_ci.maxSets = 1;
  dpool_ci.poolSizeCount = 1;
  dpool_ci.pPoolSizes = pool_sizes.data();
  auto desc_pool = dev.createDescriptorPool(dpool_ci);

  vk::DescriptorSetAllocateInfo ds_alloc{};
  ds_alloc.descriptorPool = desc_pool;
  ds_alloc.descriptorSetCount = 1;
  ds_alloc.pSetLayouts = &brdf_pipeline.desc_layout;
  auto ds = dev.allocateDescriptorSets(ds_alloc)[0];

  vk::DescriptorImageInfo lut_info{};
  lut_info.imageView = m_brdf_lut_view;
  lut_info.imageLayout = vk::ImageLayout::eGeneral;

  vk::WriteDescriptorSet write{};
  write.dstSet = ds;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = vk::DescriptorType::eStorageImage;
  write.pImageInfo = &lut_info;
  dev.updateDescriptorSets(write, {});

  cmd_pool = dev.createCommandPool(pool_info);
  cmd = begin_single_time_commands(m_device, cmd_pool);

  transition_image_layout(cmd, m_brdf_lut_image,
    vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, 1,
    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
    {}, vk::AccessFlagBits::eShaderWrite);

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, brdf_pipeline.pipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, brdf_pipeline.layout, 0, ds, {});

  struct BrdfPC { uint32_t resolution; uint32_t sampleCount; };
  BrdfPC brdf_pc{ LUT_SIZE, 256 };
  cmd.pushConstants(brdf_pipeline.layout, vk::ShaderStageFlagBits::eCompute,
    0, sizeof(brdf_pc), &brdf_pc);
  cmd.dispatch((LUT_SIZE + 7) / 8, (LUT_SIZE + 7) / 8, 1);

  transition_image_layout(cmd, m_brdf_lut_image,
    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 1, 1,
    vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

  end_single_time_commands(m_device, cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  dev.destroyDescriptorPool(desc_pool);
  destroy_compute_pipeline(dev, brdf_pipeline);

  spdlog::trace("Default IBL environment created");
}

} // namespace vkwave
