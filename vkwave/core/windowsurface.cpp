#include <vkwave/core/exception.h>
#include <vkwave/core/windowsurface.h>

#include <spdlog/spdlog.h>

#include <cassert>
#include <utility>

namespace vkwave
{
WindowSurface::WindowSurface(const vk::Instance instance, GLFWwindow* window)
  : m_instance(instance)
{
  assert(instance);
  assert(window);

  spdlog::trace("Creating window surface");
  VkSurfaceKHR c_style_surface;

  if (const auto result = glfwCreateWindowSurface(instance, window, nullptr, &c_style_surface);
      result != VK_SUCCESS)
  {
    throw VulkanException("Error: glfwCreateWindowSurface failed!", result);
  }
  // copy constructor converts to hpp convention
  m_surface = c_style_surface;
}

WindowSurface::WindowSurface(WindowSurface&& other) noexcept
{
  m_instance = other.m_instance;
  m_surface = std::exchange(other.m_surface, nullptr);
}

WindowSurface::~WindowSurface()
{
  m_instance.destroySurfaceKHR(m_surface);
}

}
