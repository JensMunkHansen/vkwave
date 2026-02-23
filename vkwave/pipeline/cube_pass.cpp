#include <vkwave/pipeline/cube_pass.h>
#include <vkwave/pipeline/execution_group.h>
#include <vkwave/pipeline/pipeline.h>

#include <vkwave/core/camera_ubo.h>
#include <vkwave/core/push_constants.h>
#include <vkwave/core/vertex.h>

namespace vkwave
{

PipelineSpec CubePass::pipeline_spec()
{
  auto binding = Vertex::binding_description();
  auto attrs = Vertex::attribute_descriptions();

  PipelineSpec spec{};
  spec.vertex_shader = SHADER_DIR "cube.vert";
  spec.fragment_shader = SHADER_DIR "cube.frag";
  spec.vertex_bindings = { binding };
  spec.vertex_attributes = { attrs.begin(), attrs.end() };
  spec.backface_culling = true;
  spec.depth_test = true;
  return spec;
}

void CubePass::record(vk::CommandBuffer cmd) const
{
  // Update camera UBO for this slot (group tracks current slot)
  CameraUBO ubo_data{};
  ubo_data.viewProj = view_projection;
  group->ubo(0, 0).update(&ubo_data, sizeof(ubo_data));

  // Push constants
  CubePushConstants pc{};
  pc.model = model;
  pc.time = time;
  pc.debugMode = debug_mode;

  auto pipeline = group->pipeline();
  auto layout = group->layout();
  auto extent = group->extent();

  // Bind pipeline and dynamic state
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
    0, sizeof(CubePushConstants), &pc);

  mesh->bind(cmd);
  mesh->draw(cmd);
}

} // namespace vkwave
