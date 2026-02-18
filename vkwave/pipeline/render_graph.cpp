#include <vkwave/pipeline/render_graph.h>

#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace vkwave
{

RenderGraph::RenderGraph(const Device& device)
  : m_device(device)
{
}

ExecutionGroup& RenderGraph::add_group(
  const std::string& name,
  vk::Pipeline pipeline, vk::PipelineLayout layout,
  vk::RenderPass renderpass)
{
  m_groups.push_back(std::make_unique<ExecutionGroup>(
    m_device, name, pipeline, layout, renderpass));
  return *m_groups.back();
}

void RenderGraph::build(const Swapchain& swapchain)
{
  m_image_count = swapchain.image_count();

  // Create acquire semaphores (indexed by cpu_frame % N)
  m_acquire_semaphores.clear();
  m_acquire_semaphores.reserve(m_image_count);
  for (uint32_t i = 0; i < m_image_count; ++i)
  {
    m_acquire_semaphores.push_back(std::make_unique<Semaphore>(
      m_device, fmt::format("acquire_sem_{}", i)));
  }

  // Track which image_index each acquire semaphore was last paired with
  m_sem_to_image.assign(m_image_count, UINT32_MAX);

  // Create per-frame resources in each group
  for (auto& group : m_groups)
    group->create_frame_resources(swapchain, m_image_count);
}

RenderGraph::~RenderGraph()
{
  for (auto& group : m_groups)
    group->destroy_frame_resources();
}

void RenderGraph::drain()
{
  for (auto& group : m_groups)
    group->drain();

  // Ensure the presentation engine is done with all semaphores.
  // Timeline waits only cover GPU command completion, not present.
  m_device.device().waitIdle();
}

void RenderGraph::resize(const Swapchain& swapchain)
{
  drain();

  for (auto& group : m_groups)
    group->destroy_frame_resources();

  m_acquire_semaphores.clear();
  m_sem_to_image.clear();

  build(swapchain);
}

RenderGraph::FrameInfo RenderGraph::begin_frame(const Swapchain& swapchain)
{
  const uint32_t sem_index = static_cast<uint32_t>(m_cpu_frame % m_image_count);

  // Ensure the previous submit that consumed this acquire semaphore has completed.
  // Without this wait, the semaphore may still have a pending wait operation from
  // the last frame that used this sem_index, violating the Vulkan spec.
  if (m_sem_to_image[sem_index] != UINT32_MAX)
  {
    for (auto& group : m_groups)
      group->begin_frame(m_sem_to_image[sem_index]);
  }

  auto [result, image_index] = m_device.device().acquireNextImageKHR(
    *swapchain.swapchain(), UINT64_MAX,
    *m_acquire_semaphores[sem_index]->semaphore(), nullptr);

  if (result == vk::Result::eSuboptimalKHR)
  {
    spdlog::debug("Swapchain suboptimal at acquire");
  }

  m_sem_to_image[sem_index] = image_index;
  return { image_index, sem_index };
}

void RenderGraph::end_frame(
  const Swapchain& swapchain, uint32_t image_index,
  vk::Queue /*graphics_queue*/, vk::Queue present_queue)
{
  // Present using the last group's present semaphore
  const auto* present_sem = m_groups.back()->present_semaphore(image_index);

  vk::PresentInfoKHR present{};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = present_sem;
  present.swapchainCount = 1;
  present.pSwapchains = swapchain.swapchain();
  present.pImageIndices = &image_index;

  auto result = present_queue.presentKHR(present);

  if (result == vk::Result::eSuboptimalKHR)
  {
    spdlog::debug("Swapchain suboptimal at present");
  }

  m_cpu_frame++;
}

vk::Semaphore RenderGraph::acquire_semaphore(uint32_t sem_index) const
{
  return *m_acquire_semaphores[sem_index]->semaphore();
}

} // namespace vkwave
