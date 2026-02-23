#include <vkwave/pipeline/submission_group.h>

#include <vkwave/core/commands.h>
#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace vkwave
{

SubmissionGroup::SubmissionGroup(
  const Device& device, const std::string& name, bool debug)
  : m_device(device)
  , m_name(name)
  , m_debug(debug)
{
  m_timeline = std::make_unique<TimelineSemaphore>(
    device, fmt::format("{}_timeline", name), 0);

  spdlog::debug("SubmissionGroup '{}': created", name);
}

SubmissionGroup::~SubmissionGroup()
{
  destroy_frame_resources();
}

void SubmissionGroup::set_record_fn(RecordFn fn)
{
  m_record_fn = std::move(fn);
}

void SubmissionGroup::set_post_record_fn(PostRecordFn fn)
{
  m_post_record_fn = std::move(fn);
}

void SubmissionGroup::create_frame_resources(
  const Swapchain& swapchain, uint32_t count)
{
  m_frames = vkwave::create_frame_resources(m_device, count);
  m_extent = swapchain.extent();

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

  // Gating: no slot has been submitted yet after create/resize
  m_slot_submitted.assign(count, false);
}

void SubmissionGroup::create_frame_resources_offscreen(
  vk::Extent2D extent, uint32_t count)
{
  m_frames = vkwave::create_frame_resources(m_device, count);
  m_extent = extent;

  // Binary present semaphores — still created but only signaled if m_signal_binary_present
  m_present_semaphores.clear();
  m_present_semaphores.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
  {
    m_present_semaphores.push_back(std::make_unique<Semaphore>(
      m_device, fmt::format("{}_{}_present", m_name, i)));
  }

  m_slot_timeline_values.assign(count, 0);
  m_slot_submitted.assign(count, false);
}

void SubmissionGroup::destroy_frame_resources()
{
  vkwave::destroy_frame_resources(m_frames, m_device.device());
  m_present_semaphores.clear();
  m_slot_timeline_values.clear();
}

void SubmissionGroup::set_gating(GatingMode mode, float hz)
{
  m_gating = mode;
  m_target_interval = (hz > 0.0f) ? (1.0f / hz) : 0.0f;
}

bool SubmissionGroup::should_submit(float elapsed_time, bool is_fifo) const
{
  switch (m_gating)
  {
  case GatingMode::always:
    return true;
  case GatingMode::display_gated:
    return is_fifo;
  case GatingMode::wall_clock:
    return (elapsed_time - m_last_run_time) >= m_target_interval;
  }
  return true;
}

void SubmissionGroup::begin_frame(uint32_t slot_index, bool will_submit)
{
  // Wait until the GPU finishes the previous submission for this slot,
  // but only if we actually submitted last time on this slot.
  if (m_slot_submitted[slot_index] && m_slot_timeline_values[slot_index] > 0)
    m_timeline->wait(m_slot_timeline_values[slot_index]);

  m_slot_submitted[slot_index] = will_submit;
}

void SubmissionGroup::submit(
  uint32_t slot_index,
  std::span<const SemaphoreWait> waits,
  vk::Queue queue,
  float elapsed_time)
{
  m_last_run_time = elapsed_time;
  auto& frame = m_frames[slot_index];

  // Set current slot so derived class accessors work inside the record callback
  m_current_slot = slot_index;

  // Reset command pool and record
  m_device.device().resetCommandPool(frame.command_pool);

  vk::CommandBufferBeginInfo begin_info{};
  frame.command_buffer.begin(begin_info);

  // Delegate to virtual hook for pass-specific recording
  record_commands(frame.command_buffer, slot_index, frame);

  // Optional post-record overlay (e.g. ImGui) — same command buffer, no extra submit
  if (m_post_record_fn)
    m_post_record_fn(frame.command_buffer, slot_index);

  frame.command_buffer.end();

  // Assign the next timeline value to this submission
  const uint64_t signal_value = m_next_timeline_value++;
  m_slot_timeline_values[slot_index] = signal_value;

  // Build wait arrays from SemaphoreWait structs
  std::vector<vk::Semaphore> wait_sems;
  std::vector<uint64_t> wait_values;
  std::vector<vk::PipelineStageFlags> wait_stages;
  wait_sems.reserve(waits.size());
  wait_values.reserve(waits.size());
  wait_stages.reserve(waits.size());
  for (auto& w : waits)
  {
    wait_sems.push_back(w.semaphore);
    wait_values.push_back(w.value);
    wait_stages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
  }

  // Signal semaphores: always timeline, conditionally binary present
  std::vector<vk::Semaphore> signal_sems;
  std::vector<uint64_t> signal_values;
  signal_sems.push_back(m_timeline->get());
  signal_values.push_back(signal_value);
  if (m_signal_binary_present)
  {
    signal_sems.push_back(*m_present_semaphores[slot_index]->semaphore());
    signal_values.push_back(0); // binary — ignored
  }

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

void SubmissionGroup::drain()
{
  if (m_next_timeline_value <= 1)
    return; // nothing submitted yet

  // Single wait for the highest submitted value drains all slots
  m_timeline->wait(m_next_timeline_value - 1);
}

vk::Semaphore SubmissionGroup::timeline_semaphore() const
{
  return m_timeline->get();
}

uint64_t SubmissionGroup::latest_signal_value() const
{
  return (m_next_timeline_value > 1) ? (m_next_timeline_value - 1) : 0;
}

const vk::Semaphore* SubmissionGroup::present_semaphore(uint32_t slot) const
{
  return m_present_semaphores[slot]->semaphore();
}

void SubmissionGroup::record_commands(
  vk::CommandBuffer cmd, uint32_t slot_index, FrameResources& /*frame*/)
{
  // Base implementation: just call the record function (no render pass)
  m_record_fn(cmd, slot_index);
}

} // namespace vkwave
