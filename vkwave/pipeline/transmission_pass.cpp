#include <vkwave/pipeline/transmission_pass.h>
#include <vkwave/pipeline/execution_group.h>

#include <vkwave/config.h>
#include <vkwave/core/pbr_ubo.h>
#include <vkwave/core/vertex.h>
#include <vkwave/loaders/gltf_loader.h>

#include <glm/glm.hpp>

namespace vkwave
{

PipelineSpec TransmissionPass::pipeline_spec()
{
  auto binding = Vertex::binding_description();
  auto attrs = Vertex::attribute_descriptions();

  PipelineSpec spec{};
  spec.vertex_shader = SHADER_DIR "pbr.vert";                 // reuse opaque VS
  spec.fragment_shader = SHADER_DIR "transmission.frag";
  spec.vertex_bindings = { binding };
  spec.vertex_attributes = { attrs.begin(), attrs.end() };
  // Glass is double-sided and depth-tested against opaque but does not write
  // depth (single layer). All static state — the record sets no dynamic cull /
  // depth-write, only viewport/scissor.
  // Cull per material (dynamic), mirroring the opaque pass: solid glass is
  // single-sided (cull back → entry surface only, avoids the back hemisphere
  // z-fighting through as a chrome center); thin double-sided glass renders both
  // sides (the shader flips the normal for back faces so Fresnel stays correct).
  spec.backface_culling = true;
  spec.dynamic_cull_mode = true;
  spec.depth_test = true;    // depth-test against the stored opaque depth
  spec.depth_write = false;
  spec.blend = false;
  return spec;
}

void TransmissionPass::record(vk::CommandBuffer cmd) const
{
  if (!ctx->primitives || ctx->primitive_count == 0) return;

  // Update this group's own per-frame UBO (separate from the pbr group's).
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

  vk::Viewport viewport{ 0.f, 0.f,
    static_cast<float>(extent.width), static_cast<float>(extent.height), 0.f, 1.f };
  cmd.setViewport(0, viewport);
  vk::Rect2D scissor{ { 0, 0 }, extent };
  cmd.setScissor(0, scissor);

  // Set 0: per-frame UBO (ring-buffered by slot)
  auto ds0 = group->descriptor_set();
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, 1, &ds0, 0, nullptr);
  // Set 1: per-scene material SSBO (singleton). Transmission has its own compact
  // layout (sets 0,1) — independent of the pbr group's sets.
  auto ds1 = group->descriptor_set(1, 0);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, 1, &ds1, 0, nullptr);

  ctx->mesh->bind(cmd);
  const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

  uint32_t bound_material = UINT32_MAX;
  for (uint32_t i = 0; i < ctx->primitive_count; ++i)
  {
    auto& prim = ctx->primitives[i];
    if (prim.materialIndex >= ctx->material_count) continue;
    if (ctx->materials[prim.materialIndex].transmissionFactor <= 0.0f) continue;

    auto& mat = ctx->materials[prim.materialIndex];

    // Cull per material: single-sided (solid) glass renders front faces only;
    // double-sided (thin) glass renders both (the shader flips the back normal).
    cmd.setCullModeEXT(mat.doubleSided
      ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

    // Set 2: per-material transmission mask texture (rebound on material change).
    if (prim.materialIndex != bound_material)
    {
      auto ds2 = group->descriptor_set(2, prim.materialIndex);
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 2, 1, &ds2, 0, nullptr);
      bound_material = prim.materialIndex;
    }

    auto pc = fill_push_constants(*ctx, prim.modelMatrix, prim.materialIndex);
    cmd.pushConstants(layout, stages, 0, sizeof(PbrPushConstants), &pc);
    ctx->mesh->draw_indexed(cmd, prim.indexCount, prim.firstIndex, prim.vertexOffset);
  }
}

} // namespace vkwave
