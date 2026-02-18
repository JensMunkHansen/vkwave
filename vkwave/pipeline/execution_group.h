#pragma once

#include <vkwave/core/buffer.h>
#include <vkwave/core/frame_resources.h>
#include <vkwave/core/semaphore.h>
#include <vkwave/core/timeline_semaphore.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vkwave
{

class Device;
class Swapchain;

/// Callback type for recording commands into a command buffer.
using RecordFn = std::function<void(vk::CommandBuffer, vk::Framebuffer, vk::Extent2D)>;

using BufferHandle = size_t;

struct BufferSpec {
  std::string name;
  vk::DeviceSize size;
  vk::BufferUsageFlags usage;
  vk::MemoryPropertyFlags properties;
};

/// Groups passes that share a VkPipeline and render pass.
///
/// Each group owns N ring-buffered FrameResources and one timeline semaphore.
/// The timeline semaphore replaces per-slot fences: begin_frame() waits on the
/// timeline value from the last submission in this slot, and submit() advances
/// the counter. drain() waits on the maximum submitted value (single call).
///
/// Binary present semaphores are owned per-slot for WSI (vkQueuePresentKHR).
class ExecutionGroup
{
  const Device& m_device;
  std::string m_name;

  // Shared immutable state (not owned — created externally)
  vk::Pipeline       m_pipeline{ VK_NULL_HANDLE };
  vk::PipelineLayout m_layout{ VK_NULL_HANDLE };
  vk::RenderPass     m_renderpass{ VK_NULL_HANDLE };

  // Per-slot ring-buffered resources
  std::vector<FrameResources> m_frames;
  std::vector<std::unique_ptr<Semaphore>> m_present_semaphores;
  vk::Extent2D m_extent{};

  // Timeline semaphore (replaces per-slot fences)
  std::unique_ptr<TimelineSemaphore> m_timeline;
  std::vector<uint64_t> m_slot_timeline_values;
  uint64_t m_next_timeline_value{ 1 };

  // Ring-buffered managed buffers
  std::vector<BufferSpec> m_buffer_specs;
  std::vector<std::vector<std::unique_ptr<Buffer>>> m_buffers; // [handle][slot]
  uint32_t m_current_slot{0};

  // Record callback
  RecordFn m_record_fn;

public:
  ExecutionGroup(const Device& device, const std::string& name,
                 vk::Pipeline pipeline, vk::PipelineLayout layout,
                 vk::RenderPass renderpass);

  void set_record_fn(RecordFn fn);

  /// Create/recreate size-dependent resources (framebuffers, command pools).
  void create_frame_resources(const Swapchain& swapchain, uint32_t count);
  void destroy_frame_resources();

  /// Wait on the timeline for this slot (blocks until GPU finishes previous use).
  void begin_frame(uint32_t slot_index);

  /// Reset command pool, record via callback, submit.
  /// Signals both the timeline semaphore and the binary present semaphore.
  void submit(uint32_t slot_index,
              vk::Semaphore wait_acquire,
              vk::Queue queue);

  /// Drain all slots via single vkWaitSemaphores call.
  void drain();

  /// Declare a ring-buffered buffer. Returns a handle for later access.
  /// Must be called before create_frame_resources().
  BufferHandle add_buffer(const std::string& name, vk::DeviceSize size,
                          vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

  /// Get the buffer for the current slot (valid inside record callback).
  Buffer& buffer(BufferHandle handle);

  /// Get the buffer for an explicit slot (valid after begin_frame).
  Buffer& buffer(BufferHandle handle, uint32_t slot);

  [[nodiscard]] vk::RenderPass renderpass() const { return m_renderpass; }
  [[nodiscard]] const vk::Semaphore* present_semaphore(uint32_t slot) const;
};

} // namespace vkwave
