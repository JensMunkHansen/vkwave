#pragma once

#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>

namespace vkwave
{
class Window
{
public:
  enum class Mode
  {
    WINDOWED,
    FULLSCREEN,
    WINDOWED_FULLSCREEN
  };

private:
  std::uint32_t m_width;
  std::uint32_t m_height;
  Mode m_mode;
  GLFWwindow* m_window{ nullptr };

  // Resize state
  bool m_resize_pending{ false };
  std::uint32_t m_pending_width{ 0 };
  std::uint32_t m_pending_height{ 0 };

public:
  Window(const std::string& title, std::uint32_t width, std::uint32_t height, bool visible,
    bool resizable, Mode mode);
  Window(const Window&) = delete;
  Window(Window&&) = delete;
  ~Window();

  Window& operator=(const Window&) = delete;
  Window& operator=(Window&&) = delete;

  [[nodiscard]] GLFWwindow* get() const { return m_window; }

  [[nodiscard]] std::uint32_t width() const { return m_width; }

  [[nodiscard]] std::uint32_t height() const { return m_height; }

  // Get actual framebuffer size (may differ from window size on HiDPI)
  void get_framebuffer_size(std::uint32_t& width, std::uint32_t& height) const;

  [[nodiscard]] Mode mode() const { return m_mode; }

  // Resize support
  void set_resize_pending(std::uint32_t width, std::uint32_t height);
  [[nodiscard]] bool has_pending_resize() const { return m_resize_pending; }
  void get_pending_resize(std::uint32_t& width, std::uint32_t& height);
  void wait_for_focus();
  void set_user_ptr(void* user_ptr);
  void set_resize_callback(GLFWframebuffersizefun callback);

  void set_title(const std::string& title);

  static void poll();

  bool should_close() const;
};
}
