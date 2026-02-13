
#include <vkwave/core/commands.h>

#include <vkwave/core/device.h>
#include <vkwave/core/swapchain.h>

#include <iostream>

namespace vkwave
{
vk::CommandPool make_command_pool(const Device& device0, bool debug)
{

  const vk::Device device = device0.device();

  uint32_t queueFamilyIndex = device0.m_graphics_queue_family_index;

  vk::CommandPoolCreateInfo poolInfo;
  poolInfo.flags =
    vk::CommandPoolCreateFlags() | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  poolInfo.queueFamilyIndex = queueFamilyIndex;

  try
  {
    return device.createCommandPool(poolInfo);
  }
  catch (vk::SystemError err)
  {

    if (debug)
    {
      std::cout << "Failed to create Command Pool" << std::endl;
    }

    return nullptr;
  }
}

vk::CommandBuffer make_command_buffers(const Device& device, const Swapchain& swapChain,
  vk::CommandPool pool, std::vector<vk::CommandBuffer>& commandBuffers, bool debug)
{

  vk::CommandBufferAllocateInfo allocInfo = {};
  allocInfo.commandPool = pool;
  allocInfo.level = vk::CommandBufferLevel::ePrimary;
  allocInfo.commandBufferCount = 1;

  commandBuffers.resize(swapChain.image_count());

  // Make a command buffer for each frame
  for (uint32_t i = 0; i < swapChain.image_count(); i++)
  {
    try
    {
      commandBuffers[i] = device.device().allocateCommandBuffers(allocInfo)[0];

      if (debug)
      {
        std::cout << "Allocated command buffer for frame " << i << std::endl;
      }
    }
    catch (vk::SystemError err)
    {

      if (debug)
      {
        std::cout << "Failed to allocate command buffer for frame " << i << std::endl;
      }
    }
  }

  // Make a "main" command buffer for the engine
  try
  {
    vk::CommandBuffer commandBuffer = device.device().allocateCommandBuffers(allocInfo)[0];

    if (debug)
    {
      std::cout << "Allocated main command buffer " << std::endl;
    }

    return commandBuffer;
  }
  catch (vk::SystemError err)
  {

    if (debug)
    {
      std::cout << "Failed to allocate main command buffer " << std::endl;
    }

    return nullptr;
  }
  return nullptr;
}

}
