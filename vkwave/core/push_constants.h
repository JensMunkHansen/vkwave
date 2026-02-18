#pragma once

#include <cstdint>

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

} // namespace vkwave
