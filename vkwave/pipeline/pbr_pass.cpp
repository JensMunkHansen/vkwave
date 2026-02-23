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

PipelineSpec PBRPass::pipeline_spec()
{
  auto binding = Vertex::binding_description();
  auto attrs = Vertex::attribute_descriptions();

  PipelineSpec spec{};
  spec.vertex_shader = SHADER_DIR "pbr.vert";
  spec.fragment_shader = SHADER_DIR "pbr.frag";
  spec.vertex_bindings = { binding };
  spec.vertex_attributes = { attrs.begin(), attrs.end() };
  spec.backface_culling = true;
  spec.depth_test = true;
  spec.blend = true;
  spec.dynamic_depth_write = true;
  spec.dynamic_cull_mode = true;
  return spec;
}

void PBRPass::record(vk::CommandBuffer cmd) const
{
  // Update camera + light UBO for this slot
  PbrUBO ubo_data{};
  ubo_data.viewProj = view_projection;
  ubo_data.camPos = glm::vec4(cam_position, 0.0f);
  ubo_data.lightDirection = glm::vec4(glm::normalize(light_direction), light_intensity);
  ubo_data.lightColor = glm::vec4(light_color, 0.0f);
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
  auto ds0 = group->descriptor_set();  // set 0, current slot
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
    0, 1, &ds0, 0, nullptr);

  // Set 2: per-scene IBL (singleton)
  auto ds2 = group->descriptor_set(2, 0);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
    2, 1, &ds2, 0, nullptr);

  mesh->bind(cmd);

  const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

  // Build base push constants (shared across all draws)
  auto make_pc = [&](const glm::mat4& m, glm::vec4 bcf, float met, float rough,
                      uint32_t alpha_mode, float alpha_cutoff) -> PbrPushConstants
  {
    PbrPushConstants pc{};
    pc.model = m;
    pc.baseColorFactor = bcf;
    pc.metallicFactor = met;
    pc.roughnessFactor = rough;
    pc.time = time;
    pc.debugMode = debug_mode;
    pc.flags = 0;
    if (enable_normal_mapping) pc.flags |= PbrFlags::NormalMapping;
    if (enable_emissive)       pc.flags |= PbrFlags::Emissive;
    pc.alphaMode = alpha_mode;
    pc.alphaCutoff = alpha_cutoff;
    return pc;
  };

  // Helper: bind material descriptor set (set 1, indexed by material)
  auto bind_material = [&](uint32_t material_index) {
    auto ds1 = group->descriptor_set(1, material_index);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
      1, 1, &ds1, 0, nullptr);
  };

  // Legacy single-draw path (backward compatible)
  if (!primitives || primitive_count == 0)
  {
    bind_material(0);
    auto pc = make_pc(model, base_color_factor, metallic_factor, roughness_factor, 0, 0.5f);
    cmd.pushConstants(layout, stages, 0, sizeof(PbrPushConstants), &pc);
    cmd.setDepthWriteEnableEXT(VK_TRUE);
    cmd.setCullModeEXT(vk::CullModeFlagBits::eBack);
    mesh->draw(cmd);
    return;
  }

  // Multi-primitive path: opaque pass then transparent pass

  // --- Opaque pass (depth write ON) ---
  cmd.setDepthWriteEnableEXT(VK_TRUE);
  uint32_t bound_material = UINT32_MAX;

  for (uint32_t i = 0; i < primitive_count; ++i)
  {
    auto& prim = primitives[i];
    if (prim.materialIndex >= material_count) continue;
    auto& mat = materials[prim.materialIndex];
    if (mat.alphaMode == AlphaMode::Blend) continue;  // skip transparent

    if (prim.materialIndex != bound_material)
    {
      bind_material(prim.materialIndex);
      bound_material = prim.materialIndex;
    }

    cmd.setCullModeEXT(mat.doubleSided
      ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

    auto pc = make_pc(prim.modelMatrix, mat.baseColorFactor,
      mat.metallicFactor, mat.roughnessFactor,
      static_cast<uint32_t>(mat.alphaMode), mat.alphaCutoff);
    cmd.pushConstants(layout, stages, 0, sizeof(PbrPushConstants), &pc);
    mesh->draw_indexed(cmd, prim.indexCount, prim.firstIndex, prim.vertexOffset);
  }

  // --- Transparent pass (depth write OFF, sorted back-to-front) ---
  std::vector<uint32_t> transparent_indices;
  for (uint32_t i = 0; i < primitive_count; ++i)
  {
    auto& prim = primitives[i];
    if (prim.materialIndex >= material_count) continue;
    if (materials[prim.materialIndex].alphaMode == AlphaMode::Blend)
      transparent_indices.push_back(i);
  }

  if (!transparent_indices.empty())
  {
    // Sort back-to-front by distance from camera to world-space centroid
    glm::vec3 cam = cam_position;
    std::sort(transparent_indices.begin(), transparent_indices.end(),
      [&](uint32_t a, uint32_t b) {
        auto ca = glm::vec3(primitives[a].modelMatrix * glm::vec4(primitives[a].centroid, 1.0f));
        auto cb = glm::vec3(primitives[b].modelMatrix * glm::vec4(primitives[b].centroid, 1.0f));
        float da = glm::dot(ca - cam, ca - cam);
        float db = glm::dot(cb - cam, cb - cam);
        return da > db;  // far first
      });

    cmd.setDepthWriteEnableEXT(VK_FALSE);
    bound_material = UINT32_MAX;

    for (uint32_t i : transparent_indices)
    {
      auto& prim = primitives[i];
      auto& mat = materials[prim.materialIndex];

      if (prim.materialIndex != bound_material)
      {
        bind_material(prim.materialIndex);
        bound_material = prim.materialIndex;
      }

      cmd.setCullModeEXT(mat.doubleSided
        ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

      auto pc = make_pc(prim.modelMatrix, mat.baseColorFactor,
        mat.metallicFactor, mat.roughnessFactor,
        static_cast<uint32_t>(mat.alphaMode), mat.alphaCutoff);
      cmd.pushConstants(layout, stages, 0, sizeof(PbrPushConstants), &pc);
      mesh->draw_indexed(cmd, prim.indexCount, prim.firstIndex, prim.vertexOffset);
    }
  }
}

} // namespace vkwave
