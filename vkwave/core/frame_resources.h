#pragma once

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <vector>

namespace vkwave
{

class Device;

/// Per-swapchain-image resource set.
///
/// Owns only the command pool/buffer and framebuffer.
/// Synchronization (timeline semaphore + binary present semaphore)
/// lives in the ExecutionGroup that owns these.
struct FrameResources
{
  vk::CommandPool   command_pool{ VK_NULL_HANDLE };
  vk::CommandBuffer command_buffer{ VK_NULL_HANDLE };
  vk::Framebuffer   framebuffer{ VK_NULL_HANDLE };
};

/// Create N frame resource sets (command pool + buffer each).
std::vector<FrameResources> create_frame_resources(
  const Device& device, uint32_t count);

/// Destroy non-RAII resources (command pools, framebuffers). Clears the vector.
void destroy_frame_resources(
  std::vector<FrameResources>& frames, vk::Device device);

} // namespace vkwave
