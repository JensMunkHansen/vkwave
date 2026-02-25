#pragma once

#include <vkwave/config.h>

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkwave
{

class ShaderReflection;

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

  // Pre-compiled shader modules (when set, skip loading from filepath)
  vk::ShaderModule vertexModule{ VK_NULL_HANDLE };
  vk::ShaderModule fragmentModule{ VK_NULL_HANDLE };

  // Reflection-driven layout (when set, push constants and descriptor set
  // layouts come from reflection instead of manual specification)
  const ShaderReflection* reflection{ nullptr };

  // Vertex input (optional - if empty, no vertex buffers used)
  std::vector<vk::VertexInputBindingDescription> vertexBindings;
  std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

  // Rasterizer options
  bool backfaceCulling{ true };
  bool wireframe{ false };
  bool dynamicCullMode{ false };
  bool dynamicDepthWrite{ false };

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
  std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
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

/// Pure data describing what pipeline to create.
///
/// ExecutionGroup takes this in its constructor, compiles shaders,
/// reflects layout, and creates the pipeline + renderpass internally.
/// The app only fills in this struct — no Vulkan handles needed.
struct PipelineSpec
{
  std::string vertex_shader;    // path to .vert GLSL source
  std::string fragment_shader;  // path to .frag GLSL source

  std::vector<vk::VertexInputBindingDescription> vertex_bindings;
  std::vector<vk::VertexInputAttributeDescription> vertex_attributes;

  bool backface_culling{ true };
  bool wireframe{ false };
  bool depth_test{ false };
  bool depth_write{ true };
  vk::Format depth_format{ vk::Format::eD32Sfloat };
  bool blend{ false };
  bool dynamic_depth_write{ false };
  bool dynamic_cull_mode{ false };
  vk::SampleCountFlagBits msaa_samples{ vk::SampleCountFlagBits::e1 };

  /// Optional: use pre-created render pass instead of auto-creating.
  /// When set, ExecutionGroup passes it through to create_graphics_pipeline().
  vk::RenderPass existing_renderpass{ VK_NULL_HANDLE };
};

class Pipeline
{
};
}
