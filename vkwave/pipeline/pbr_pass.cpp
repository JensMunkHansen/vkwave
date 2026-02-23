#include <vkwave/pipeline/pbr_pass.h>
#include <vkwave/pipeline/execution_group.h>
#include <vkwave/pipeline/pipeline.h>

#include <vkwave/core/pbr_ubo.h>
#include <vkwave/core/vertex.h>

#include <glm/gtc/matrix_transform.hpp>

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

  // Push constants
  PbrPushConstants pc{};
  pc.model = model;
  pc.baseColorFactor = base_color_factor;
  pc.metallicFactor = metallic_factor;
  pc.roughnessFactor = roughness_factor;
  pc.time = time;
  pc.debugMode = debug_mode;

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

  auto ds = group->descriptor_set();
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
    0, 1, &ds, 0, nullptr);

  cmd.pushConstants(layout,
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    0, sizeof(PbrPushConstants), &pc);

  mesh->bind(cmd);
  mesh->draw(cmd);
}

} // namespace vkwave
