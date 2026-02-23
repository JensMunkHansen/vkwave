#include "scene.h"
#include "app.h"

#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/pipeline/pipeline.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>

static constexpr bool kDebug =
#ifdef VKWAVE_DEBUG
  true;
#else
  false;
#endif

// ---------------------------------------------------------------------------
// Offscreen HDR image management
// ---------------------------------------------------------------------------

void Scene::create_hdr_images(vk::Extent2D extent, uint32_t count)
{
  auto dev = m_app->device.device();

  for (uint32_t i = 0; i < count; ++i)
  {
    OffscreenImage img{};

    // Create image
    vk::ImageCreateInfo image_info{};
    image_info.imageType = vk::ImageType::e2D;
    image_info.extent = vk::Extent3D{ extent.width, extent.height, 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = kHdrFormat;
    image_info.tiling = vk::ImageTiling::eOptimal;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage =
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
    image_info.sharingMode = vk::SharingMode::eExclusive;
    image_info.samples = vk::SampleCountFlagBits::e1;

    img.image = dev.createImage(image_info);

    // Allocate memory
    auto mem_reqs = dev.getImageMemoryRequirements(img.image);
    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = m_app->device.find_memory_type(
      mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    img.memory = dev.allocateMemory(alloc_info);
    dev.bindImageMemory(img.image, img.memory, 0);

    // Create image view
    vk::ImageViewCreateInfo view_info{};
    view_info.image = img.image;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = kHdrFormat;
    view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    auto name = fmt::format("hdr_image_{}", i);
    m_app->device.create_image_view(view_info, &img.view, name);
    m_app->device.set_debug_name(
      reinterpret_cast<uint64_t>(static_cast<VkImage>(img.image)),
      vk::ObjectType::eImage, name);

    hdr_images.push_back(img);
  }

  spdlog::debug("Created {} HDR images ({}x{})", count, extent.width, extent.height);
}

void Scene::destroy_hdr_images()
{
  auto dev = m_app->device.device();
  for (auto& img : hdr_images)
  {
    if (img.view)
      dev.destroyImageView(img.view);
    if (img.image)
      dev.destroyImage(img.image);
    if (img.memory)
      dev.freeMemory(img.memory);
  }
  hdr_images.clear();
}

void Scene::create_sampler()
{
  vk::SamplerCreateInfo info{};
  info.magFilter = vk::Filter::eLinear;
  info.minFilter = vk::Filter::eLinear;
  info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  info.anisotropyEnable = VK_FALSE;
  info.unnormalizedCoordinates = VK_FALSE;
  info.mipmapMode = vk::SamplerMipmapMode::eLinear;

  hdr_sampler = m_app->device.device().createSampler(info);
}

// ---------------------------------------------------------------------------
// Scene construction
// ---------------------------------------------------------------------------

Scene::Scene(App& app)
  : m_app(&app)
{
  cube_mesh = vkwave::Mesh::create_cube(app.device);

  camera.set_position(0.0f, 1.5f, 3.0f);
  camera.set_focal_point(0.0f, 0.0f, 0.0f);
  camera.set_aspect_ratio(
    static_cast<float>(app.swapchain.extent().width) /
    static_cast<float>(app.swapchain.extent().height));

  // Create render passes (owned by Scene, shared with ExecutionGroups)
  scene_renderpass = vkwave::make_scene_renderpass(
    app.device.device(), kHdrFormat, vk::Format::eD32Sfloat, kDebug);
  composite_renderpass = vkwave::make_composite_renderpass(
    app.device.device(), app.swapchain.image_format(), kDebug);

  // Create sampler (persistent across resize)
  create_sampler();

  // Cube pass: renders to offscreen HDR image
  auto cube_spec = vkwave::CubePass::pipeline_spec();
  cube_spec.existing_renderpass = scene_renderpass;

  // One HDR image per offscreen slot — eliminates WAW hazard
  const uint32_t os_depth = app.swapchain.image_count();
  create_hdr_images(app.swapchain.extent(), os_depth);

  std::vector<vk::ImageView> hdr_views;
  hdr_views.reserve(os_depth);
  for (uint32_t i = 0; i < os_depth; ++i)
    hdr_views.push_back(hdr_images[i].view);

  auto& cube_group = app.graph.add_offscreen_group("cube", cube_spec, kHdrFormat, kDebug);
  cube_group.set_color_views(hdr_views);
  cube_pass.group = &cube_group;
  cube_pass.mesh = cube_mesh.get();
  cube_group.set_record_fn([this](vk::CommandBuffer cmd, uint32_t /*frame_index*/) {
    cube_pass.record(cmd);
  });

  // Composite pass: samples HDR image, tonemaps, writes to swapchain
  auto comp_spec = vkwave::CompositePass::pipeline_spec();
  comp_spec.existing_renderpass = composite_renderpass;

  auto& comp_group = app.graph.set_present_group(
    "composite", comp_spec, app.swapchain.image_format(), kDebug);
  composite_pass.group = &comp_group;
  comp_group.set_record_fn([this](vk::CommandBuffer cmd, uint32_t frame_index) {
    // Update descriptor to sample from the latest offscreen HDR image.
    // begin_frame() already waited for this slot's previous use, so the
    // descriptor set is safe to update.
    auto slot = m_app->graph.last_offscreen_slot();
    auto& cg = static_cast<vkwave::ExecutionGroup&>(m_app->graph.present_group());
    cg.write_image_descriptor(0, 0, frame_index,
      hdr_images[slot].view, hdr_sampler);
    composite_pass.record(cmd);
  });

  // Gate present at display refresh rate — offscreen runs at GPU speed,
  // present only acquires/presents when the display needs a new frame.
  float refresh = static_cast<float>(app.window.refresh_rate());
  if (refresh > 0.0f)
    comp_group.set_gating(vkwave::GatingMode::wall_clock, refresh);

  // ImGui overlay — records into the composite group's command buffer
  imgui = std::make_unique<vkwave::ImGuiOverlay>(
    app.instance.instance(), app.device,
    app.window.get(), app.swapchain.image_format(),
    app.swapchain.image_count(), kDebug);

  auto* overlay = imgui.get();
  comp_group.set_post_record_fn([overlay](vk::CommandBuffer cmd, uint32_t slot) {
    overlay->record(cmd, slot);
  });

  // Set resize callback so the graph can recreate offscreen images on resize
  app.graph.set_resize_fn([this](vk::Extent2D extent) {
    destroy_hdr_images();

    auto depth = m_app->graph.offscreen_depth();
    create_hdr_images(extent, depth);

    // Update cube group's color views (one HDR image per slot)
    std::vector<vk::ImageView> views;
    views.reserve(depth);
    for (uint32_t i = 0; i < depth; ++i)
      views.push_back(hdr_images[i].view);
    auto& cg = static_cast<vkwave::ExecutionGroup&>(m_app->graph.offscreen_group(0));
    cg.set_color_views(views);
  });

  // Build all frame resources
  app.graph.build(app.swapchain);

  // Write HDR image descriptor to composite pass (after build allocates descriptor sets)
  // Use the first HDR image view — since the scene renderpass transitions to
  // eShaderReadOnlyOptimal, the composite pass can sample it
  comp_group.write_image_descriptor(0, 0, hdr_images[0].view, hdr_sampler);

  // Overlay framebuffers reference swapchain image views — create after build
  imgui->create_frame_resources(app.swapchain, app.swapchain.image_count());
}

Scene::~Scene()
{
  m_app->device.device().waitIdle();

  imgui.reset();
  destroy_hdr_images();

  auto dev = m_app->device.device();
  if (hdr_sampler)
    dev.destroySampler(hdr_sampler);
  if (scene_renderpass)
    dev.destroyRenderPass(scene_renderpass);
  if (composite_renderpass)
    dev.destroyRenderPass(composite_renderpass);
}

void Scene::resize(const vkwave::Swapchain& swapchain)
{
  if (imgui)
  {
    imgui->destroy_frame_resources();
    imgui->create_frame_resources(swapchain, swapchain.image_count());
  }

  // Update composite pass's HDR image descriptor after resize rebuilt everything
  auto& comp_group = static_cast<vkwave::ExecutionGroup&>(m_app->graph.present_group());
  comp_group.write_image_descriptor(0, 0, hdr_images[0].view, hdr_sampler);
}

void Scene::update(vkwave::RenderGraph& graph)
{
  camera.set_aspect_ratio(
    static_cast<float>(graph.offscreen_group(0).extent().width) /
    static_cast<float>(graph.offscreen_group(0).extent().height));
  cube_pass.view_projection = camera.view_projection_matrix();
  cube_pass.model = glm::rotate(glm::mat4(1.0f),
    glm::radians(45.0f) * graph.elapsed_time(),
    glm::vec3(0.0f, 1.0f, 0.0f));
  cube_pass.time = graph.elapsed_time();
}
