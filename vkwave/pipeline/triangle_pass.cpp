#include <vkwave/pipeline/triangle_pass.h>

namespace vkwave
{

void TrianglePass::record(
  vk::CommandBuffer cmd, const TrianglePushConstants& pc) const
{
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

  cmd.pushConstants(layout, vk::ShaderStageFlagBits::eFragment,
    0, sizeof(TrianglePushConstants), &pc);

  cmd.draw(3, 1, 0, 0);
}

} // namespace vkwave
