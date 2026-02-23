#include <vkwave/pipeline/execution_group.h>

#include <vkwave/core/commands.h>
#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/pipeline/framebuffer.h>
#include <vkwave/pipeline/pipeline.h>
#include <vkwave/pipeline/shader_compiler.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cassert>

namespace vkwave
{

static vk::BufferUsageFlags usage_for_descriptor_type(vk::DescriptorType type)
{
  switch (type)
  {
  case vk::DescriptorType::eUniformBuffer:
  case vk::DescriptorType::eUniformBufferDynamic:
    return vk::BufferUsageFlagBits::eUniformBuffer;
  case vk::DescriptorType::eStorageBuffer:
  case vk::DescriptorType::eStorageBufferDynamic:
    return vk::BufferUsageFlagBits::eStorageBuffer;
  default:
    assert(false && "unsupported descriptor type for auto-buffer");
    return {};
  }
}

ExecutionGroup::ExecutionGroup(
  const Device& device, const std::string& name,
  const PipelineSpec& spec, vk::Format swapchain_format,
  bool debug)
  : SubmissionGroup(device, name, debug)
  , m_depth_enabled(spec.depth_test)
  , m_depth_format(spec.depth_format)
{
  // Compile shaders
  auto vert = ShaderCompiler::compile(spec.vertex_shader, vk::ShaderStageFlagBits::eVertex);
  auto frag = ShaderCompiler::compile(spec.fragment_shader, vk::ShaderStageFlagBits::eFragment);

  // Reflect layout
  ShaderReflection reflection;
  reflection.add_stage(vert.spirv, vk::ShaderStageFlagBits::eVertex);
  reflection.add_stage(frag.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  // Store reflected descriptor set info for auto-creating UBOs later
  m_reflected_sets = reflection.descriptor_set_infos();

  // Register buffer specs for each binding with blockSize > 0 (UBO/SSBO)
  for (auto& set_info : m_reflected_sets)
  {
    for (auto& b : set_info.bindings)
    {
      if (b.blockSize > 0)
      {
        BufferHandle handle = m_buffer_specs.size();
        m_buffer_specs.push_back({
          fmt::format("set{}_binding{}", set_info.set, b.binding),
          b.blockSize,
          usage_for_descriptor_type(b.type),
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        });
        m_binding_to_handle[{ set_info.set, b.binding }] = handle;
      }
    }
  }

  auto d = device.device();
  auto vert_mod = ShaderCompiler::create_module(d, vert.spirv);
  auto frag_mod = ShaderCompiler::create_module(d, frag.spirv);

  // Build GraphicsPipelineInBundle from PipelineSpec + reflection
  GraphicsPipelineInBundle bundle_in{};
  bundle_in.device = d;
  bundle_in.swapchainExtent = vk::Extent2D{ 1, 1 }; // viewport/scissor are dynamic state
  bundle_in.swapchainImageFormat = swapchain_format;
  bundle_in.backfaceCulling = spec.backface_culling;
  bundle_in.depthTestEnabled = spec.depth_test;
  bundle_in.depthWriteEnabled = spec.depth_write;
  bundle_in.depthFormat = spec.depth_format;
  bundle_in.blendEnabled = spec.blend;
  bundle_in.dynamicCullMode = spec.dynamic_cull_mode;
  bundle_in.dynamicDepthWrite = spec.dynamic_depth_write;
  bundle_in.msaaSamples = spec.msaa_samples;
  bundle_in.vertexModule = vert_mod;
  bundle_in.fragmentModule = frag_mod;
  bundle_in.reflection = &reflection;
  bundle_in.vertexBindings = spec.vertex_bindings;
  bundle_in.vertexAttributes = spec.vertex_attributes;
  bundle_in.existingRenderPass = spec.existing_renderpass;

  auto bundle_out = create_graphics_pipeline(bundle_in, debug);
  m_pipeline = bundle_out.pipeline;
  m_layout = bundle_out.layout;
  m_renderpass = bundle_out.renderpass;
  m_owns_renderpass = !spec.existing_renderpass;
  m_descriptor_layouts = std::move(bundle_out.descriptorSetLayouts);

  // Destroy shader modules (no longer needed after pipeline creation)
  d.destroyShaderModule(vert_mod);
  d.destroyShaderModule(frag_mod);

  // Default clear values
  m_clear_values.resize(m_depth_enabled ? 2 : 1);
  m_clear_values[0].color = std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f };
  if (m_depth_enabled)
    m_clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };

  spdlog::debug("ExecutionGroup '{}': pipeline created, {} auto-buffered bindings",
    name, m_binding_to_handle.size());
}

ExecutionGroup::~ExecutionGroup()
{
  // Frame resources must be destroyed before pipeline state,
  // because framebuffers reference the renderpass.
  destroy_frame_resources();

  auto d = m_device.device();
  if (m_pipeline)
    d.destroyPipeline(m_pipeline);
  if (m_layout)
    d.destroyPipelineLayout(m_layout);
  if (m_renderpass && m_owns_renderpass)
    d.destroyRenderPass(m_renderpass);
  for (auto layout : m_descriptor_layouts)
    d.destroyDescriptorSetLayout(layout);
}

void ExecutionGroup::set_clear_values(std::vector<vk::ClearValue> values)
{
  m_clear_values = std::move(values);
}

void ExecutionGroup::set_color_views(std::vector<vk::ImageView> views)
{
  m_color_views = std::move(views);
}

void ExecutionGroup::create_frame_resources(
  const Swapchain& swapchain, uint32_t count)
{
  // Base class creates command pools/buffers, present semaphores, timeline tracking
  SubmissionGroup::create_frame_resources(swapchain, count);

  create_frame_resources_internal(swapchain.extent(), count,
    m_color_views.empty() ? swapchain.image_views() : m_color_views);
}

void ExecutionGroup::create_frame_resources(vk::Extent2D extent, uint32_t count)
{
  // For offscreen groups: create base resources without swapchain
  SubmissionGroup::create_frame_resources_offscreen(extent, count);

  create_frame_resources_internal(extent, count, m_color_views);
}

void ExecutionGroup::create_frame_resources_internal(
  vk::Extent2D extent, uint32_t count,
  const std::vector<vk::ImageView>& color_views)
{
  // Create depth buffer if enabled (size-dependent, not ring-buffered)
  if (m_depth_enabled)
  {
    m_depth_buffer = std::make_unique<DepthStencilAttachment>(
      m_device, m_depth_format, extent, vk::SampleCountFlagBits::e1);
  }

  // Create framebuffers
  auto depth_view = m_depth_buffer ? m_depth_buffer->combined_view() : VK_NULL_HANDLE;

  for (uint32_t i = 0; i < count; ++i)
  {
    std::vector<vk::ImageView> attachments;
    attachments.push_back(color_views[i]);
    if (depth_view)
      attachments.push_back(depth_view);

    vk::FramebufferCreateInfo fb_info{};
    fb_info.renderPass = m_renderpass;
    fb_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    fb_info.pAttachments = attachments.data();
    fb_info.width = extent.width;
    fb_info.height = extent.height;
    fb_info.layers = 1;

    m_frames[i].framebuffer = m_device.device().createFramebuffer(fb_info);
  }

  // Allocate ring-buffered managed buffers (specs populated from reflection in constructor)
  m_buffers.resize(m_buffer_specs.size());
  for (size_t h = 0; h < m_buffer_specs.size(); ++h)
  {
    auto& spec = m_buffer_specs[h];
    m_buffers[h].clear();
    m_buffers[h].reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
      m_buffers[h].push_back(std::make_unique<Buffer>(
        m_device, fmt::format("{}_{}_{}", m_name, spec.name, i),
        spec.size, spec.usage, spec.properties));
    }
  }

  // Create descriptor pool and sets if layouts were provided
  if (!m_descriptor_layouts.empty())
  {
    const auto num_sets = static_cast<uint32_t>(m_descriptor_layouts.size());

    // Fill in default set counts (ring-buffered = count) for any set not overridden.
    // 0 means "not explicitly set" — replace with the ring-buffer count.
    m_set_counts.resize(num_sets, count);
    for (auto& c : m_set_counts)
      if (c == 0) c = count;

    // Compute pool sizes: each binding type × that set's allocation count
    uint32_t total_sets = 0;
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    for (auto& set_info : m_reflected_sets)
    {
      uint32_t set_count = (set_info.set < num_sets) ? m_set_counts[set_info.set] : count;
      total_sets += set_count;
      for (auto& b : set_info.bindings)
        pool_sizes.push_back({ b.type, set_count });
    }

    if (!pool_sizes.empty())
    {
      vk::DescriptorPoolCreateInfo pool_info{};
      pool_info.maxSets = total_sets;
      pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
      pool_info.pPoolSizes = pool_sizes.data();

      m_descriptor_pool = m_device.device().createDescriptorPool(pool_info);

      // Allocate descriptor sets per set index with each set's own count
      m_descriptor_sets.resize(num_sets);
      for (uint32_t s = 0; s < num_sets; ++s)
      {
        uint32_t n = m_set_counts[s];
        std::vector<vk::DescriptorSetLayout> alloc_layouts(n, m_descriptor_layouts[s]);

        vk::DescriptorSetAllocateInfo alloc_info{};
        alloc_info.descriptorPool = m_descriptor_pool;
        alloc_info.descriptorSetCount = n;
        alloc_info.pSetLayouts = alloc_layouts.data();

        m_descriptor_sets[s] = m_device.device().allocateDescriptorSets(alloc_info);
      }

      // Write UBO/SSBO buffer descriptors to sets that contain buffer bindings.
      // Buffers are ring-buffered by slot, so the set's allocation count must
      // match `count` for the buffer index to make sense.
      for (auto& [key, handle] : m_binding_to_handle)
      {
        uint32_t s = key.first;
        if (s >= num_sets) continue;
        assert(m_set_counts[s] == count &&
          "set with auto-created buffers must have allocation count == ring-buffer count");

        // Find the descriptor type
        vk::DescriptorType dtype = vk::DescriptorType::eUniformBuffer;
        for (auto& set_info : m_reflected_sets)
        {
          if (set_info.set != s) continue;
          for (auto& b : set_info.bindings)
          {
            if (b.binding == key.second)
            {
              dtype = b.type;
              break;
            }
          }
        }

        for (uint32_t i = 0; i < count; ++i)
        {
          auto& buf = *m_buffers[handle][i];
          vk::DescriptorBufferInfo buffer_info{ buf.buffer(), 0, buf.size() };

          vk::WriteDescriptorSet write{};
          write.dstSet = m_descriptor_sets[s][i];
          write.dstBinding = key.second;
          write.dstArrayElement = 0;
          write.descriptorCount = 1;
          write.descriptorType = dtype;
          write.pBufferInfo = &buffer_info;

          m_device.device().updateDescriptorSets(write, {});
        }
      }
    }
  }
}

void ExecutionGroup::destroy_frame_resources()
{
  // Destroy ExecutionGroup-specific resources first
  if (m_descriptor_pool)
  {
    m_device.device().destroyDescriptorPool(m_descriptor_pool);
    m_descriptor_pool = VK_NULL_HANDLE;
  }
  for (auto& group : m_descriptor_sets)
    group.clear();
  m_descriptor_sets.clear();
  m_buffers.clear();
  m_depth_buffer.reset();

  // Then base class destroys command pools, present semaphores, timeline tracking
  SubmissionGroup::destroy_frame_resources();
}

void ExecutionGroup::set_descriptor_count(uint32_t set_index, uint32_t n)
{
  if (m_set_counts.size() <= set_index)
    m_set_counts.resize(set_index + 1, 0);
  m_set_counts[set_index] = n;
}

void ExecutionGroup::write_image_descriptor(
  uint32_t set, uint32_t binding,
  vk::ImageView view, vk::Sampler sampler, vk::ImageLayout layout)
{
  assert(set < m_descriptor_sets.size() && "set index out of range");

  for (size_t i = 0; i < m_descriptor_sets[set].size(); ++i)
  {
    vk::DescriptorImageInfo image_info{};
    image_info.sampler = sampler;
    image_info.imageView = view;
    image_info.imageLayout = layout;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_descriptor_sets[set][i];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &image_info;

    m_device.device().updateDescriptorSets(write, {});
  }
}

void ExecutionGroup::write_image_descriptor(
  uint32_t set, uint32_t binding, uint32_t index,
  vk::ImageView view, vk::Sampler sampler, vk::ImageLayout layout)
{
  assert(set < m_descriptor_sets.size() && "set index out of range");
  assert(index < m_descriptor_sets[set].size() && "index out of range");

  vk::DescriptorImageInfo image_info{};
  image_info.sampler = sampler;
  image_info.imageView = view;
  image_info.imageLayout = layout;

  vk::WriteDescriptorSet write{};
  write.dstSet = m_descriptor_sets[set][index];
  write.dstBinding = binding;
  write.dstArrayElement = 0;
  write.descriptorCount = 1;
  write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  write.pImageInfo = &image_info;

  m_device.device().updateDescriptorSets(write, {});
}

uint32_t ExecutionGroup::binding_index(uint32_t set, const std::string& name) const
{
  for (auto& set_info : m_reflected_sets)
  {
    if (set_info.set != set) continue;
    for (auto& b : set_info.bindings)
    {
      if (b.name == name)
        return b.binding;
    }
  }
  throw std::runtime_error(
    "Descriptor binding '" + name + "' not found in set " + std::to_string(set));
}

void ExecutionGroup::write_image_descriptor(
  uint32_t set, const std::string& name,
  vk::ImageView view, vk::Sampler sampler, vk::ImageLayout layout)
{
  write_image_descriptor(set, binding_index(set, name), view, sampler, layout);
}

void ExecutionGroup::write_image_descriptor(
  uint32_t set, const std::string& name, uint32_t slot,
  vk::ImageView view, vk::Sampler sampler, vk::ImageLayout layout)
{
  write_image_descriptor(set, binding_index(set, name), slot, view, sampler, layout);
}

void ExecutionGroup::record_commands(
  vk::CommandBuffer cmd, uint32_t slot_index, FrameResources& frame)
{
  // Begin render pass
  vk::RenderPassBeginInfo rp_info{};
  rp_info.renderPass = m_renderpass;
  rp_info.framebuffer = frame.framebuffer;
  rp_info.renderArea.extent = m_extent;
  rp_info.clearValueCount = static_cast<uint32_t>(m_clear_values.size());
  rp_info.pClearValues = m_clear_values.data();

  cmd.beginRenderPass(rp_info, vk::SubpassContents::eInline);

  // Record pass commands
  m_record_fn(cmd, slot_index);

  cmd.endRenderPass();
}

Buffer& ExecutionGroup::buffer(BufferHandle handle)
{
  assert(handle < m_buffers.size() && "invalid BufferHandle");
  assert(m_current_slot < m_buffers[handle].size() && "buffer() called before create_frame_resources()");
  return *m_buffers[handle][m_current_slot];
}

Buffer& ExecutionGroup::buffer(BufferHandle handle, uint32_t slot)
{
  assert(handle < m_buffers.size() && "invalid BufferHandle");
  assert(slot < m_buffers[handle].size() && "slot out of range");
  return *m_buffers[handle][slot];
}

Buffer& ExecutionGroup::ubo(uint32_t set, uint32_t binding)
{
  auto it = m_binding_to_handle.find({ set, binding });
  assert(it != m_binding_to_handle.end() && "no auto-created buffer for this (set, binding)");
  return buffer(it->second);
}

Buffer& ExecutionGroup::ubo(uint32_t set, uint32_t binding, uint32_t slot)
{
  auto it = m_binding_to_handle.find({ set, binding });
  assert(it != m_binding_to_handle.end() && "no auto-created buffer for this (set, binding)");
  return buffer(it->second, slot);
}

vk::DescriptorSet ExecutionGroup::descriptor_set() const
{
  return descriptor_set(0, m_current_slot);
}

vk::DescriptorSet ExecutionGroup::descriptor_set(uint32_t slot) const
{
  return descriptor_set(0, slot);
}

vk::DescriptorSet ExecutionGroup::descriptor_set(uint32_t set_index, uint32_t i) const
{
  assert(set_index < m_descriptor_sets.size() && "set index out of range");
  assert(i < m_descriptor_sets[set_index].size() && "index out of range");
  return m_descriptor_sets[set_index][i];
}

} // namespace vkwave
