#pragma once

#include <vkwave/core/pass.h>
#include <vkwave/pipeline/pipeline.h>

#include <vulkan/vulkan.hpp>

namespace vkwave
{

class ExecutionGroup;

/// Composite pass: fullscreen triangle that samples an HDR image,
/// applies tonemapping + gamma, and writes to the swapchain.
///
/// Holds only raw handles — graph owns the RAII wrappers.
struct CompositePass : Pass<CompositePass>
{
  ExecutionGroup* group{ nullptr };
  float exposure{ 1.0f };
  int debug_mode{ 0 };

  /// Returns the PipelineSpec (fullscreen.vert + composite.frag, no vertex input, no depth).
  /// The caller must set existing_renderpass on the returned spec before passing to add_group().
  static PipelineSpec pipeline_spec();

  /// Record: bind pipeline, push constants, bind descriptor set, draw 3 vertices.
  void record(vk::CommandBuffer cmd) const;
};

static_assert(std::is_trivially_destructible_v<CompositePass>,
  "CompositePass must be trivially destructible");

} // namespace vkwave
