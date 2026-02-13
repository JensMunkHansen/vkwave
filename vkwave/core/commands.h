#pragma once

#include <vkwave/config.h>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

namespace vkwave
{

class Device;
class Swapchain;

/**
        Data structures used in creating command buffers
*/
struct commandBufferInputChunk
{
  vk::Device device;
  vk::CommandPool commandPool;
  // std::vector<vkUtil::SwapChainFrame>& frames;
};

/**
        Make a command pool.

        \param device the logical device
        \param physicalDevice the physical device
        \param the windows surface (used for getting the queue families)
        \param debug whether the system is running in debug mode
        \returns the created command pool
*/

vk::CommandPool make_command_pool(const Device& device0, bool debug);

/**
        Make a command buffer for each swapchain frame and return a main command buffer.

        \param inputChunk the required input info
        \param debug whether the system is running in debug mode
        \returns the main command buffer
*/
vk::CommandBuffer make_command_buffers(const Device& device, const Swapchain& swapChain,
  vk::CommandPool pool, std::vector<vk::CommandBuffer>& commandBuffers, bool debug);

}
