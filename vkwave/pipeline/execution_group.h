#pragma once

#include <vkwave/core/buffer.h>
#include <vkwave/core/depth_stencil_attachment.h>
#include <vkwave/pipeline/shader_reflection.h>
#include <vkwave/pipeline/submission_group.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
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

/// Groups passes that share a VkPipeline and render pass.
///
/// Inherits frame submission machinery from SubmissionGroup.
///
/// Owns the pipeline, pipeline layout, render pass, descriptor set layouts,
/// and (if depth testing is enabled) the depth buffer. These are created
/// from a PipelineSpec at construction time and destroyed in the destructor.
///
/// UBOs/SSBOs are auto-created from shader reflection: any descriptor binding
/// with blockSize > 0 gets ring-buffered buffers and automatic descriptor
/// writes. Access them via ubo(set, binding).
class ExecutionGroup : public SubmissionGroup
{
  using BufferHandle = size_t;

  struct BufferSpec {
    std::string name;
    vk::DeviceSize size;
    vk::BufferUsageFlags usage;
    vk::MemoryPropertyFlags properties;
  };

  // Owned immutable pipeline state (created from PipelineSpec)
  vk::Pipeline       m_pipeline{ VK_NULL_HANDLE };
  vk::PipelineLayout m_layout{ VK_NULL_HANDLE };
  vk::RenderPass     m_renderpass{ VK_NULL_HANDLE };
  std::vector<vk::DescriptorSetLayout> m_descriptor_layouts;
  bool m_depth_enabled{ false };
  bool m_owns_renderpass{ true };
  vk::Format m_depth_format{ vk::Format::eD32Sfloat };

  // Reflected descriptor set info (stored at construction for auto-creating UBOs)
  std::vector<DescriptorSetInfo> m_reflected_sets;

  // Depth buffer (owned, size-dependent — created/destroyed with frame resources)
  std::unique_ptr<DepthStencilAttachment> m_depth_buffer;

  // Ring-buffered managed buffers (auto-created from reflection)
  std::vector<BufferSpec> m_buffer_specs;
  std::vector<std::vector<std::unique_ptr<Buffer>>> m_buffers; // [handle][slot]

  // Map from (set, binding) -> BufferHandle for reflected UBOs/SSBOs
  std::map<std::pair<uint32_t, uint32_t>, BufferHandle> m_binding_to_handle;

  // Descriptor set management
  vk::DescriptorPool m_descriptor_pool{ VK_NULL_HANDLE };
  std::vector<vk::DescriptorSet> m_descriptor_sets; // one per slot

  // Offscreen color views (when set, used instead of swapchain views for framebuffers)
  std::vector<vk::ImageView> m_color_views;

  // Clear values for render pass begin
  std::vector<vk::ClearValue> m_clear_values;

  // Internal: get the buffer for a handle and current slot
  Buffer& buffer(BufferHandle handle);
  Buffer& buffer(BufferHandle handle, uint32_t slot);

  void create_frame_resources_internal(
    vk::Extent2D extent, uint32_t count,
    const std::vector<vk::ImageView>& color_views);

protected:
  /// Record render pass begin, user commands, render pass end.
  void record_commands(vk::CommandBuffer cmd, uint32_t slot_index,
                       FrameResources& frame) override;

public:
  /// Construct from a PipelineSpec: compiles shaders, reflects layout,
  /// creates pipeline + renderpass + descriptor set layouts.
  ExecutionGroup(const Device& device, const std::string& name,
                 const PipelineSpec& spec, vk::Format swapchain_format,
                 bool debug);
  ~ExecutionGroup() override;

  ExecutionGroup(const ExecutionGroup&) = delete;
  ExecutionGroup& operator=(const ExecutionGroup&) = delete;

  /// Set clear values for the render pass begin.
  /// Default: dark gray color clear + depth 1.0.
  void set_clear_values(std::vector<vk::ClearValue> values);

  /// Set offscreen color views (used instead of swapchain views for framebuffers).
  /// Call before create_frame_resources().
  void set_color_views(std::vector<vk::ImageView> views);

  /// Create/recreate size-dependent resources (framebuffers, depth buffer, UBOs, descriptors).
  void create_frame_resources(const Swapchain& swapchain, uint32_t count) override;

  /// Create frame resources for offscreen groups (no swapchain needed).
  void create_frame_resources(vk::Extent2D extent, uint32_t count);

  void destroy_frame_resources() override;

  /// Write a combined image sampler descriptor to all N slots.
  /// Call after create_frame_resources().
  void write_image_descriptor(uint32_t set, uint32_t binding,
                              vk::ImageView view, vk::Sampler sampler,
                              vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

  /// Write a combined image sampler descriptor to a single slot.
  void write_image_descriptor(uint32_t set, uint32_t binding, uint32_t slot,
                              vk::ImageView view, vk::Sampler sampler,
                              vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

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
};

} // namespace vkwave
