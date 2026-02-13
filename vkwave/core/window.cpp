#include <cassert>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <vkwave/core/window.h>

namespace vkwave
{

Window::Window(const std::string& title, const std::uint32_t width, const std::uint32_t height,
  const bool visible, const bool resizable, const Mode mode)
  : m_width(width)
  , m_height(height)
  , m_mode(mode)
{
  assert(!title.empty());

  // Force X11 platform for NVIDIA PRIME compatibility
  // Can be controlled via environment variable VULK3D_USE_X11=1
  if (const char* use_x11 = std::getenv("VULK3D_USE_X11"); use_x11 && std::string(use_x11) == "1")
  {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    spdlog::trace("Forcing X11 platform");
  }

  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("Failed to initialise GLFW!");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

  spdlog::trace("Creating window");

  GLFWmonitor* monitor = nullptr;
  if (m_mode != Mode::WINDOWED)
  {
    monitor = glfwGetPrimaryMonitor();
    if (m_mode == Mode::WINDOWED_FULLSCREEN)
    {
      const auto* video_mode = glfwGetVideoMode(monitor);
      m_width = video_mode->width;
      m_height = video_mode->height;
    }
  }

  m_window = glfwCreateWindow(
    static_cast<int>(width), static_cast<int>(height), title.c_str(), monitor, nullptr);

  if (m_window == nullptr)
  {
    throw std::runtime_error("Error: glfwCreateWindow failed for window " + title + " !");
  }
}

Window::~Window()
{
  glfwDestroyWindow(m_window);
  glfwTerminate();
}
bool Window::should_close() const
{
  return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}
void Window::poll()
{
  glfwPollEvents();
}

void Window::set_resize_pending(std::uint32_t width, std::uint32_t height)
{
  m_pending_width = width;
  m_pending_height = height;
  m_resize_pending = true;
  spdlog::warn("RESIZE CALLBACK: {} x {}", width, height);
}

void Window::get_pending_resize(std::uint32_t& width, std::uint32_t& height)
{
  width = m_pending_width;
  height = m_pending_height;
  // Update stored dimensions
  m_width = m_pending_width;
  m_height = m_pending_height;
  m_resize_pending = false;
}

void Window::wait_for_focus()
{
  int current_width = 0;
  int current_height = 0;

  do
  {
    glfwWaitEvents();
    glfwGetFramebufferSize(m_window, &current_width, &current_height);
  } while (current_width == 0 || current_height == 0);

  m_width = current_width;
  m_height = current_height;
}

void Window::set_user_ptr(void* user_ptr)
{
  glfwSetWindowUserPointer(m_window, user_ptr);
}

void Window::set_resize_callback(GLFWframebuffersizefun callback)
{
  glfwSetFramebufferSizeCallback(m_window, callback);
  // Also set window size callback as backup
  glfwSetWindowSizeCallback(m_window, callback);
  spdlog::warn("Resize callback registered");
}

void Window::get_framebuffer_size(std::uint32_t& width, std::uint32_t& height) const
{
  int w, h;
  glfwGetFramebufferSize(m_window, &w, &h);
  width = static_cast<std::uint32_t>(w);
  height = static_cast<std::uint32_t>(h);
}

}
