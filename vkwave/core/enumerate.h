#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

#include <cassert>
#include <vector>

namespace vkwave
{
std::vector<vk::PhysicalDevice> get_physical_devices(const vk::Instance& inst);
}
