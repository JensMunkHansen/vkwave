#include "scene.h"
#include "app.h"

#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/pipeline/pipeline.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <filesystem>

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

void Scene::create_hdr_images(const vkwave::Device& device,
  vk::Extent2D extent, uint32_t count)
{
  hdr_images.clear();
  for (uint32_t i = 0; i < count; ++i)
  {
    hdr_images.emplace_back(device, kHdrFormat, extent,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
      fmt::format("hdr_image_{}", i));
  }
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
// Fallback textures (1x1 pixels for missing material slots)
// ---------------------------------------------------------------------------

void Scene::create_fallback_textures()
{
  // White (base color, AO fallback)
  const uint8_t white[] = { 255, 255, 255, 255 };
  fallback_white = std::make_unique<vkwave::Texture>(
    m_app->device, "fallback_white", white, 1, 1, false);

  // Flat normal (128,128,255 = (0,0,1) in tangent space)
  const uint8_t flat_normal[] = { 128, 128, 255, 255 };
  fallback_normal = std::make_unique<vkwave::Texture>(
    m_app->device, "fallback_normal", flat_normal, 1, 1, true);

  // Default metallic-roughness: non-metallic, medium roughness
  // glTF: G=roughness, B=metallic → (0, 128, 0, 255) = roughness 0.5, metallic 0
  const uint8_t default_mr[] = { 0, 128, 0, 255 };
  fallback_mr = std::make_unique<vkwave::Texture>(
    m_app->device, "fallback_mr", default_mr, 1, 1, true);

  // Black (emissive fallback)
  const uint8_t black[] = { 0, 0, 0, 255 };
  fallback_black = std::make_unique<vkwave::Texture>(
    m_app->device, "fallback_black", black, 1, 1, false);
}

// ---------------------------------------------------------------------------
// Write PBR texture descriptors to execution group
// ---------------------------------------------------------------------------

void Scene::write_pbr_descriptors(vkwave::ExecutionGroup& group)
{
  auto tex_or = [](const std::unique_ptr<vkwave::Texture>& tex,
                   const std::unique_ptr<vkwave::Texture>& fallback)
    -> const vkwave::Texture&
  {
    return tex ? *tex : *fallback;
  };

  const bool use_scene = !gltf_scene.materials.empty();
  const uint32_t mat_count = use_scene
    ? static_cast<uint32_t>(gltf_scene.materials.size()) : 1;

  // Set 1: per-material textures (one descriptor set per material)
  for (uint32_t m = 0; m < mat_count; ++m)
  {
    auto& mat_base  = use_scene ? gltf_scene.materials[m].baseColorTexture         : gltf_model.baseColorTexture;
    auto& mat_norm  = use_scene ? gltf_scene.materials[m].normalTexture            : gltf_model.normalTexture;
    auto& mat_mr    = use_scene ? gltf_scene.materials[m].metallicRoughnessTexture : gltf_model.metallicRoughnessTexture;
    auto& mat_emis  = use_scene ? gltf_scene.materials[m].emissiveTexture          : gltf_model.emissiveTexture;
    auto& mat_ao    = use_scene ? gltf_scene.materials[m].aoTexture                : gltf_model.aoTexture;

    auto& base = tex_or(mat_base, fallback_white);
    group.write_image_descriptor(1, "baseColorTexture", m, base.image_view(), base.sampler());

    auto& norm = tex_or(mat_norm, fallback_normal);
    group.write_image_descriptor(1, "normalTexture", m, norm.image_view(), norm.sampler());

    auto& mr = tex_or(mat_mr, fallback_mr);
    group.write_image_descriptor(1, "metallicRoughnessTexture", m, mr.image_view(), mr.sampler());

    auto& emis = tex_or(mat_emis, fallback_black);
    group.write_image_descriptor(1, "emissiveTexture", m, emis.image_view(), emis.sampler());

    auto& ao = tex_or(mat_ao, fallback_white);
    group.write_image_descriptor(1, "aoTexture", m, ao.image_view(), ao.sampler());
  }

  // Set 2: per-scene IBL textures (single descriptor set)
  group.write_image_descriptor(2, "brdfLUT",
    ibl->brdf_lut_view(), ibl->brdf_lut_sampler());
  group.write_image_descriptor(2, "irradianceMap",
    ibl->irradiance_view(), ibl->irradiance_sampler());
  group.write_image_descriptor(2, "prefilterMap",
    ibl->prefiltered_view(), ibl->prefiltered_sampler());
}

// ---------------------------------------------------------------------------
// Scene construction
// ---------------------------------------------------------------------------

Scene::Scene(App& app)
  : m_app(&app)
{
  // Load model or fall back to cube
  if (!app.config.model_path.empty())
  {
    if (!std::filesystem::exists(app.config.model_path))
    {
      spdlog::warn("Model file not found: {} -- using default cube", app.config.model_path);
    }
    else
    {
      spdlog::info("Loading glTF scene: {}", app.config.model_path);
      gltf_scene = vkwave::load_gltf_scene(app.device, app.config.model_path);
      if (!gltf_scene.mesh)
      {
        spdlog::warn("Scene load returned no mesh, falling back to single-material loader");
        gltf_model = vkwave::load_gltf_model(app.device, app.config.model_path);
      }
    }
  }

  if (!gltf_scene.mesh && !gltf_model.mesh)
  {
    spdlog::info("Using default cube mesh");
    cube_mesh = vkwave::Mesh::create_cube(app.device);
  }

  const vkwave::Mesh* active_mesh = gltf_scene.mesh
    ? gltf_scene.mesh.get()
    : (gltf_model.mesh ? gltf_model.mesh.get() : cube_mesh.get());

  // Create IBL resources
  if (!app.config.hdr_path.empty())
  {
    if (!std::filesystem::exists(app.config.hdr_path))
    {
      spdlog::warn("HDR file not found: {} -- using default neutral IBL", app.config.hdr_path);
      ibl = std::make_unique<vkwave::IBL>(app.device);
    }
    else
    {
      spdlog::info("Loading HDR environment: {}", app.config.hdr_path);
      ibl = std::make_unique<vkwave::IBL>(app.device, app.config.hdr_path);
    }
  }
  else
  {
    spdlog::info("Using default neutral IBL environment");
    ibl = std::make_unique<vkwave::IBL>(app.device);
  }

  // Determine which hdr_paths index matches the initial hdr_path
  current_hdr_index = -1;
  for (int i = 0; i < static_cast<int>(app.config.hdr_paths.size()); ++i)
  {
    if (app.config.hdr_paths[i] == app.config.hdr_path)
    {
      current_hdr_index = i;
      break;
    }
  }
  if (current_hdr_index < 0 && !app.config.hdr_paths.empty())
    current_hdr_index = 0;

  // Create fallback textures for missing material slots
  create_fallback_textures();

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

  // PBR pass: renders to offscreen HDR image
  auto pbr_spec = vkwave::PBRPass::pipeline_spec();
  pbr_spec.existing_renderpass = scene_renderpass;

  // One HDR image per offscreen slot -- eliminates WAW hazard
  const uint32_t os_depth = app.swapchain.image_count();
  create_hdr_images(app.device, app.swapchain.extent(), os_depth);

  std::vector<vk::ImageView> hdr_views;
  hdr_views.reserve(os_depth);
  for (uint32_t i = 0; i < os_depth; ++i)
    hdr_views.push_back(hdr_images[i].image_view());

  auto& pbr_group = app.graph.add_offscreen_group("pbr", pbr_spec, kHdrFormat, kDebug);
  pbr_group.set_color_views(hdr_views);
  pbr_pass.group = &pbr_group;
  pbr_pass.mesh = active_mesh;
  if (gltf_scene.mesh && !gltf_scene.primitives.empty())
  {
    pbr_pass.primitives = gltf_scene.primitives.data();
    pbr_pass.primitive_count = static_cast<uint32_t>(gltf_scene.primitives.size());
    pbr_pass.materials = gltf_scene.materials.data();
    pbr_pass.material_count = static_cast<uint32_t>(gltf_scene.materials.size());
  }
  pbr_group.set_record_fn([this](vk::CommandBuffer cmd, uint32_t /*frame_index*/) {
    pbr_pass.record(cmd);
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
    cg.write_image_descriptor(0, "hdrImage", frame_index,
      hdr_images[slot].image_view(), hdr_sampler);
    composite_pass.record(cmd);
  });

  // Gate present at display refresh rate -- offscreen runs at GPU speed,
  // present only acquires/presents when the display needs a new frame.
  float refresh = static_cast<float>(app.window.refresh_rate());
  if (refresh > 0.0f)
    comp_group.set_gating(vkwave::GatingMode::wall_clock, refresh);

  // ImGui overlay -- records into the composite group's command buffer
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
    auto depth = m_app->graph.offscreen_depth();
    create_hdr_images(m_app->device, extent, depth);

    // Update PBR group's color views (one HDR image per slot)
    std::vector<vk::ImageView> views;
    views.reserve(depth);
    for (uint32_t i = 0; i < depth; ++i)
      views.push_back(hdr_images[i].image_view());
    auto& cg = static_cast<vkwave::ExecutionGroup&>(m_app->graph.offscreen_group(0));
    cg.set_color_views(views);
  });

  // Descriptor set frequency layout:
  //   Set 0: per-frame UBO (ring-buffered, count = swapchain images) — default
  //   Set 1: per-material textures (count = number of materials)
  //   Set 2: per-scene IBL (count = 1)
  {
    uint32_t mat_count = gltf_scene.mesh && !gltf_scene.materials.empty()
      ? static_cast<uint32_t>(gltf_scene.materials.size()) : 1;
    pbr_group.set_descriptor_count(1, mat_count);
    pbr_group.set_descriptor_count(2, 1);
  }

  // Build all frame resources
  app.graph.build(app.swapchain);

  // Write texture descriptors (after build allocates descriptor sets)
  write_pbr_descriptors(pbr_group);

  // Write HDR image descriptor to composite pass
  comp_group.write_image_descriptor(0, "hdrImage", hdr_images[0].image_view(), hdr_sampler);

  // Overlay framebuffers reference swapchain image views -- create after build
  imgui->create_frame_resources(app.swapchain, app.swapchain.image_count());
}

Scene::~Scene()
{
  m_app->device.device().waitIdle();

  imgui.reset();
  hdr_images.clear();

  auto dev = m_app->device.device();
  if (hdr_sampler)
    dev.destroySampler(hdr_sampler);
  if (scene_renderpass)
    dev.destroyRenderPass(scene_renderpass);
  if (composite_renderpass)
    dev.destroyRenderPass(composite_renderpass);
}

void Scene::switch_ibl(const std::string& hdr_path)
{
  // Drain GPU — in-flight descriptor sets still reference old IBL views
  m_app->graph.drain();

  // Create new IBL (old one destroyed by unique_ptr reassignment)
  if (hdr_path.empty() || !std::filesystem::exists(hdr_path))
  {
    if (!hdr_path.empty())
      spdlog::warn("HDR file not found: {} -- using neutral IBL", hdr_path);
    ibl = std::make_unique<vkwave::IBL>(m_app->device);
  }
  else
  {
    spdlog::info("Switching IBL to: {}", hdr_path);
    ibl = std::make_unique<vkwave::IBL>(m_app->device, hdr_path);
  }

  // Rewrite IBL descriptors (set 2: per-scene globals)
  auto& pbr_group = static_cast<vkwave::ExecutionGroup&>(m_app->graph.offscreen_group(0));
  pbr_group.write_image_descriptor(2, "brdfLUT", ibl->brdf_lut_view(), ibl->brdf_lut_sampler());
  pbr_group.write_image_descriptor(2, "irradianceMap", ibl->irradiance_view(), ibl->irradiance_sampler());
  pbr_group.write_image_descriptor(2, "prefilterMap", ibl->prefiltered_view(), ibl->prefiltered_sampler());
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
  comp_group.write_image_descriptor(0, "hdrImage", hdr_images[0].image_view(), hdr_sampler);

  // Re-write PBR texture descriptors (descriptor sets were recreated)
  auto& pbr_group = static_cast<vkwave::ExecutionGroup&>(m_app->graph.offscreen_group(0));
  write_pbr_descriptors(pbr_group);
}

void Scene::update(vkwave::RenderGraph& graph)
{
  camera.set_aspect_ratio(
    static_cast<float>(graph.offscreen_group(0).extent().width) /
    static_cast<float>(graph.offscreen_group(0).extent().height));
  pbr_pass.view_projection = camera.view_projection_matrix();
  pbr_pass.cam_position = camera.position();
  pbr_pass.model = glm::rotate(glm::mat4(1.0f),
    glm::radians(45.0f) * graph.elapsed_time(),
    glm::vec3(0.0f, 1.0f, 0.0f));
  pbr_pass.time = graph.elapsed_time();
}
