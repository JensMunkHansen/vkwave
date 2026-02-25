#include "scene_pipeline.h"
#include "scene_data.h"
#include "engine.h"

#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/pipeline/pipeline.h>
#include <vkwave/pipeline/pbr_pass.h>
#include <vkwave/pipeline/composite_pass.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

static constexpr bool kDebug =
#ifdef VKWAVE_DEBUG
  true;
#else
  false;
#endif

// ---------------------------------------------------------------------------
// HDR image management
// ---------------------------------------------------------------------------

void ScenePipeline::create_hdr_images(vk::Extent2D extent, uint32_t count)
{
  hdr_images.clear();
  for (uint32_t i = 0; i < count; ++i)
  {
    hdr_images.emplace_back(*m_engine->device, kHdrFormat, extent,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
        | vk::ImageUsageFlagBits::eTransferSrc,
      fmt::format("hdr_image_{}", i));
  }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ScenePipeline::ScenePipeline(Engine& engine, SceneData& data,
                             vk::SampleCountFlagBits msaa)
  : msaa_samples(msaa)
  , m_engine(&engine)
{
  // Create render passes (owned by ScenePipeline, shared with ExecutionGroups)
  scene_renderpass = vkwave::make_scene_renderpass(
    engine.device->device(), kHdrFormat, vk::Format::eD32Sfloat, kDebug, msaa_samples);
  composite_renderpass = vkwave::make_composite_renderpass(
    engine.device->device(), engine.swapchain->image_format(), kDebug);

  // Create sampler (persistent across resize)
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
    hdr_sampler = engine.device->device().createSampler(info);
  }

  // One HDR image per offscreen slot -- eliminates WAW hazard
  const uint32_t os_depth = engine.swapchain->image_count();
  create_hdr_images(engine.swapchain->extent(), os_depth);

  // PBR pass: renders to offscreen HDR image
  auto pbr_spec = vkwave::PBRPass::pipeline_spec();
  pbr_spec.existing_renderpass = scene_renderpass;
  pbr_spec.msaa_samples = msaa_samples;

  std::vector<vk::ImageView> hdr_views;
  hdr_views.reserve(os_depth);
  for (uint32_t i = 0; i < os_depth; ++i)
    hdr_views.push_back(hdr_images[i].image_view());

  auto& pbr_grp = engine.graph->add_offscreen_group("pbr", pbr_spec, kHdrFormat, kDebug);
  pbr_grp.set_color_views(hdr_views);

  // Descriptor set frequency layout:
  //   Set 0: per-frame UBO (ring-buffered, count = swapchain images) -- default
  //   Set 1: per-material textures (count = number of materials)
  //   Set 2: per-scene IBL (count = 1)
  pbr_grp.set_descriptor_count(1, data.material_count());
  pbr_grp.set_descriptor_count(2, 1);

  // Composite pass: samples HDR image, tonemaps, writes to swapchain
  auto comp_spec = vkwave::CompositePass::pipeline_spec();
  comp_spec.existing_renderpass = composite_renderpass;

  auto& comp_grp = engine.graph->set_present_group(
    "composite", comp_spec, engine.swapchain->image_format(), kDebug);

  // Gate present based on present mode: FIFO modes are vsync'd by the driver,
  // so always present. Non-FIFO modes gate at display refresh to avoid
  // unnecessary acquire/present overhead.
  auto pm = engine.swapchain->present_mode();
  bool fifo = (pm == vk::PresentModeKHR::eFifo || pm == vk::PresentModeKHR::eFifoRelaxed);
  if (fifo)
    comp_grp.set_gating(vkwave::GatingMode::always);
  else
  {
    float refresh = static_cast<float>(engine.window.refresh_rate());
    if (refresh > 0.0f)
      comp_grp.set_gating(vkwave::GatingMode::wall_clock, refresh);
  }

  // ImGui overlay -- records into the composite group's command buffer
  imgui = std::make_unique<vkwave::ImGuiOverlay>(
    engine.instance.instance(), *engine.device,
    engine.window.get(), engine.swapchain->image_format(),
    engine.swapchain->image_count(), kDebug);

  // Set resize callback so the graph can recreate offscreen images on resize
  engine.graph->set_resize_fn([this](vk::Extent2D extent) {
    auto depth = m_engine->graph->offscreen_depth();
    create_hdr_images(extent, depth);

    std::vector<vk::ImageView> views;
    views.reserve(depth);
    for (uint32_t i = 0; i < depth; ++i)
      views.push_back(hdr_images[i].image_view());
    auto& grp = static_cast<vkwave::ExecutionGroup&>(m_engine->graph->offscreen_group(0));
    grp.set_color_views(views);
  });

  // Build all frame resources
  engine.graph->build(*engine.swapchain);

  // Write texture descriptors (after build allocates descriptor sets)
  write_pbr_descriptors(data);

  // Write HDR image descriptor to composite pass
  comp_grp.write_image_descriptor(0, "hdrImage", hdr_images[0].image_view(), hdr_sampler);

  // Overlay framebuffers reference swapchain image views -- create after build
  imgui->create_frame_resources(*engine.swapchain, engine.swapchain->image_count());
}

ScenePipeline::~ScenePipeline()
{
  imgui.reset();
  hdr_images.clear();

  auto dev = m_engine->device->device();
  if (hdr_sampler)
    dev.destroySampler(hdr_sampler);
  if (scene_renderpass)
    dev.destroyRenderPass(scene_renderpass);
  if (composite_renderpass)
    dev.destroyRenderPass(composite_renderpass);
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void ScenePipeline::write_pbr_descriptors(SceneData& data)
{
  auto tex_or = [](const std::unique_ptr<vkwave::Texture>& tex,
                   const std::unique_ptr<vkwave::Texture>& fallback)
    -> const vkwave::Texture&
  {
    return tex ? *tex : *fallback;
  };

  auto& group = pbr_group();
  const bool use_scene = data.has_multi_material();
  const uint32_t mat_count = data.material_count();

  // Set 1: per-material textures (one descriptor set per material)
  for (uint32_t m = 0; m < mat_count; ++m)
  {
    auto& mat_base  = use_scene ? data.gltf_scene.materials[m].baseColorTexture         : data.gltf_model.baseColorTexture;
    auto& mat_norm  = use_scene ? data.gltf_scene.materials[m].normalTexture            : data.gltf_model.normalTexture;
    auto& mat_mr    = use_scene ? data.gltf_scene.materials[m].metallicRoughnessTexture : data.gltf_model.metallicRoughnessTexture;
    auto& mat_emis  = use_scene ? data.gltf_scene.materials[m].emissiveTexture          : data.gltf_model.emissiveTexture;
    auto& mat_ao    = use_scene ? data.gltf_scene.materials[m].aoTexture                : data.gltf_model.aoTexture;

    auto& base = tex_or(mat_base, data.fallback_white);
    group.write_image_descriptor(1, "baseColorTexture", m, base.image_view(), base.sampler());

    auto& norm = tex_or(mat_norm, data.fallback_normal);
    group.write_image_descriptor(1, "normalTexture", m, norm.image_view(), norm.sampler());

    auto& mr = tex_or(mat_mr, data.fallback_mr);
    group.write_image_descriptor(1, "metallicRoughnessTexture", m, mr.image_view(), mr.sampler());

    auto& emis = tex_or(mat_emis, data.fallback_black);
    group.write_image_descriptor(1, "emissiveTexture", m, emis.image_view(), emis.sampler());

    auto& ao = tex_or(mat_ao, data.fallback_white);
    group.write_image_descriptor(1, "aoTexture", m, ao.image_view(), ao.sampler());
  }

  // Set 2: per-scene IBL textures (single descriptor set)
  write_ibl_descriptors(data);
}

void ScenePipeline::write_ibl_descriptors(SceneData& data)
{
  auto& group = pbr_group();
  group.write_image_descriptor(2, "brdfLUT",
    data.ibl->brdf_lut_view(), data.ibl->brdf_lut_sampler());
  group.write_image_descriptor(2, "irradianceMap",
    data.ibl->irradiance_view(), data.ibl->irradiance_sampler());
  group.write_image_descriptor(2, "prefilterMap",
    data.ibl->prefiltered_view(), data.ibl->prefiltered_sampler());
}

void ScenePipeline::rebuild_pbr_descriptors(SceneData& data)
{
  auto& grp = pbr_group();

  // Save extent before destroying (group resets to zero)
  const auto extent = grp.extent();

  grp.destroy_frame_resources();
  grp.set_descriptor_count(1, data.material_count());
  grp.set_descriptor_count(2, 1);
  grp.create_frame_resources(extent, m_engine->graph->offscreen_depth());

  write_pbr_descriptors(data);
}

// ---------------------------------------------------------------------------
// MSAA rebuild
// ---------------------------------------------------------------------------

void ScenePipeline::rebuild_for_msaa(vk::SampleCountFlagBits new_samples,
                                     SceneData& data)
{
  msaa_samples = new_samples;

  // Capture extent before destroying (the new group starts with zero extent)
  auto& old_group = pbr_group();
  const auto extent = old_group.extent();
  old_group.destroy_frame_resources();

  // Destroy old scene render pass
  auto dev = m_engine->device->device();
  if (scene_renderpass)
  {
    dev.destroyRenderPass(scene_renderpass);
    scene_renderpass = VK_NULL_HANDLE;
  }

  // Create new scene render pass with updated MSAA
  scene_renderpass = vkwave::make_scene_renderpass(
    dev, kHdrFormat, vk::Format::eD32Sfloat, kDebug, msaa_samples);

  // Create new PBR pipeline spec with updated render pass + MSAA
  auto pbr_spec = vkwave::PBRPass::pipeline_spec();
  pbr_spec.existing_renderpass = scene_renderpass;
  pbr_spec.msaa_samples = msaa_samples;

  // Replace the offscreen group (new pipeline + render pass reference)
  auto& new_grp = m_engine->graph->replace_offscreen_group(
    0, "pbr", pbr_spec, kHdrFormat, kDebug);

  // Update color views from existing HDR images
  const uint32_t os_depth = m_engine->graph->offscreen_depth();
  std::vector<vk::ImageView> hdr_views;
  hdr_views.reserve(os_depth);
  for (uint32_t i = 0; i < os_depth; ++i)
    hdr_views.push_back(hdr_images[i].image_view());
  new_grp.set_color_views(hdr_views);

  // Set descriptor counts
  new_grp.set_descriptor_count(1, data.material_count());
  new_grp.set_descriptor_count(2, 1);

  // Rebuild frame resources for this group
  new_grp.create_frame_resources(extent, os_depth);

  // Rewrite descriptors
  write_pbr_descriptors(data);

  spdlog::info("MSAA changed to {}x", static_cast<int>(msaa_samples));
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void ScenePipeline::resize(const vkwave::Swapchain& swapchain, SceneData& data)
{
  if (imgui)
  {
    imgui->destroy_frame_resources();
    imgui->create_frame_resources(swapchain, swapchain.image_count());
  }

  // Update composite pass's HDR image descriptor after resize rebuilt everything
  composite_group().write_image_descriptor(
    0, "hdrImage", hdr_images[0].image_view(), hdr_sampler);

  // Re-write PBR texture descriptors (descriptor sets were recreated)
  write_pbr_descriptors(data);
}

// ---------------------------------------------------------------------------
// Group accessors
// ---------------------------------------------------------------------------

vkwave::ExecutionGroup& ScenePipeline::pbr_group()
{
  return static_cast<vkwave::ExecutionGroup&>(m_engine->graph->offscreen_group(0));
}

vkwave::ExecutionGroup& ScenePipeline::composite_group()
{
  return static_cast<vkwave::ExecutionGroup&>(m_engine->graph->present_group());
}
