#include <vkwave/pipeline/pbr_pass.h>
#include <vkwave/pipeline/execution_group.h>
#include <vkwave/pipeline/pipeline.h>

#include <vkwave/core/pbr_ubo.h>
#include <vkwave/core/vertex.h>
#include <vkwave/loaders/gltf_loader.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <numeric>
#include <vector>

namespace vkwave
{

// Build the per-draw push constants. Per-material data now lives in the
// GpuMaterial SSBO (indexed by material_index); this carries only the model
// transform, the global UI toggles, and the global preview overrides.
static PbrPushConstants fill_push_constants(
  const PBRContext& ctx, const glm::mat4& model, uint32_t material_index)
{
  PbrPushConstants pc{};
  pc.model = model;
  pc.materialIndex = material_index;
  pc.time = ctx.time;
  pc.debugMode = ctx.debug_mode;

  pc.globalFlags = 0;
  if (ctx.enable_normal_mapping) pc.globalFlags |= PbrFlags::NormalMapping;
  if (ctx.enable_emissive)       pc.globalFlags |= PbrFlags::Emissive;
  if (ctx.enable_clearcoat)      pc.globalFlags |= PbrFlags::Clearcoat;
  if (ctx.enable_anisotropy)     pc.globalFlags |= PbrFlags::Anisotropy;

  // Overrides: a value < 0 means "use the material's authored value".
  pc.metallicOverride            = ctx.metallic_override;
  pc.roughnessOverride           = ctx.roughness_override;
  pc.clearcoatOverride           = ctx.clearcoat_override;
  pc.clearcoatRoughnessOverride  = ctx.clearcoat_roughness_override;
  pc.anisotropyOverride          = ctx.anisotropy_override;
  pc.anisotropyRotationOverride  = ctx.anisotropy_rotation_override;
  pc.mipBias                     = ctx.mip_bias;
  return pc;
}

PipelineSpec PBRPass::pipeline_spec()
{
  auto binding = Vertex::binding_description();
  auto attrs = Vertex::attribute_descriptions();

  PipelineSpec spec{};
  spec.vertex_shader = SHADER_DIR "pbr.vert";
  spec.fragment_shader = SHADER_DIR "pbr.frag";
  spec.vertex_bindings = { binding };
  spec.vertex_attributes = { attrs.begin(), attrs.end() };
#if 0
  spec.wireframe = true;
#endif
  spec.backface_culling = true;
  spec.depth_test = true;
  spec.blend = true;
  spec.dynamic_depth_write = true;
  spec.dynamic_cull_mode = true;
  return spec;
}

void PBRPass::record(vk::CommandBuffer cmd) const
{
  auto* group = ctx->group;

  // Update camera + light UBO for this slot
  PbrUBO ubo_data{};
  ubo_data.viewProj = ctx->view_projection;
  ubo_data.camPos = glm::vec4(ctx->cam_position, 0.0f);
  ubo_data.lightDirection = glm::vec4(glm::normalize(ctx->light_direction), ctx->light_intensity);
  ubo_data.lightColor = glm::vec4(ctx->light_color, 0.0f);
  group->ubo(0, 0).update(&ubo_data, sizeof(ubo_data));

  auto pipeline = group->pipeline();
  auto layout = group->layout();
  auto extent = group->extent();

  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

  vk::Viewport viewport{
    0.f, 0.f,
    static_cast<float>(extent.width),
    static_cast<float>(extent.height),
    0.f, 1.f
  };
  cmd.setViewport(0, viewport);

  vk::Rect2D scissor{ { 0, 0 }, extent };
  cmd.setScissor(0, scissor);

  // Set 0: per-frame UBO (ring-buffered by slot)
  auto ds0 = group->descriptor_set();
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
    0, 1, &ds0, 0, nullptr);

  // Set 2: per-scene IBL (singleton)
  auto ds2 = group->descriptor_set(2, 0);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
    2, 1, &ds2, 0, nullptr);

  ctx->mesh->bind(cmd);

  const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

  auto make_pc = [&](const glm::mat4& m, uint32_t material_index) -> PbrPushConstants
  {
    return fill_push_constants(*ctx, m, material_index);
  };

  auto bind_material = [&](uint32_t material_index) {
    auto ds1 = group->descriptor_set(1, material_index);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
      1, 1, &ds1, 0, nullptr);
  };

  // Legacy single-draw path (backward compatible). Material 0 in the SSBO
  // carries the single-material/cube defaults.
  if (!ctx->primitives || ctx->primitive_count == 0)
  {
    bind_material(0);
    auto pc = make_pc(model, 0);
    cmd.pushConstants(layout, stages, 0, sizeof(PbrPushConstants), &pc);
    cmd.setDepthWriteEnableEXT(VK_TRUE);
    cmd.setCullModeEXT(vk::CullModeFlagBits::eBack);
    ctx->mesh->draw(cmd);
    return;
  }

  // Opaque draws (depth write ON, skip blend materials)
  cmd.setDepthWriteEnableEXT(VK_TRUE);
  uint32_t bound_material = UINT32_MAX;

  for (uint32_t i = 0; i < ctx->primitive_count; ++i)
  {
    auto& prim = ctx->primitives[i];
    if (prim.materialIndex >= ctx->material_count) continue;
    auto& mat = ctx->materials[prim.materialIndex];
    if (mat.alphaMode == AlphaMode::Blend) continue;

    if (prim.materialIndex != bound_material)
    {
      bind_material(prim.materialIndex);
      bound_material = prim.materialIndex;
    }

    cmd.setCullModeEXT(mat.doubleSided
      ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

    auto pc = make_pc(prim.modelMatrix, prim.materialIndex);
    cmd.pushConstants(layout, stages, 0, sizeof(PbrPushConstants), &pc);
    ctx->mesh->draw_indexed(cmd, prim.indexCount, prim.firstIndex, prim.vertexOffset);
  }
}

// ---------------------------------------------------------------------------
// BlendPass — transparent primitives, sorted back-to-front
// ---------------------------------------------------------------------------

void BlendPass::record(vk::CommandBuffer cmd) const
{
  if (!ctx->has_transparent) return;

  auto* group = ctx->group;
  auto layout = group->layout();
  const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

  auto bind_material = [&](uint32_t material_index) {
    auto ds1 = group->descriptor_set(1, material_index);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
      1, 1, &ds1, 0, nullptr);
  };

  auto make_pc = [&](const glm::mat4& m, uint32_t material_index) -> PbrPushConstants
  {
    return fill_push_constants(*ctx, m, material_index);
  };

  // Collect transparent primitive indices
  std::vector<uint32_t> transparent_indices;
  for (uint32_t i = 0; i < ctx->primitive_count; ++i)
  {
    auto& prim = ctx->primitives[i];
    if (prim.materialIndex >= ctx->material_count) continue;
    if (ctx->materials[prim.materialIndex].alphaMode == AlphaMode::Blend)
      transparent_indices.push_back(i);
  }

  if (transparent_indices.empty()) return;

  // Sort back-to-front by distance from camera to world-space centroid
  glm::vec3 cam = ctx->cam_position;
  std::sort(transparent_indices.begin(), transparent_indices.end(),
    [&](uint32_t a, uint32_t b) {
      auto ca = glm::vec3(ctx->primitives[a].modelMatrix * glm::vec4(ctx->primitives[a].centroid, 1.0f));
      auto cb = glm::vec3(ctx->primitives[b].modelMatrix * glm::vec4(ctx->primitives[b].centroid, 1.0f));
      float da = glm::dot(ca - cam, ca - cam);
      float db = glm::dot(cb - cam, cb - cam);
      return da > db;
    });

  cmd.setDepthWriteEnableEXT(VK_FALSE);
  uint32_t bound_material = UINT32_MAX;

  for (uint32_t i : transparent_indices)
  {
    auto& prim = ctx->primitives[i];
    auto& mat = ctx->materials[prim.materialIndex];

    if (prim.materialIndex != bound_material)
    {
      bind_material(prim.materialIndex);
      bound_material = prim.materialIndex;
    }

    cmd.setCullModeEXT(mat.doubleSided
      ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

    auto pc = make_pc(prim.modelMatrix, prim.materialIndex);
    cmd.pushConstants(layout, stages, 0, sizeof(PbrPushConstants), &pc);
    ctx->mesh->draw_indexed(cmd, prim.indexCount, prim.firstIndex, prim.vertexOffset);
  }
}

} // namespace vkwave
