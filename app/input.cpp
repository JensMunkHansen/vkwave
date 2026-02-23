#include "input.h"

#include <vkwave/core/camera.h>

void Input::on_cursor_pos(GLFWwindow* window, double xpos, double ypos)
{
  if (!mouse_tracked)
  {
    last_mouse_x = xpos;
    last_mouse_y = ypos;
    mouse_tracked = true;
    return;
  }

  double dx = xpos - last_mouse_x;
  double dy = ypos - last_mouse_y;
  last_mouse_x = xpos;
  last_mouse_y = ypos;

  // Left drag — orbit
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
  {
    camera->azimuth(static_cast<float>(-dx) * 0.3f);
    camera->elevation(static_cast<float>(-dy) * 0.3f);
  }

  // Right drag — dolly
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
  {
    float factor = 1.0f + static_cast<float>(dy) * 0.005f;
    camera->dolly(factor);
  }

  // Middle drag — pan
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
  {
    float dist = camera->distance();
    float scale = dist * 0.002f;
    camera->pan(static_cast<float>(-dx) * scale, static_cast<float>(dy) * scale);
  }
}

void Input::on_scroll(double yoffset)
{
  float factor = 1.0f + static_cast<float>(yoffset) * 0.1f;
  camera->dolly(factor);
}
