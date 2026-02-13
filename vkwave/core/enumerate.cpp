#include <vkwave/core/enumerate.h>

#include <cassert>
namespace vkwave
{
std::vector<vk::PhysicalDevice> get_physical_devices(const vk::Instance& inst)
{
  assert(inst);
  try
  {
    const auto availableDevices = inst.enumeratePhysicalDevices();
  }
  catch (...)
  {
  }
}
}
