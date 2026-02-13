#pragma once

#include <vulkan/vulkan.hpp>

#include <GLFW/glfw3.h>

namespace vkwave
{

/// @brief RAII wrapper class for VkSurfaceKHR.
class WindowSurface
{
  vk::Instance m_instance{ VK_NULL_HANDLE };
  vk::SurfaceKHR m_surface{ VK_NULL_HANDLE };

public:
  /// @brief Default constructor.
  /// @param instance The Vulkan instance which will be associated with this surface.
  /// @param window The window which will be associated with this surface.
  WindowSurface(vk::Instance instance, GLFWwindow* window);

  WindowSurface(const WindowSurface&) = delete;
  WindowSurface(WindowSurface&&) noexcept;

  ~WindowSurface();

  WindowSurface& operator=(const WindowSurface&) = delete;
  WindowSurface& operator=(WindowSurface&&) = default;

  [[nodiscard]] vk::SurfaceKHR get() const { return m_surface; }
};

} // namespace vkwave
