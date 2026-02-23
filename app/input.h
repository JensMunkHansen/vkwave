#pragma once

#include <GLFW/glfw3.h>

namespace vkwave { class Camera; }

/// Mouse input state. GLFW callbacks are registered in vkwave_app.cpp
/// (since the user pointer is shared with resize handling).
struct Input
{
  double last_mouse_x{0.0};
  double last_mouse_y{0.0};
  bool mouse_tracked{false};

  vkwave::Camera* camera{nullptr};

  void bind(vkwave::Camera& cam) { camera = &cam; }

  /// Called from cursor position callback.
  void on_cursor_pos(GLFWwindow* window, double xpos, double ypos);

  /// Called from scroll callback.
  void on_scroll(double yoffset);
};
