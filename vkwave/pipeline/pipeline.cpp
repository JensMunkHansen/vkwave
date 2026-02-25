#include <vkwave/pipeline/pipeline.h>

#include <vkwave/pipeline/shader_reflection.h>
#include <vkwave/pipeline/shaders.h>

#include <iostream>

namespace vkwave
{
vk::PipelineLayout make_pipeline_layout(
  vk::Device device, vk::DescriptorSetLayout descriptorSetLayout,
  const std::vector<vk::PushConstantRange>& pushConstantRanges, bool debug)
{
  vk::PipelineLayoutCreateInfo layoutInfo;
  layoutInfo.flags = vk::PipelineLayoutCreateFlags();

  if (!pushConstantRanges.empty())
  {
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges.data();
  }
  else
  {
    layoutInfo.pushConstantRangeCount = 0;
  }

  if (descriptorSetLayout)
  {
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
  }
  else
  {
    layoutInfo.setLayoutCount = 0;
  }

  try
  {
    return device.createPipelineLayout(layoutInfo);
  }
  catch (vk::SystemError err)
  {
    if (debug)
    {
      std::cout << "Failed to create pipeline layout!" << std::endl;
    }
  }
  return nullptr;
}

vk::RenderPass make_renderpass(vk::Device device, vk::Format swapchainImageFormat,
  bool depthEnabled, vk::Format depthFormat, bool debug,
  vk::SampleCountFlagBits msaaSamples)
{
  const bool msaa = msaaSamples != vk::SampleCountFlagBits::e1;
  std::vector<vk::AttachmentDescription> attachments;

  // Attachment 0: Color attachment
  vk::AttachmentDescription colorAttachment = {};
  colorAttachment.flags = vk::AttachmentDescriptionFlags();
  colorAttachment.format = swapchainImageFormat;
  colorAttachment.samples = msaaSamples;
  colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  colorAttachment.storeOp = msaa ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore;
  colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
  colorAttachment.finalLayout =
    msaa ? vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::ePresentSrcKHR;
  attachments.push_back(colorAttachment);

  // Attachment 1: Depth attachment (optional)
  vk::AttachmentDescription depthAttachment = {};
  if (depthEnabled)
  {
    depthAttachment.flags = vk::AttachmentDescriptionFlags();
    depthAttachment.format = depthFormat;
    depthAttachment.samples = msaaSamples;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    attachments.push_back(depthAttachment);
  }

  // Attachment 2 (MSAA only): Resolve attachment (single-sample swapchain image)
  if (msaa)
  {
    vk::AttachmentDescription resolveAttachment = {};
    resolveAttachment.format = swapchainImageFormat;
    resolveAttachment.samples = vk::SampleCountFlagBits::e1;
    resolveAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
    resolveAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    resolveAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    resolveAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    resolveAttachment.initialLayout = vk::ImageLayout::eUndefined;
    resolveAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
    attachments.push_back(resolveAttachment);
  }

  // Color attachment reference
  vk::AttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

  // Depth attachment reference
  vk::AttachmentReference depthAttachmentRef = {};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // Resolve attachment reference (MSAA only)
  vk::AttachmentReference resolveAttachmentRef = {};
  resolveAttachmentRef.attachment = depthEnabled ? 2u : 1u;
  resolveAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

  // Subpass
  vk::SubpassDescription subpass = {};
  subpass.flags = vk::SubpassDescriptionFlags();
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  if (depthEnabled)
  {
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
  }
  if (msaa)
  {
    subpass.pResolveAttachments = &resolveAttachmentRef;
  }

  // Subpass dependency
  vk::SubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask =
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependency.srcAccessMask = vk::AccessFlagBits::eNone;
  dependency.dstStageMask =
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependency.dstAccessMask =
    vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

  // Create renderpass
  vk::RenderPassCreateInfo renderpassInfo = {};
  renderpassInfo.flags = vk::RenderPassCreateFlags();
  renderpassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderpassInfo.pAttachments = attachments.data();
  renderpassInfo.subpassCount = 1;
  renderpassInfo.pSubpasses = &subpass;
  renderpassInfo.dependencyCount = 1;
  renderpassInfo.pDependencies = &dependency;

  try
  {
    return device.createRenderPass(renderpassInfo);
  }
  catch (vk::SystemError err)
  {
    if (debug)
    {
      std::cout << "Failed to create renderpass!" << std::endl;
    }
  }
  return nullptr;
}

vk::RenderPass make_scene_renderpass(vk::Device device, vk::Format hdrFormat,
  vk::Format depthFormat, bool debug,
  vk::SampleCountFlagBits msaaSamples)
{
  const bool msaa = msaaSamples != vk::SampleCountFlagBits::e1;
  std::vector<vk::AttachmentDescription> attachments;

  // Attachment 0: Color (MSAA or single-sample, HDR format)
  vk::AttachmentDescription colorAttachment{};
  colorAttachment.format = hdrFormat;
  colorAttachment.samples = msaaSamples;
  colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  colorAttachment.storeOp = msaa ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore;
  colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
  colorAttachment.finalLayout = msaa
    ? vk::ImageLayout::eColorAttachmentOptimal
    : vk::ImageLayout::eShaderReadOnlyOptimal;
  attachments.push_back(colorAttachment);

  // Attachment 1: Depth-stencil
  vk::AttachmentDescription depthAttachment{};
  depthAttachment.format = depthFormat;
  depthAttachment.samples = msaaSamples;
  depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
  depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eStore;
  depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
  depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  attachments.push_back(depthAttachment);

  // Attachment 2 (MSAA only): Resolve target (single-sample HDR)
  if (msaa)
  {
    vk::AttachmentDescription resolveAttachment{};
    resolveAttachment.format = hdrFormat;
    resolveAttachment.samples = vk::SampleCountFlagBits::e1;
    resolveAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
    resolveAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    resolveAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    resolveAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    resolveAttachment.initialLayout = vk::ImageLayout::eUndefined;
    resolveAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    attachments.push_back(resolveAttachment);
  }

  vk::AttachmentReference colorRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
  vk::AttachmentReference depthRef{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };
  vk::AttachmentReference resolveRef{ 2, vk::ImageLayout::eColorAttachmentOptimal };

  vk::SubpassDescription subpass{};
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  subpass.pDepthStencilAttachment = &depthRef;
  if (msaa)
  {
    subpass.pResolveAttachments = &resolveRef;
  }

  vk::SubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask =
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependency.srcAccessMask = vk::AccessFlagBits::eNone;
  dependency.dstStageMask =
    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependency.dstAccessMask =
    vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

  vk::RenderPassCreateInfo rpInfo{};
  rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  rpInfo.pAttachments = attachments.data();
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 1;
  rpInfo.pDependencies = &dependency;

  try
  {
    return device.createRenderPass(rpInfo);
  }
  catch (vk::SystemError err)
  {
    if (debug)
      std::cout << "Failed to create scene renderpass!" << std::endl;
  }
  return nullptr;
}

vk::RenderPass make_composite_renderpass(vk::Device device, vk::Format swapchainFormat, bool debug)
{
  // Single color attachment (swapchain image), no depth, no MSAA
  vk::AttachmentDescription colorAttachment{};
  colorAttachment.format = swapchainFormat;
  colorAttachment.samples = vk::SampleCountFlagBits::e1;
  colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
  colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
  colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

  vk::AttachmentReference colorRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };

  vk::SubpassDescription subpass{};
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  vk::SubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependency.srcAccessMask = vk::AccessFlagBits::eNone;
  dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

  vk::RenderPassCreateInfo rpInfo{};
  rpInfo.attachmentCount = 1;
  rpInfo.pAttachments = &colorAttachment;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 1;
  rpInfo.pDependencies = &dependency;

  try
  {
    return device.createRenderPass(rpInfo);
  }
  catch (vk::SystemError err)
  {
    if (debug)
      std::cout << "Failed to create composite renderpass!" << std::endl;
  }
  return nullptr;
}

/**
        Make a graphics pipeline, along with renderpass and pipeline layout

        \param specification the struct holding input data, as specified at the top of the file.
        \param debug whether the system is running in debug mode
        \returns the bundle of data structures created
*/
GraphicsPipelineOutBundle create_graphics_pipeline(
  GraphicsPipelineInBundle& specification, bool debug)
{
  /*
   * Build and return a graphics pipeline based on the given info.
   */

  // The info for the graphics pipeline
  vk::GraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.flags = vk::PipelineCreateFlags();

  // Shader stages, to be populated later
  std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

  // Vertex Input
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.flags = vk::PipelineVertexInputStateCreateFlags();
  vertexInputInfo.vertexBindingDescriptionCount =
    static_cast<uint32_t>(specification.vertexBindings.size());
  vertexInputInfo.pVertexBindingDescriptions = specification.vertexBindings.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
    static_cast<uint32_t>(specification.vertexAttributes.size());
  vertexInputInfo.pVertexAttributeDescriptions = specification.vertexAttributes.data();
  pipelineInfo.pVertexInputState = &vertexInputInfo;

  // Input Assembly
  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
  inputAssemblyInfo.flags = vk::PipelineInputAssemblyStateCreateFlags();
  inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;
  pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;

  // Vertex Shader
  bool ownsVertexShader = false;
  vk::ShaderModule vertexShader;
  if (specification.vertexModule)
  {
    vertexShader = specification.vertexModule;
  }
  else
  {
    if (debug)
      std::cout << "Create vertex shader module" << std::endl;
    vertexShader = vkwave::createModule(specification.vertexFilepath, specification.device, debug);
    ownsVertexShader = true;
  }
  vk::PipelineShaderStageCreateInfo vertexShaderInfo = {};
  vertexShaderInfo.flags = vk::PipelineShaderStageCreateFlags();
  vertexShaderInfo.stage = vk::ShaderStageFlagBits::eVertex;
  vertexShaderInfo.module = vertexShader;
  vertexShaderInfo.pName = "main";
  shaderStages.push_back(vertexShaderInfo);

  // Viewport and Scissor - using dynamic state
  vk::PipelineViewportStateCreateInfo viewportState = {};
  viewportState.flags = vk::PipelineViewportStateCreateFlags();
  viewportState.viewportCount = 1;
  viewportState.pViewports = nullptr; // Dynamic state
  viewportState.scissorCount = 1;
  viewportState.pScissors = nullptr; // Dynamic state
  pipelineInfo.pViewportState = &viewportState;

  // Rasterizer
  vk::PipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.flags = vk::PipelineRasterizationStateCreateFlags();
  rasterizer.depthClampEnable = VK_FALSE; // discard out of bounds fragments, don't clamp them
  rasterizer.rasterizerDiscardEnable = VK_FALSE; // This flag would disable fragment output
  rasterizer.polygonMode = specification.wireframe ? vk::PolygonMode::eLine : vk::PolygonMode::eFill;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode =
    specification.backfaceCulling ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eNone;
  rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
  rasterizer.depthBiasEnable = VK_FALSE; // Depth bias can be useful in shadow maps.
  pipelineInfo.pRasterizationState = &rasterizer;

  // Fragment Shader
  bool ownsFragmentShader = false;
  vk::ShaderModule fragmentShader;
  if (specification.fragmentModule)
  {
    fragmentShader = specification.fragmentModule;
  }
  else
  {
    if (debug)
      std::cout << "Create fragment shader module" << std::endl;
    fragmentShader = vkwave::createModule(specification.fragmentFilepath, specification.device, debug);
    ownsFragmentShader = true;
  }
  vk::PipelineShaderStageCreateInfo fragmentShaderInfo = {};
  fragmentShaderInfo.flags = vk::PipelineShaderStageCreateFlags();
  fragmentShaderInfo.stage = vk::ShaderStageFlagBits::eFragment;
  fragmentShaderInfo.module = fragmentShader;
  fragmentShaderInfo.pName = "main";
  shaderStages.push_back(fragmentShaderInfo);
  // Now both shaders have been made, we can declare them to the pipeline info
  pipelineInfo.stageCount = shaderStages.size();
  pipelineInfo.pStages = shaderStages.data();

  // Multisampling
  vk::PipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.flags = vk::PipelineMultisampleStateCreateFlags();
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = specification.msaaSamples;
  pipelineInfo.pMultisampleState = &multisampling;

  // Depth Stencil
  vk::PipelineDepthStencilStateCreateInfo depthStencil = {};
  depthStencil.depthTestEnable = specification.depthTestEnabled ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable = (specification.depthTestEnabled && specification.depthWriteEnabled) ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = vk::CompareOp::eLess;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  if (specification.stencilWriteEnabled)
  {
    depthStencil.stencilTestEnable = VK_TRUE;
    vk::StencilOpState stencilOp{};
    stencilOp.compareOp = vk::CompareOp::eAlways;
    stencilOp.passOp = vk::StencilOp::eReplace;
    stencilOp.failOp = vk::StencilOp::eKeep;
    stencilOp.depthFailOp = vk::StencilOp::eKeep;
    stencilOp.compareMask = 0xFF;
    stencilOp.writeMask = 0xFF;
    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;
  }

  pipelineInfo.pDepthStencilState = &depthStencil;

  // Color Blend
  vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
  colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
    vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
    vk::ColorComponentFlagBits::eA;
  if (specification.blendEnabled)
  {
    colorBlendAttachment.blendEnable = VK_TRUE;
    // RGB: SRC_ALPHA / ONE_MINUS_SRC_ALPHA / ADD (matches Khronos Sample-Viewer)
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    // Alpha: ONE / ONE_MINUS_SRC_ALPHA / ADD
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
  }
  else
  {
    colorBlendAttachment.blendEnable = VK_FALSE;
  }
  vk::PipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.flags = vk::PipelineColorBlendStateCreateFlags();
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = vk::LogicOp::eCopy;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;
  pipelineInfo.pColorBlendState = &colorBlending;

  // Pipeline Layout - use existing if provided, otherwise create new
  vk::PipelineLayout pipelineLayout;
  std::vector<vk::DescriptorSetLayout> reflectedDSLayouts;
  if (specification.existingPipelineLayout)
  {
    if (debug)
      std::cout << "Using existing Pipeline Layout" << std::endl;
    pipelineLayout = specification.existingPipelineLayout;
  }
  else if (specification.reflection)
  {
    if (debug)
      std::cout << "Create Pipeline Layout (reflection-driven)" << std::endl;

    auto& refl = *specification.reflection;
    reflectedDSLayouts = refl.create_descriptor_set_layouts(specification.device);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pushConstantRangeCount =
      static_cast<uint32_t>(refl.push_constant_ranges().size());
    layoutInfo.pPushConstantRanges = refl.push_constant_ranges().data();
    layoutInfo.setLayoutCount = static_cast<uint32_t>(reflectedDSLayouts.size());
    layoutInfo.pSetLayouts = reflectedDSLayouts.data();

    pipelineLayout = specification.device.createPipelineLayout(layoutInfo);
  }
  else
  {
    if (debug)
      std::cout << "Create Pipeline Layout" << std::endl;
    pipelineLayout =
      make_pipeline_layout(specification.device, specification.descriptorSetLayout,
        specification.pushConstantRanges, debug);
  }
  pipelineInfo.layout = pipelineLayout;

  // Renderpass - use existing if provided, otherwise create new
  vk::RenderPass renderpass;
  bool ownsRenderPass = false;
  if (specification.existingRenderPass)
  {
    if (debug)
    {
      std::cout << "Using existing RenderPass" << std::endl;
    }
    renderpass = specification.existingRenderPass;
  }
  else
  {
    if (debug)
    {
      std::cout << "Create RenderPass" << std::endl;
    }
    renderpass = make_renderpass(specification.device, specification.swapchainImageFormat,
      specification.depthTestEnabled, specification.depthFormat, debug, specification.msaaSamples);
    ownsRenderPass = true;
  }
  pipelineInfo.renderPass = renderpass;
  pipelineInfo.subpass = 0;

  // Dynamic state for viewport and scissor
  std::vector<vk::DynamicState> dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor
  };
  if (specification.dynamicCullMode)
  {
    dynamicStates.push_back(vk::DynamicState::eCullModeEXT);
  }
  if (specification.dynamicDepthWrite)
  {
    dynamicStates.push_back(vk::DynamicState::eDepthWriteEnableEXT);
  }
  if (specification.stencilWriteEnabled)
  {
    dynamicStates.push_back(vk::DynamicState::eStencilReference);
  }
  vk::PipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();
  pipelineInfo.pDynamicState = &dynamicState;

  // Extra stuff
  pipelineInfo.basePipelineHandle = nullptr;

  // Make the Pipeline
  if (debug)
  {
    std::cout << "Create Graphics Pipeline" << std::endl;
  }
  vk::Pipeline graphicsPipeline;
  try
  {
    graphicsPipeline = (specification.device.createGraphicsPipeline(nullptr, pipelineInfo)).value;
  }
  catch (vk::SystemError err)
  {
    if (debug)
    {
      std::cout << "Failed to create Pipeline" << std::endl;
    }
  }

  GraphicsPipelineOutBundle output;
  output.layout = pipelineLayout;
  output.renderpass = renderpass;
  output.pipeline = graphicsPipeline;
  output.descriptorSetLayouts = std::move(reflectedDSLayouts);

  // Clean up shader modules we created (not caller-owned ones)
  if (ownsVertexShader)
    specification.device.destroyShaderModule(vertexShader);
  if (ownsFragmentShader)
    specification.device.destroyShaderModule(fragmentShader);

  return output;
}

}
