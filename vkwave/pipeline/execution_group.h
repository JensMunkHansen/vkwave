#pragma once

#include <vkwave/core/buffer.h>
#include <vkwave/core/depth_stencil_attachment.h>
#include <vkwave/core/frame_resources.h>
#include <vkwave/core/semaphore.h>
#include <vkwave/core/timeline_semaphore.h>
#include <vkwave/pipeline/shader_reflection.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vkwave
{

class Device;
class Swapchain;
struct PipelineSpec;

/// Callback type for recording pass commands into a command buffer.
///
/// The group calls beginRenderPass/endRenderPass around this callback.
/// The callback receives the command buffer (with active render pass)
/// and the frame_index for indexing into ring-buffered resources.
using RecordFn = std::function<void(vk::CommandBuffer cmd, uint32_t frame_index)>;

/// Groups passes that share a VkPipeline and render pass.
///
/// Owns the pipeline, pipeline layout, render pass, descriptor set layouts,
/// and (if depth testing is enabled) the depth buffer. These are created
/// from a PipelineSpec at construction time and destroyed in the destructor.
///
/// UBOs/SSBOs are auto-created from shader reflection: any descriptor binding
/// with blockSize > 0 gets ring-buffered buffers and automatic descriptor
/// writes. Access them via ubo(set, binding).
///
/// Each group owns N ring-buffered FrameResources and one timeline semaphore.
/// The timeline semaphore replaces per-slot fences: begin_frame() waits on the
/// timeline value from the last submission in this slot, and submit() advances
/// the counter. drain() waits on the maximum submitted value (single call).
///
/// Binary present semaphores are owned per-slot for WSI (vkQueuePresentKHR).
class ExecutionGroup
{
  using BufferHandle = size_t;

  struct BufferSpec {
    std::string name;
    vk::DeviceSize size;
    vk::BufferUsageFlags usage;
    vk::MemoryPropertyFlags properties;
  };

  const Device& m_device;
  std::string m_name;
  bool m_debug{ false };

  // Owned immutable pipeline state (created from PipelineSpec)
  vk::Pipeline       m_pipeline{ VK_NULL_HANDLE };
  vk::PipelineLayout m_layout{ VK_NULL_HANDLE };
  vk::RenderPass     m_renderpass{ VK_NULL_HANDLE };
  std::vector<vk::DescriptorSetLayout> m_descriptor_layouts;
  bool m_depth_enabled{ false };
  vk::Format m_depth_format{ vk::Format::eD32Sfloat };

  // Reflected descriptor set info (stored at construction for auto-creating UBOs)
  std::vector<DescriptorSetInfo> m_reflected_sets;

  // Depth buffer (owned, size-dependent — created/destroyed with frame resources)
  std::unique_ptr<DepthStencilAttachment> m_depth_buffer;

  // Per-slot ring-buffered resources
  std::vector<FrameResources> m_frames;
  std::vector<std::unique_ptr<Semaphore>> m_present_semaphores;
  vk::Extent2D m_extent{};

  // Timeline semaphore (replaces per-slot fences)
  std::unique_ptr<TimelineSemaphore> m_timeline;
  std::vector<uint64_t> m_slot_timeline_values;
  uint64_t m_next_timeline_value{ 1 };

  // Ring-buffered managed buffers (auto-created from reflection)
  std::vector<BufferSpec> m_buffer_specs;
  std::vector<std::vector<std::unique_ptr<Buffer>>> m_buffers; // [handle][slot]
  uint32_t m_current_slot{0};

  // Map from (set, binding) -> BufferHandle for reflected UBOs/SSBOs
  std::map<std::pair<uint32_t, uint32_t>, BufferHandle> m_binding_to_handle;

  // Descriptor set management
  vk::DescriptorPool m_descriptor_pool{ VK_NULL_HANDLE };
  std::vector<vk::DescriptorSet> m_descriptor_sets; // one per slot

  // Clear values for render pass begin
  std::vector<vk::ClearValue> m_clear_values;

  // Record callback
  RecordFn m_record_fn;

  // Internal: get the buffer for a handle and current slot
  Buffer& buffer(BufferHandle handle);
  Buffer& buffer(BufferHandle handle, uint32_t slot);

public:
  /// Construct from a PipelineSpec: compiles shaders, reflects layout,
  /// creates pipeline + renderpass + descriptor set layouts.
  ExecutionGroup(const Device& device, const std::string& name,
                 const PipelineSpec& spec, vk::Format swapchain_format,
                 bool debug);
  ~ExecutionGroup();

  ExecutionGroup(const ExecutionGroup&) = delete;
  ExecutionGroup& operator=(const ExecutionGroup&) = delete;

  void set_record_fn(RecordFn fn);

  /// Set clear values for the render pass begin.
  /// Default: dark gray color clear + depth 1.0.
  void set_clear_values(std::vector<vk::ClearValue> values);

  /// Create/recreate size-dependent resources (framebuffers, command pools, depth buffer).
  /// Auto-creates ring-buffered UBOs/SSBOs from reflection and writes descriptor sets.
  void create_frame_resources(const Swapchain& swapchain, uint32_t count);
  void destroy_frame_resources();

  /// Wait on the timeline for this slot (blocks until GPU finishes previous use).
  void begin_frame(uint32_t slot_index);

  /// Reset command pool, record via callback (with render pass begin/end), submit.
  /// Signals both the timeline semaphore and the binary present semaphore.
  void submit(uint32_t slot_index,
              vk::Semaphore wait_acquire,
              vk::Queue queue);

  /// Drain all slots via single vkWaitSemaphores call.
  void drain();

  /// Get the UBO/SSBO buffer for a given (set, binding) at the current slot.
  /// Valid inside the record callback after begin_frame().
  Buffer& ubo(uint32_t set, uint32_t binding);

  /// Get the UBO/SSBO buffer for a given (set, binding) at an explicit slot.
  Buffer& ubo(uint32_t set, uint32_t binding, uint32_t slot);

  /// Get the descriptor set for the current slot (valid inside record callback).
  [[nodiscard]] vk::DescriptorSet descriptor_set() const;

  /// Get the descriptor set for an explicit slot.
  [[nodiscard]] vk::DescriptorSet descriptor_set(uint32_t slot) const;

  [[nodiscard]] vk::Pipeline pipeline() const { return m_pipeline; }
  [[nodiscard]] vk::PipelineLayout layout() const { return m_layout; }
  [[nodiscard]] vk::RenderPass renderpass() const { return m_renderpass; }
  [[nodiscard]] vk::Extent2D extent() const { return m_extent; }
  [[nodiscard]] const vk::Semaphore* present_semaphore(uint32_t slot) const;
};

} // namespace vkwave
