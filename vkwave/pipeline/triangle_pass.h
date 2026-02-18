#pragma once

#include <vkwave/core/pass.h>
#include <vkwave/core/push_constants.h>

#include <vulkan/vulkan.hpp>

namespace vkwave
{

/// Fullscreen triangle pass: draws a single triangle covering the screen.
///
/// Holds only raw vk:: handles (not owned — graph owns the RAII wrappers).
/// Receives frame_index from the caller, is N-unaware.
///
/// record() binds pipeline, sets viewport/scissor, pushes constants, draws
/// 3 vertices. It does NOT begin/end the render pass or command buffer —
/// the caller manages that.
struct TrianglePass : Pass<TrianglePass>
{
  vk::Pipeline pipeline{ VK_NULL_HANDLE };
  vk::PipelineLayout layout{ VK_NULL_HANDLE };
  vk::RenderPass renderpass{ VK_NULL_HANDLE };
  vk::Extent2D extent{};

  /// Record draw commands into the given command buffer.
  /// Must be called between beginRenderPass and endRenderPass.
  void record(vk::CommandBuffer cmd, const TrianglePushConstants& pc) const;
};

static_assert(std::is_trivially_destructible_v<TrianglePass>,
  "TrianglePass must be trivially destructible");

} // namespace vkwave
