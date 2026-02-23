#pragma once

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace vkwave
{

class Device;
class Swapchain;

/// Lightweight ImGui overlay — not a SubmissionGroup.
///
/// Owns the ImGui context, a load-preserving render pass, and per-slot
/// framebuffers. Records draw commands into an externally-provided
/// command buffer via record(), so no extra vkQueueSubmit is needed.
///
/// Wire to the last submission group's post-record callback:
///
///   group.set_post_record_fn([&](vk::CommandBuffer cmd, uint32_t slot) {
///       overlay.record(cmd, slot);
///   });
///
/// To disable ImGui, simply don't set the callback.
class ImGuiOverlay
{
  const Device& m_device;
  vk::RenderPass m_renderpass{ VK_NULL_HANDLE };
  std::vector<vk::Framebuffer> m_framebuffers;
  vk::Extent2D m_extent{};

public:
  ImGuiOverlay(vk::Instance instance,
               const Device& device,
               GLFWwindow* window,
               vk::Format swapchain_format,
               uint32_t image_count,
               bool debug);
  ~ImGuiOverlay();

  ImGuiOverlay(const ImGuiOverlay&) = delete;
  ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;

  /// Create/recreate per-slot framebuffers (call on build and resize).
  void create_frame_resources(const Swapchain& swapchain, uint32_t count);
  void destroy_frame_resources();

  /// Call once per frame before building ImGui UI.
  void new_frame();

  /// Record ImGui draw commands into an existing command buffer.
  /// Begins its own render pass (load-preserving), records draw data, ends it.
  void record(vk::CommandBuffer cmd, uint32_t slot_index);
};

} // namespace vkwave
