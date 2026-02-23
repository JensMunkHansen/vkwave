#include <vkwave/pipeline/imgui_overlay.h>

#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/pipeline/framebuffer.h>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

namespace vkwave
{

// ---------------------------------------------------------------------------
// Render pass: color-only, load-preserving overlay, final → presentSrc
// ---------------------------------------------------------------------------

static vk::RenderPass create_overlay_renderpass(
  vk::Device device, vk::Format swapchain_format)
{
  vk::AttachmentDescription color{};
  color.format = swapchain_format;
  color.samples = vk::SampleCountFlagBits::e1;
  color.loadOp = vk::AttachmentLoadOp::eLoad;        // preserve scene
  color.storeOp = vk::AttachmentStoreOp::eStore;
  color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  color.initialLayout = vk::ImageLayout::ePresentSrcKHR;  // from scene pass
  color.finalLayout = vk::ImageLayout::ePresentSrcKHR;    // ready for present

  vk::AttachmentReference color_ref{ 0, vk::ImageLayout::eColorAttachmentOptimal };

  vk::SubpassDescription subpass{};
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;

  // External → subpass: wait for previous pass's color output
  vk::SubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
  dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  dependency.dstAccessMask =
    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

  vk::RenderPassCreateInfo rp_info{};
  rp_info.attachmentCount = 1;
  rp_info.pAttachments = &color;
  rp_info.subpassCount = 1;
  rp_info.pSubpasses = &subpass;
  rp_info.dependencyCount = 1;
  rp_info.pDependencies = &dependency;

  return device.createRenderPass(rp_info);
}

// ---------------------------------------------------------------------------
// ImGuiOverlay
// ---------------------------------------------------------------------------

ImGuiOverlay::ImGuiOverlay(
  vk::Instance instance,
  const Device& device,
  GLFWwindow* window,
  vk::Format swapchain_format,
  uint32_t image_count,
  bool debug)
  : m_device(device)
{
  m_renderpass = create_overlay_renderpass(device.device(), swapchain_format);

  // ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  // Query framebuffer vs window scale for HiDPI
  int win_w, win_h, fb_w, fb_h;
  glfwGetWindowSize(window, &win_w, &win_h);
  glfwGetFramebufferSize(window, &fb_w, &fb_h);
  const float dpi_scale = (win_w > 0) ? static_cast<float>(fb_w) / static_cast<float>(win_w) : 1.0f;

  // Scale all style sizes (padding, rounding, title bar, etc.) to match display
  ImGui::GetStyle().ScaleAllSizes(dpi_scale);

  // Load default font at a DPI-aware size
  ImFontConfig font_cfg;
  font_cfg.SizePixels = 32.0f * dpi_scale;
  ImGui::GetIO().Fonts->AddFontDefault(&font_cfg);

  // GLFW platform backend (installs callbacks, handles input)
  ImGui_ImplGlfw_InitForVulkan(window, true);

  // Vulkan renderer backend — let it create its own descriptor pool
  ImGui_ImplVulkan_InitInfo init{};
  init.Instance = instance;
  init.PhysicalDevice = device.physicalDevice();
  init.Device = device.device();
  init.QueueFamily = device.m_graphics_queue_family_index;
  init.Queue = device.graphics_queue();
  init.RenderPass = m_renderpass;
  init.MinImageCount = image_count;
  init.ImageCount = image_count;
  init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init.DescriptorPoolSize = 16;   // backend creates pool (fonts + user textures)

  ImGui_ImplVulkan_Init(&init);

  // Font atlas upload (uses backend's internal command buffer since v1.91)
  ImGui_ImplVulkan_CreateFontsTexture();

  spdlog::debug("ImGuiOverlay: initialized ({} images in flight{})",
    image_count, debug ? ", debug" : "");
}

ImGuiOverlay::~ImGuiOverlay()
{
  m_device.device().waitIdle();
  destroy_frame_resources();

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (m_renderpass)
    m_device.device().destroyRenderPass(m_renderpass);
}

// ---------------------------------------------------------------------------
// Frame resources (per-slot framebuffers)
// ---------------------------------------------------------------------------

void ImGuiOverlay::create_frame_resources(
  const Swapchain& swapchain, uint32_t count)
{
  m_extent = swapchain.extent();

  framebufferInput fb_in{};
  fb_in.device = m_device.device();
  fb_in.renderpass = m_renderpass;
  fb_in.swapchainExtent = m_extent;

  m_framebuffers = make_framebuffers(fb_in, swapchain, false);

  ImGui_ImplVulkan_SetMinImageCount(count);
}

void ImGuiOverlay::destroy_frame_resources()
{
  for (auto fb : m_framebuffers)
    m_device.device().destroyFramebuffer(fb);
  m_framebuffers.clear();
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void ImGuiOverlay::new_frame()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiOverlay::record(vk::CommandBuffer cmd, uint32_t slot_index)
{
  // Finalize ImGui draw lists (no Vulkan calls, just geometry assembly)
  ImGui::Render();

  vk::RenderPassBeginInfo rp_info{};
  rp_info.renderPass = m_renderpass;
  rp_info.framebuffer = m_framebuffers[slot_index];
  rp_info.renderArea.extent = m_extent;
  // No clear values — loadOp is eLoad

  cmd.beginRenderPass(rp_info, vk::SubpassContents::eInline);

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

  cmd.endRenderPass();
}

} // namespace vkwave
