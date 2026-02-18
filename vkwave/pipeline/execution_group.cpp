#include <vkwave/pipeline/execution_group.h>

#include <vkwave/core/commands.h>
#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/pipeline/framebuffer.h>

#include <fmt/format.h>

#include <cassert>

namespace vkwave
{

ExecutionGroup::ExecutionGroup(
  const Device& device, const std::string& name,
  vk::Pipeline pipeline, vk::PipelineLayout layout,
  vk::RenderPass renderpass)
  : m_device(device)
  , m_name(name)
  , m_pipeline(pipeline)
  , m_layout(layout)
  , m_renderpass(renderpass)
{
  m_timeline = std::make_unique<TimelineSemaphore>(
    device, fmt::format("{}_timeline", name), 0);
}

void ExecutionGroup::set_record_fn(RecordFn fn)
{
  m_record_fn = std::move(fn);
}

void ExecutionGroup::create_frame_resources(
  const Swapchain& swapchain, uint32_t count)
{
  m_frames = vkwave::create_frame_resources(m_device, count);
  m_extent = swapchain.extent();

  // Create framebuffers
  framebufferInput fb_input{};
  fb_input.device = m_device.device();
  fb_input.renderpass = m_renderpass;
  fb_input.swapchainExtent = swapchain.extent();

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

  // Allocate ring-buffered managed buffers
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
}

void ExecutionGroup::destroy_frame_resources()
{
  m_buffers.clear();
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

  // Set current slot so buffer() works inside the record callback
  m_current_slot = slot_index;

  // Reset command pool and record
  m_device.device().resetCommandPool(frame.command_pool);

  vk::CommandBufferBeginInfo begin_info{};
  frame.command_buffer.begin(begin_info);
  m_record_fn(frame.command_buffer, frame.framebuffer, m_extent);
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

BufferHandle ExecutionGroup::add_buffer(
  const std::string& name, vk::DeviceSize size,
  vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
{
  assert(m_buffers.empty() && "add_buffer() must be called before create_frame_resources()");
  assert(size > 0);
  BufferHandle handle = m_buffer_specs.size();
  m_buffer_specs.push_back({name, size, usage, properties});
  return handle;
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

} // namespace vkwave
