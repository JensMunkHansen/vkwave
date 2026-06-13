#include "scene_pipeline.h"
#include "scene_data.h"
#include "engine.h"

#include <cmath>
#include <vector>

#include <vkwave/core/buffer.h>
#include <vkwave/core/device.h>
#include <vkwave/core/pbr_ubo.h>
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

  // Register the graph-owned, per-slot HDR target (eliminates the WAW hazard)
  // and depth buffer. Per-slot depth lets frames overlap on the GPU yet lets
  // same-frame passes (e.g. opaque + transmission) share one depth buffer.
  hdr_handle = engine.graph->resources().add_color("hdr_image", kHdrFormat,
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
      | vk::ImageUsageFlagBits::eTransferSrc);
  depth_handle = engine.graph->resources().add_depth("scene_depth", kDepthFormat,
    msaa_samples);

  // PBR pass: renders to the graph-owned HDR target + depth
  auto pbr_spec = vkwave::PBRPass::pipeline_spec();
  pbr_spec.existing_renderpass = scene_renderpass;
  pbr_spec.msaa_samples = msaa_samples;

  auto& pbr_grp = engine.graph->add_offscreen_group("pbr", pbr_spec, kHdrFormat, kDebug);
  pbr_grp.set_color_attachment(engine.graph->resources(), hdr_handle);
  pbr_grp.set_depth_attachment(engine.graph->resources(), depth_handle);

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

  // No resize callback needed: the graph owns the HDR pool and recreates it on
  // resize, and the pbr group re-resolves its color attachment from the pool.

  // Declare the pass-dependency edge: composite consumes the pbr group's HDR
  // output, so it waits on the pbr timeline (replaces the graph's implicit
  // "present waits on last offscreen" default).
  comp_grp.depends_on(pbr_grp);

  // Build all frame resources
  engine.graph->build(*engine.swapchain);

  // Write texture descriptors (after build allocates descriptor sets)
  write_pbr_descriptors(data);

  // Write HDR image descriptor to composite pass (slot 0; the per-frame record
  // callback rebinds the correct slot each frame).
  comp_grp.write_image_descriptor(0, "hdrImage",
    engine.graph->resources().color_view(hdr_handle, 0), hdr_sampler);

  // Overlay framebuffers reference swapchain image views -- create after build
  imgui->create_frame_resources(*engine.swapchain, engine.swapchain->image_count());
}

ScenePipeline::~ScenePipeline()
{
  imgui.reset();

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
    // Empty handle used as the "no texture" source for single-material models,
    // which have no clearcoat slots — tex_or() falls back appropriately.
    static const std::unique_ptr<vkwave::Texture> none;

    auto& mat_base  = use_scene ? data.gltf_scene.materials[m].baseColorTexture         : data.gltf_model.baseColorTexture;
    auto& mat_norm  = use_scene ? data.gltf_scene.materials[m].normalTexture            : data.gltf_model.normalTexture;
    auto& mat_mr    = use_scene ? data.gltf_scene.materials[m].metallicRoughnessTexture : data.gltf_model.metallicRoughnessTexture;
    auto& mat_emis  = use_scene ? data.gltf_scene.materials[m].emissiveTexture          : data.gltf_model.emissiveTexture;
    auto& mat_ao    = use_scene ? data.gltf_scene.materials[m].aoTexture                : data.gltf_model.aoTexture;
    auto& mat_cc    = use_scene ? data.gltf_scene.materials[m].clearcoatTexture          : none;
    auto& mat_ccr   = use_scene ? data.gltf_scene.materials[m].clearcoatRoughnessTexture : none;
    auto& mat_ccn   = use_scene ? data.gltf_scene.materials[m].clearcoatNormalTexture    : none;
    auto& mat_ani   = use_scene ? data.gltf_scene.materials[m].anisotropyTexture          : none;

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

    // Clear coat: white fallback => texture multiplier of 1.0 (factor-only path).
    auto& cc = tex_or(mat_cc, data.fallback_white);
    group.write_image_descriptor(1, "clearcoatTexture", m, cc.image_view(), cc.sampler());

    auto& ccr = tex_or(mat_ccr, data.fallback_white);
    group.write_image_descriptor(1, "clearcoatRoughnessTexture", m, ccr.image_view(), ccr.sampler());

    auto& ccn = tex_or(mat_ccn, data.fallback_normal);
    group.write_image_descriptor(1, "clearcoatNormalTexture", m, ccn.image_view(), ccn.sampler());

    // Anisotropy: gated by the AnisotropyMap flag, so fallback content is unused.
    auto& ani = tex_or(mat_ani, data.fallback_white);
    group.write_image_descriptor(1, "anisotropyTexture", m, ani.image_view(), ani.sampler());
  }

  // Set 2: per-scene IBL textures (single descriptor set)
  write_ibl_descriptors(data);

  // Set 2, binding 3: immutable per-material SSBO (shared across all frames)
  upload_material_buffer(data);
}

void ScenePipeline::upload_material_buffer(SceneData& data)
{
  // KHR_texture_transform → precomputed affine (matrix = T * R * S), packed as
  // mat2 (col0.xy, col1.xy) + offset (col2.xy). Matches the GLSL spec example.
  auto pack_xform = [](vkwave::GpuMaterial& gm, int slot, const vkwave::TexTransform& t) {
    const float c = std::cos(t.rotation), s = std::sin(t.rotation);
    glm::mat3 T(1, 0, 0, 0, 1, 0, t.offset.x, t.offset.y, 1);
    glm::mat3 R(c, s, 0, -s, c, 0, 0, 0, 1);
    glm::mat3 S(t.scale.x, 0, 0, 0, t.scale.y, 0, 0, 0, 1);
    glm::mat3 M = T * R * S;
    gm.texXform[2 * slot + 0] = glm::vec4(M[0][0], M[0][1], M[1][0], M[1][1]);
    gm.texXform[2 * slot + 1] = glm::vec4(M[2][0], M[2][1], 0.0f, 0.0f);
  };

  auto to_gpu = [&](const vkwave::SceneMaterial& m) {
    vkwave::GpuMaterial gm{};
    vkwave::set_identity_tex_xforms(gm);
    for (int s = 0; s < 9; ++s)
      pack_xform(gm, s, m.texXforms[s]);
    gm.baseColorFactor = m.baseColorFactor;
    gm.metallicFactor = m.metallicFactor;
    gm.roughnessFactor = m.roughnessFactor;
    gm.clearcoatFactor = m.clearcoatFactor;
    gm.clearcoatRoughnessFactor = m.clearcoatRoughnessFactor;
    gm.anisotropyStrength = m.anisotropyStrength;
    gm.anisotropyRotation = m.anisotropyRotation;
    gm.alphaCutoff = m.alphaCutoff;
    gm.alphaMode = static_cast<uint32_t>(m.alphaMode);
    gm.materialFlags = 0;
    if (m.hasClearcoatNormal)   gm.materialFlags |= vkwave::PbrFlags::ClearcoatNormalMap;
    if (m.hasAnisotropyTexture) gm.materialFlags |= vkwave::PbrFlags::AnisotropyMap;
    gm.uvSets = m.uvSets;
    gm.normalScale = m.normalScale;
    return gm;
  };

  std::vector<vkwave::GpuMaterial> gpu_materials;
  if (data.has_multi_material())
  {
    gpu_materials.reserve(data.gltf_scene.materials.size());
    for (auto& m : data.gltf_scene.materials)
      gpu_materials.push_back(to_gpu(m));
  }
  else
  {
    // Single-material / cube fallback: one default material (white,
    // metallic=1, roughness=1) matching the legacy push-constant defaults.
    vkwave::GpuMaterial gm{};
    vkwave::set_identity_tex_xforms(gm);
    gpu_materials.push_back(gm);
  }

  const vk::DeviceSize bytes =
    gpu_materials.size() * sizeof(vkwave::GpuMaterial);

  // (Re)create the buffer only when missing or too small. Rebuild/resize paths
  // drain the GPU before calling this, so replacing the buffer here is safe.
  if (!material_buffer || material_buffer->size() < bytes)
  {
    material_buffer = std::make_unique<vkwave::Buffer>(
      *m_engine->device, "material_ssbo", bytes,
      vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible
        | vk::MemoryPropertyFlagBits::eHostCoherent);
  }
  material_buffer->update(gpu_materials.data(), bytes);

  // Singleton set 2, binding 3 — one descriptor shared by every frame.
  pbr_group().write_buffer_descriptor(2, 3, material_buffer->buffer(), bytes);
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

  // Point the new group at the graph-owned HDR target (unchanged by MSAA — the
  // single-sample HDR is the resolve/sampled target; MSAA scratch is internal).
  const uint32_t os_depth = m_engine->graph->offscreen_depth();
  auto& pool = m_engine->graph->resources();
  new_grp.set_color_attachment(pool, hdr_handle);

  // Depth must match the new MSAA sample count: update the pool depth and
  // re-allocate it (the caller has drained the GPU), then bind it to the new
  // group before its framebuffers are built.
  pool.set_depth_samples(depth_handle, msaa_samples);
  pool.recreate(*m_engine->device);
  new_grp.set_depth_attachment(pool, depth_handle);

  // Set descriptor counts
  new_grp.set_descriptor_count(1, data.material_count());
  new_grp.set_descriptor_count(2, 1);

  // Rebuild frame resources for this group
  new_grp.create_frame_resources(extent, os_depth);

  // The pool re-alloc gave the HDR target fresh views; rebind composite's sampler.
  composite_group().write_image_descriptor(
    0, "hdrImage", pool.color_view(hdr_handle, 0), hdr_sampler);

  // Re-declare the dependency edge: replace_offscreen_group built a new pbr
  // group, so the composite group's stored predecessor pointer now dangles.
  auto& comp_grp = composite_group();
  comp_grp.clear_dependencies();
  comp_grp.depends_on(new_grp);

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
    0, "hdrImage", m_engine->graph->resources().color_view(hdr_handle, 0), hdr_sampler);

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
