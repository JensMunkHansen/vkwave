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
  : m_device(device)
  , m_name(name)
  , m_debug(debug)
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
  bundle_in.msaaSamples = spec.msaa_samples;
  bundle_in.vertexModule = vert_mod;
  bundle_in.fragmentModule = frag_mod;
  bundle_in.reflection = &reflection;
  bundle_in.vertexBindings = spec.vertex_bindings;
  bundle_in.vertexAttributes = spec.vertex_attributes;

  auto bundle_out = create_graphics_pipeline(bundle_in, debug);
  m_pipeline = bundle_out.pipeline;
  m_layout = bundle_out.layout;
  m_renderpass = bundle_out.renderpass;
  m_descriptor_layouts = std::move(bundle_out.descriptorSetLayouts);

  // Destroy shader modules (no longer needed after pipeline creation)
  d.destroyShaderModule(vert_mod);
  d.destroyShaderModule(frag_mod);

  // Timeline semaphore
  m_timeline = std::make_unique<TimelineSemaphore>(
    device, fmt::format("{}_timeline", name), 0);

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
  if (m_renderpass)
    d.destroyRenderPass(m_renderpass);
  for (auto layout : m_descriptor_layouts)
    d.destroyDescriptorSetLayout(layout);
}

void ExecutionGroup::set_record_fn(RecordFn fn)
{
  m_record_fn = std::move(fn);
}

void ExecutionGroup::set_clear_values(std::vector<vk::ClearValue> values)
{
  m_clear_values = std::move(values);
}

void ExecutionGroup::create_frame_resources(
  const Swapchain& swapchain, uint32_t count)
{
  m_frames = vkwave::create_frame_resources(m_device, count);
  m_extent = swapchain.extent();

  // Create depth buffer if enabled (size-dependent, not ring-buffered)
  if (m_depth_enabled)
  {
    m_depth_buffer = std::make_unique<DepthStencilAttachment>(
      m_device, m_depth_format, m_extent, vk::SampleCountFlagBits::e1);
  }

  // Create framebuffers
  framebufferInput fb_input{};
  fb_input.device = m_device.device();
  fb_input.renderpass = m_renderpass;
  fb_input.swapchainExtent = swapchain.extent();
  fb_input.depthImageView = m_depth_buffer ? m_depth_buffer->combined_view() : VK_NULL_HANDLE;

  auto framebuffers = make_framebuffers(fb_input, swapchain, false);
  for (uint32_t i = 0; i < count; ++i)
    m_frames[i].framebuffer = framebuffers[i];

  // Create binary present semaphores (one per slot, for WSI)
  m_present_semaphores.clear();
  m_present_semaphores.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
  {
    m_present_semaphores.push_back(std::make_unique<Semaphore>(
      m_device, fmt::format("{}_{}_present", m_name, i)));
  }

  // Initialize per-slot timeline tracking.
  // Zero means "never submitted" — begin_frame() skips the wait, which is
  // correct because drain() was called before any resize.
  // Do NOT reset m_next_timeline_value: the timeline semaphore persists
  // across resize and its counter is already beyond earlier values.
  m_slot_timeline_values.assign(count, 0);

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
    // Compute pool sizes from reflected sets
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    for (auto& set_info : m_reflected_sets)
    {
      for (auto& b : set_info.bindings)
      {
        if (b.blockSize > 0)
          pool_sizes.push_back({ b.type, count });
      }
    }

    if (!pool_sizes.empty())
    {
      vk::DescriptorPoolCreateInfo pool_info{};
      pool_info.maxSets = count;
      pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
      pool_info.pPoolSizes = pool_sizes.data();

      m_descriptor_pool = m_device.device().createDescriptorPool(pool_info);

      // Allocate one set per slot (all using the same layout[0])
      std::vector<vk::DescriptorSetLayout> alloc_layouts(count, m_descriptor_layouts[0]);

      vk::DescriptorSetAllocateInfo alloc_info{};
      alloc_info.descriptorPool = m_descriptor_pool;
      alloc_info.descriptorSetCount = count;
      alloc_info.pSetLayouts = alloc_layouts.data();

      m_descriptor_sets = m_device.device().allocateDescriptorSets(alloc_info);

      // Write descriptor updates: bind each slot's buffer to its set
      for (uint32_t i = 0; i < count; ++i)
      {
        std::vector<vk::WriteDescriptorSet> writes;
        std::vector<vk::DescriptorBufferInfo> buffer_infos;
        buffer_infos.reserve(m_binding_to_handle.size());

        for (auto& [key, handle] : m_binding_to_handle)
        {
          auto& buf = *m_buffers[handle][i];
          buffer_infos.push_back({ buf.buffer(), 0, buf.size() });

          // Find the binding info for the descriptor type
          vk::DescriptorType dtype = vk::DescriptorType::eUniformBuffer;
          for (auto& set_info : m_reflected_sets)
          {
            if (set_info.set != key.first) continue;
            for (auto& b : set_info.bindings)
            {
              if (b.binding == key.second)
              {
                dtype = b.type;
                break;
              }
            }
          }

          vk::WriteDescriptorSet write{};
          write.dstSet = m_descriptor_sets[i];
          write.dstBinding = key.second;
          write.dstArrayElement = 0;
          write.descriptorCount = 1;
          write.descriptorType = dtype;
          write.pBufferInfo = &buffer_infos.back();
          writes.push_back(write);
        }

        m_device.device().updateDescriptorSets(writes, {});
      }
    }
  }
}

void ExecutionGroup::destroy_frame_resources()
{
  if (m_descriptor_pool)
  {
    m_device.device().destroyDescriptorPool(m_descriptor_pool);
    m_descriptor_pool = VK_NULL_HANDLE;
  }
  m_descriptor_sets.clear();
  m_buffers.clear();
  m_depth_buffer.reset();
  vkwave::destroy_frame_resources(m_frames, m_device.device());
  m_present_semaphores.clear();
  m_slot_timeline_values.clear();
}

void ExecutionGroup::begin_frame(uint32_t slot_index)
{
  // Wait until the GPU finishes the previous submission for this slot.
  // Slot 0 starts with value 0 — the timeline was created at 0,
  // so the first wait passes immediately (like a signaled fence).
  if (m_slot_timeline_values[slot_index] > 0)
    m_timeline->wait(m_slot_timeline_values[slot_index]);
}

void ExecutionGroup::submit(
  uint32_t slot_index,
  vk::Semaphore wait_acquire,
  vk::Queue queue)
{
  auto& frame = m_frames[slot_index];

  // Set current slot so buffer()/descriptor_set() work inside the record callback
  m_current_slot = slot_index;

  // Reset command pool and record
  m_device.device().resetCommandPool(frame.command_pool);

  vk::CommandBufferBeginInfo begin_info{};
  frame.command_buffer.begin(begin_info);

  // Begin render pass
  vk::RenderPassBeginInfo rp_info{};
  rp_info.renderPass = m_renderpass;
  rp_info.framebuffer = frame.framebuffer;
  rp_info.renderArea.extent = m_extent;
  rp_info.clearValueCount = static_cast<uint32_t>(m_clear_values.size());
  rp_info.pClearValues = m_clear_values.data();

  frame.command_buffer.beginRenderPass(rp_info, vk::SubpassContents::eInline);

  // Record pass commands
  m_record_fn(frame.command_buffer, slot_index);

  frame.command_buffer.endRenderPass();
  frame.command_buffer.end();

  // Assign the next timeline value to this submission
  const uint64_t signal_value = m_next_timeline_value++;
  m_slot_timeline_values[slot_index] = signal_value;

  // Two wait semaphores: acquire (binary)
  // Two signal semaphores: timeline + present (binary)
  std::array<vk::Semaphore, 1> wait_sems = { wait_acquire };
  std::array<vk::PipelineStageFlags, 1> wait_stages = {
    vk::PipelineStageFlagBits::eColorAttachmentOutput
  };

  std::array<vk::Semaphore, 2> signal_sems = {
    m_timeline->get(),
    *m_present_semaphores[slot_index]->semaphore()
  };

  // Timeline submit info: for each wait/signal semaphore, provide a value.
  // Binary semaphore values are ignored (use 0).
  std::array<uint64_t, 1> wait_values = { 0 };        // binary acquire — ignored
  std::array<uint64_t, 2> signal_values = {
    signal_value, 0  // timeline value, binary present — ignored
  };

  vk::TimelineSemaphoreSubmitInfo timeline_info{};
  timeline_info.waitSemaphoreValueCount = static_cast<uint32_t>(wait_values.size());
  timeline_info.pWaitSemaphoreValues = wait_values.data();
  timeline_info.signalSemaphoreValueCount = static_cast<uint32_t>(signal_values.size());
  timeline_info.pSignalSemaphoreValues = signal_values.data();

  vk::SubmitInfo submit{};
  submit.pNext = &timeline_info;
  submit.waitSemaphoreCount = static_cast<uint32_t>(wait_sems.size());
  submit.pWaitSemaphores = wait_sems.data();
  submit.pWaitDstStageMask = wait_stages.data();
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &frame.command_buffer;
  submit.signalSemaphoreCount = static_cast<uint32_t>(signal_sems.size());
  submit.pSignalSemaphores = signal_sems.data();

  // No fence — timeline semaphore handles CPU-GPU sync
  queue.submit(submit, nullptr);
}

void ExecutionGroup::drain()
{
  if (m_next_timeline_value <= 1)
    return; // nothing submitted yet

  // Single wait for the highest submitted value drains all slots
  m_timeline->wait(m_next_timeline_value - 1);
}

const vk::Semaphore* ExecutionGroup::present_semaphore(uint32_t slot) const
{
  return m_present_semaphores[slot]->semaphore();
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
  assert(!m_descriptor_sets.empty() && "no descriptor sets allocated");
  assert(m_current_slot < m_descriptor_sets.size() && "descriptor_set() called before create_frame_resources()");
  return m_descriptor_sets[m_current_slot];
}

vk::DescriptorSet ExecutionGroup::descriptor_set(uint32_t slot) const
{
  assert(!m_descriptor_sets.empty() && "no descriptor sets allocated");
  assert(slot < m_descriptor_sets.size() && "slot out of range");
  return m_descriptor_sets[slot];
}

} // namespace vkwave
