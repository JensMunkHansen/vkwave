#include "vkwave/core/swapchain.h"
#include <vkwave/config.h>

#include <vulkan/vulkan.hpp>

namespace vkwave
{
class Swapchain;
/**
        Data structures involved in making framebuffers for the
        swapchain.
*/
struct framebufferInput
{
  vk::Device device;
  vk::RenderPass renderpass;
  vk::Extent2D swapchainExtent;
  vk::ImageView depthImageView{ VK_NULL_HANDLE };     // Optional depth attachment
  vk::ImageView msaaColorImageView{ VK_NULL_HANDLE }; // Optional MSAA color attachment
};

/**
        Make framebuffers for the swapchain

        \param inputChunk required input for creation
        \param frames the vector to be populated with the created framebuffers
        \param debug whether the system is running in debug mode.
*/
std::vector<vk::Framebuffer> make_framebuffers(
  framebufferInput inputChunk, const Swapchain& swapchaine, bool debug);

}
