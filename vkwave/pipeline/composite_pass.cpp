#include <vkwave/pipeline/composite_pass.h>
#include <vkwave/pipeline/execution_group.h>

namespace vkwave
{

PipelineSpec CompositePass::pipeline_spec()
{
  PipelineSpec spec{};
  spec.vertex_shader = SHADER_DIR "fullscreen.vert";
  spec.fragment_shader = SHADER_DIR "composite.frag";
  // No vertex input (fullscreen triangle from gl_VertexIndex)
  // No depth test
  // No backface culling
  spec.backface_culling = false;
  return spec;
}

void CompositePass::record(vk::CommandBuffer cmd) const
{
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

  struct { float exposure; int tonemapMode; } pc{ exposure, tonemap_mode };
  cmd.pushConstants(layout,
    vk::ShaderStageFlagBits::eFragment,
    0, sizeof(pc), &pc);

  cmd.draw(3, 1, 0, 0);
}

} // namespace vkwave
