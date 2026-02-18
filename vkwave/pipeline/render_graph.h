#pragma once

#include <vkwave/core/semaphore.h>
#include <vkwave/pipeline/execution_group.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vkwave
{

class Device;
class Swapchain;

/// Top-level frame orchestration.
///
/// Owns all ExecutionGroups, acquire semaphores, and the CPU frame counter.
/// Manages swapchain image acquisition and presentation.
class RenderGraph
{
  const Device& m_device;

  std::vector<std::unique_ptr<ExecutionGroup>> m_groups;
  std::vector<std::unique_ptr<Semaphore>> m_acquire_semaphores;
  std::vector<uint32_t> m_sem_to_image; // last image_index paired with each sem_index

  uint64_t m_cpu_frame{ 0 };
  uint32_t m_image_count{ 0 };

public:
  explicit RenderGraph(const Device& device);
  ~RenderGraph();

  /// Add an execution group from a PipelineSpec.
  /// The group compiles shaders, creates pipeline/renderpass/layouts internally.
  ExecutionGroup& add_group(const std::string& name,
                            const PipelineSpec& spec,
                            vk::Format swapchain_format,
                            bool debug);

  /// Allocate per-frame resources for all groups.
  void build(const Swapchain& swapchain);

  /// Drain all groups (for shutdown).
  void drain();

  /// Drain + recreate (for resize).
  void resize(const Swapchain& swapchain);

  /// Acquire swapchain image. Returns image_index and sem_index.
  struct FrameInfo
  {
    uint32_t image_index;
    uint32_t sem_index;
  };
  FrameInfo begin_frame(const Swapchain& swapchain);

  /// Present using the last group's present semaphore.
  void end_frame(const Swapchain& swapchain, uint32_t image_index,
                 vk::Queue graphics_queue, vk::Queue present_queue);

  /// Access acquire semaphore by index (for passing to group submit).
  [[nodiscard]] vk::Semaphore acquire_semaphore(uint32_t sem_index) const;

  /// Access a group by index.
  [[nodiscard]] ExecutionGroup& group(size_t index) { return *m_groups[index]; }

  [[nodiscard]] uint64_t cpu_frame() const { return m_cpu_frame; }
};

} // namespace vkwave
