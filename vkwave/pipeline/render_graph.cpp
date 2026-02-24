#include <vkwave/pipeline/render_graph.h>

#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cassert>

namespace vkwave
{

RenderGraph::RenderGraph(const Device& device)
  : m_device(device)
{
}

RenderGraph::~RenderGraph()
{
}

ExecutionGroup& RenderGraph::add_offscreen_group(
  const std::string& name,
  const PipelineSpec& spec,
  vk::Format color_format,
  bool debug)
{
  auto eg = std::make_unique<ExecutionGroup>(
    m_device, name, spec, color_format, debug);
  eg->set_signal_present(false); // offscreen groups don't need binary present semaphores
  auto& ref = *eg;
  m_offscreen_groups.push_back(std::move(eg));
  return ref;
}

ExecutionGroup& RenderGraph::set_present_group(
  const std::string& name,
  const PipelineSpec& spec,
  vk::Format swapchain_format,
  bool debug)
{
  m_present_group = std::make_unique<ExecutionGroup>(
    m_device, name, spec, swapchain_format, debug);
  return static_cast<ExecutionGroup&>(*m_present_group);
}

uint32_t RenderGraph::offscreen_depth() const
{
  return m_offscreen_depth > 0 ? m_offscreen_depth : m_swapchain_image_count;
}

void RenderGraph::build(const Swapchain& swapchain)
{
  m_swapchain_image_count = swapchain.image_count();
  const uint32_t os_depth = offscreen_depth();

  // Create acquire semaphores (indexed by cpu_frame % swapchain_count)
  m_acquire_semaphores.clear();
  m_acquire_semaphores.reserve(m_swapchain_image_count);
  for (uint32_t i = 0; i < m_swapchain_image_count; ++i)
  {
    m_acquire_semaphores.push_back(std::make_unique<Semaphore>(
      m_device, fmt::format("acquire_sem_{}", i)));
  }
  m_sem_to_image.assign(m_swapchain_image_count, UINT32_MAX);

  // Create offscreen group resources (independent of swapchain)
  for (auto& group : m_offscreen_groups)
  {
    auto* eg = static_cast<ExecutionGroup*>(group.get());
    eg->create_frame_resources(swapchain.extent(), os_depth);
  }

  // Create present group resources (uses swapchain views)
  if (m_present_group)
    m_present_group->create_frame_resources(swapchain, m_swapchain_image_count);
}

void RenderGraph::drain()
{
  for (auto& group : m_offscreen_groups)
    group->drain();
  if (m_present_group)
    m_present_group->drain();

  m_device.device().waitIdle();
}

void RenderGraph::resize(const Swapchain& swapchain)
{
  drain();

  // Destroy present group resources
  if (m_present_group)
    m_present_group->destroy_frame_resources();

  // Destroy offscreen group resources
  for (auto& group : m_offscreen_groups)
    group->destroy_frame_resources();

  m_acquire_semaphores.clear();
  m_sem_to_image.clear();

  // Let the app recreate offscreen images at the new size
  if (m_resize_fn)
    m_resize_fn(swapchain.extent());

  build(swapchain);
}

bool RenderGraph::render_frame(const Swapchain& swapchain)
{
  // Update wall-clock timing
  auto now = Clock::now();
  if (m_first_frame)
  {
    m_start_time = now;
    m_prev_frame_time = now;
    m_first_frame = false;
  }
  m_delta_time = std::chrono::duration<float>(now - m_prev_frame_time).count();
  m_elapsed_time = std::chrono::duration<float>(now - m_start_time).count();
  m_prev_frame_time = now;

  const uint32_t os_depth = offscreen_depth();

  // 1. Submit offscreen groups (every frame, no acquire/present)
  uint32_t offscreen_slot = static_cast<uint32_t>(m_cpu_frame % os_depth);
  m_last_offscreen_slot = offscreen_slot;

  for (auto& group : m_offscreen_groups)
  {
    group->begin_frame(offscreen_slot);
    group->submit(offscreen_slot, {}, m_device.graphics_queue(), m_elapsed_time);
  }

  // 2. Conditionally submit present group
  assert(m_present_group && "present group must be set before render_frame()");

  const bool is_fifo = (swapchain.present_mode() == vk::PresentModeKHR::eFifo);
  const bool should_present = m_present_group->should_submit(m_elapsed_time, is_fifo);

  if (should_present)
  {
    const uint32_t sem_index = static_cast<uint32_t>(m_cpu_frame % m_swapchain_image_count);

    // Ensure the previous submit that consumed this acquire semaphore has completed
    if (m_sem_to_image[sem_index] != UINT32_MAX)
      m_present_group->begin_frame(m_sem_to_image[sem_index]);

    vk::Result acq_result;
    uint32_t image_index;
    try
    {
      auto [result, idx] = m_device.device().acquireNextImageKHR(
        *swapchain.swapchain(), UINT64_MAX,
        *m_acquire_semaphores[sem_index]->semaphore(), nullptr);
      acq_result = result;
      image_index = idx;
    }
    catch (vk::OutOfDateKHRError&)
    {
      m_cpu_frame++;
      return false;
    }

    if (acq_result == vk::Result::eSuboptimalKHR)
      spdlog::debug("Swapchain suboptimal at acquire");

    m_sem_to_image[sem_index] = image_index;

    // Present group waits on: acquire binary semaphore + offscreen timeline
    std::vector<SemaphoreWait> present_waits;
    present_waits.push_back({ *m_acquire_semaphores[sem_index]->semaphore(), 0 });

    if (!m_offscreen_groups.empty())
    {
      auto& last_offscreen = *m_offscreen_groups.back();
      auto tl_value = last_offscreen.latest_signal_value();
      if (tl_value > 0)
        present_waits.push_back({ last_offscreen.timeline_semaphore(), tl_value });
    }

    m_present_group->begin_frame(image_index, true);
    m_present_group->submit(image_index, present_waits,
                             m_device.graphics_queue(), m_elapsed_time);

    // Present
    const auto* present_sem = m_present_group->present_semaphore(image_index);

    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = present_sem;
    present.swapchainCount = 1;
    present.pSwapchains = swapchain.swapchain();
    present.pImageIndices = &image_index;

    try
    {
      auto result = m_device.present_queue().presentKHR(present);
      if (result == vk::Result::eSuboptimalKHR)
        spdlog::debug("Swapchain suboptimal at present");
    }
    catch (vk::OutOfDateKHRError&)
    {
      m_cpu_frame++;
      return false;
    }
  }

  m_cpu_frame++;
  return true;
}

} // namespace vkwave
