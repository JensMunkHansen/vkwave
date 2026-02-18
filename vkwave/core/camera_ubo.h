#pragma once

#include <glm/glm.hpp>

namespace vkwave
{

/// UBO for per-frame camera data.
/// Ring-buffered by the execution group (one copy per swapchain image).
struct CameraUBO
{
  glm::mat4 viewProj;  // 64 bytes — combined view-projection matrix
};

static_assert(sizeof(CameraUBO) == 64,
  "CameraUBO must be 64 bytes to match shader layout");

} // namespace vkwave
