#pragma once

#include <vkwave/config.h>

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkwave
{
/**
        holds the data structures used to create a pipeline
*/
struct GraphicsPipelineInBundle
{
  vk::Device device;
  std::string vertexFilepath;
  std::string fragmentFilepath;
  vk::Extent2D swapchainExtent;
  vk::Format swapchainImageFormat;
  vk::DescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE }; // Optional

  // Vertex input (optional - if empty, no vertex buffers used)
  std::vector<vk::VertexInputBindingDescription> vertexBindings;
  std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

  // Rasterizer options
  bool backfaceCulling{ true };
  bool dynamicCullMode{ false };

  // Depth testing
  bool depthTestEnabled{ false };
  bool depthWriteEnabled{ true };
  vk::Format depthFormat{ vk::Format::eD32Sfloat };

  // Stencil write (for SSS masking — writes stencil ref per draw)
  bool stencilWriteEnabled{ false };

  // Blending
  bool blendEnabled{ false };

  // Optional: use existing render pass instead of creating new one
  vk::RenderPass existingRenderPass{ VK_NULL_HANDLE };

  // Optional: use existing pipeline layout instead of creating new one
  vk::PipelineLayout existingPipelineLayout{ VK_NULL_HANDLE };

  // Push constant ranges
  std::vector<vk::PushConstantRange> pushConstantRanges;

  // MSAA sample count (e1 = no MSAA)
  vk::SampleCountFlagBits msaaSamples{ vk::SampleCountFlagBits::e1 };
};

/**
        Used for returning the pipeline, along with associated data structures,
        after creation.
*/
struct GraphicsPipelineOutBundle
{
  vk::PipelineLayout layout;
  vk::RenderPass renderpass;
  vk::Pipeline pipeline;
};

vk::PipelineLayout make_pipeline_layout(vk::Device device,
  vk::DescriptorSetLayout descriptorSetLayout,
  const std::vector<vk::PushConstantRange>& pushConstantRanges, bool debug);

vk::RenderPass make_renderpass(vk::Device device, vk::Format swapchainImageFormat,
  bool depthEnabled, vk::Format depthFormat, bool debug,
  vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1);

/// Scene render pass: renders to an HDR color target (with optional MSAA + resolve).
/// The resolved HDR image ends in eShaderReadOnlyOptimal for sampling by the composite pass.
vk::RenderPass make_scene_renderpass(vk::Device device, vk::Format hdrFormat,
  vk::Format depthFormat, bool debug,
  vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1);

/// Composite render pass: single swapchain color attachment, no depth.
vk::RenderPass make_composite_renderpass(vk::Device device, vk::Format swapchainFormat, bool debug);

GraphicsPipelineOutBundle create_graphics_pipeline(
  GraphicsPipelineInBundle& specification, bool debug);

class Pipeline
{
};
}
