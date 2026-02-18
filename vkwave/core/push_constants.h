#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace vkwave
{

/// Push constant data for the fullscreen triangle pass.
/// Must match the layout in fullscreen.frag.
struct TrianglePushConstants
{
  float color[4];      // RGBA color
  float time;          // Frame time for animation
  int32_t debugMode;   // Debug visualization mode
};

static_assert(sizeof(TrianglePushConstants) <= 128,
  "Push constants must fit in 128 bytes (guaranteed minimum)");

/// Push constant data for the cube pass.
/// Must match the layout in cube.vert / cube.frag.
struct CubePushConstants
{
  glm::mat4 model;     // 64 bytes — model transform
  float time;          // 4 bytes  — animation time
  int32_t debugMode;   // 4 bytes  — debug visualization mode
};                     // 72 bytes total

static_assert(sizeof(CubePushConstants) == 72,
  "CubePushConstants must be 72 bytes to match shader layout");
static_assert(sizeof(CubePushConstants) <= 128,
  "Push constants must fit in 128 bytes (guaranteed minimum)");

} // namespace vkwave
