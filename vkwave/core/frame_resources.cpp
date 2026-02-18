#include <vkwave/core/frame_resources.h>

#include <vkwave/core/commands.h>
#include <vkwave/core/device.h>

namespace vkwave
{

std::vector<FrameResources> create_frame_resources(
  const Device& device, const uint32_t count)
{
  std::vector<FrameResources> frames;
  frames.reserve(count);

  for (uint32_t i = 0; i < count; ++i)
  {
    FrameResources fr;

    fr.command_pool = make_command_pool(device, false);

    vk::CommandBufferAllocateInfo alloc{};
    alloc.commandPool = fr.command_pool;
    alloc.level = vk::CommandBufferLevel::ePrimary;
    alloc.commandBufferCount = 1;
    fr.command_buffer = device.device().allocateCommandBuffers(alloc)[0];

    frames.push_back(std::move(fr));
  }

  return frames;
}

void destroy_frame_resources(
  std::vector<FrameResources>& frames, vk::Device device)
{
  for (auto& fr : frames)
  {
    if (fr.command_pool)
      device.destroyCommandPool(fr.command_pool);
    if (fr.framebuffer)
      device.destroyFramebuffer(fr.framebuffer);
  }
  frames.clear();
}

} // namespace vkwave
