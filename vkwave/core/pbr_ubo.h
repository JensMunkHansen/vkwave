#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace vkwave
{

/// UBO for PBR per-frame camera + lighting data.
/// Ring-buffered by the execution group (one copy per swapchain image).
/// Must match shader layout (std140).
struct PbrUBO
{
  glm::mat4 viewProj;       // 64 bytes — combined view-projection matrix
  glm::vec4 camPos;         // 16 bytes — xyz=camera position, w=unused
  glm::vec4 lightDirection; // 16 bytes — xyz=direction, w=intensity
  glm::vec4 lightColor;     // 16 bytes — rgb=color, a=unused
};

static_assert(sizeof(PbrUBO) == 112,
  "PbrUBO must be 112 bytes to match shader layout (std140)");

/// Push constant flags for toggling PBR features.
namespace PbrFlags {
  constexpr uint32_t NormalMapping = 1u << 0;
  constexpr uint32_t Emissive     = 1u << 1;
  constexpr uint32_t All          = NormalMapping | Emissive;
}

/// Push constant data for the PBR pass.
/// Must match the layout in pbr.vert / pbr.frag.
struct PbrPushConstants
{
  glm::mat4 model;           // 64 bytes — model transform
  glm::vec4 baseColorFactor; // 16 bytes — base color multiplier
  float metallicFactor;      //  4 bytes
  float roughnessFactor;     //  4 bytes
  float time;                //  4 bytes — animation time
  int32_t debugMode;         //  4 bytes — debug visualization mode
  uint32_t flags;            //  4 bytes — PbrFlags bitmask
  uint32_t alphaMode;        //  4 bytes — 0=opaque, 1=mask, 2=blend
  float alphaCutoff;         //  4 bytes — cutoff for alpha mask mode
};                           // 108 bytes total

static_assert(sizeof(PbrPushConstants) == 108,
  "PbrPushConstants must be 108 bytes to match shader layout");
static_assert(sizeof(PbrPushConstants) <= 128,
  "Push constants must fit in 128 bytes (guaranteed minimum)");

} // namespace vkwave
