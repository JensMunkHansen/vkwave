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

/// Flags for toggling PBR features.
///
/// Split by frequency: the *global* bits are UI toggles that live in the push
/// constant (PbrPushConstants::globalFlags); the *material* bits describe a
/// material's authored capabilities and live in the per-material SSBO
/// (GpuMaterial::materialFlags). The shader ORs the two together so the
/// existing `(flags & BIT)` tests keep working unchanged.
namespace PbrFlags {
  // Global (push constant) — per-frame UI toggles
  constexpr uint32_t NormalMapping      = 1u << 0;
  constexpr uint32_t Emissive           = 1u << 1;
  constexpr uint32_t Clearcoat          = 1u << 2; // apply KHR_materials_clearcoat layer
  constexpr uint32_t Anisotropy         = 1u << 4; // apply KHR_materials_anisotropy

  // Material (SSBO) — authored per material
  constexpr uint32_t ClearcoatNormalMap = 1u << 3; // coat has a dedicated normal texture
  constexpr uint32_t AnisotropyMap      = 1u << 5; // anisotropy has a direction texture

  constexpr uint32_t GlobalMask   = NormalMapping | Emissive | Clearcoat | Anisotropy;
  constexpr uint32_t MaterialMask = ClearcoatNormalMap | AnisotropyMap;
}

/// Per-material constants, indexed by PbrPushConstants::materialIndex.
///
/// Stored in a single immutable, shared SSBO (NOT ring-buffered): material data
/// never changes after load, so every in-flight frame reads the same buffer
/// concurrently — race-free by construction. See pbr.frag's `GpuMaterial`.
///
/// Layout matches std430. baseColorFactor is first so its 16-byte alignment
/// coincides with offset 0; every following scalar packs at 4-byte stride,
/// identically in C++ (glm default alignment) and std430.
struct GpuMaterial
{
  glm::vec4 baseColorFactor{ 1.0f }; // 16 bytes
  float metallicFactor{ 1.0f };      //  4 bytes
  float roughnessFactor{ 1.0f };     //  4 bytes
  float clearcoatFactor{ 0.0f };     //  4 bytes
  float clearcoatRoughnessFactor{ 0.0f }; // 4 bytes
  float anisotropyStrength{ 0.0f };  //  4 bytes
  float anisotropyRotation{ 0.0f };  //  4 bytes — radians
  float alphaCutoff{ 0.5f };         //  4 bytes
  uint32_t alphaMode{ 0 };           //  4 bytes — 0=opaque, 1=mask, 2=blend
  uint32_t materialFlags{ 0 };       //  4 bytes — PbrFlags::MaterialMask bits
  uint32_t uvSets{ 0 };              //  4 bytes — per-texture UV-set selector (bit b => binding b uses TEXCOORD_1)
  float normalScale{ 1.0f };         //  4 bytes — glTF normalTexture.scale
  uint32_t _pad2{ 0 };               //  reserved (future: sheen/transmission)

  // KHR_texture_transform per texture slot, precomputed affine (xform_identity
  // by default). Two vec4 per slot: [2*s] = packed mat2 (col0.xy, col1.xy),
  // [2*s+1].xy = UV offset. uv' = mat2(a.xy, a.zw) * uv + off.
  glm::vec4 texXform[18];            // 9 slots × 2 vec4 = 288 bytes
};                                   // 352 bytes total (std430 stride)

static_assert(sizeof(GpuMaterial) == 352,
  "GpuMaterial must be 352 bytes to match std430 SSBO layout");

/// Set a GpuMaterial's texture transforms to identity (no-op). Required because
/// a zero-initialized transform would collapse all UVs to (0,0).
inline void set_identity_tex_xforms(GpuMaterial& gm)
{
  for (int s = 0; s < 9; ++s)
  {
    gm.texXform[2 * s + 0] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // identity mat2
    gm.texXform[2 * s + 1] = glm::vec4(0.0f);                   // zero offset
  }
}

/// Push constant data for the PBR pass.
/// Must match the layout in pbr.vert / pbr.frag.
///
/// Per-material constants moved to the GpuMaterial SSBO — this carries only
/// per-draw data plus the small global UI overrides (a value < 0 means
/// "use the material's authored value").
struct PbrPushConstants
{
  glm::mat4 model;                   // 64 bytes — model transform
  uint32_t materialIndex;            //  4 bytes — index into the GpuMaterial SSBO
  uint32_t globalFlags;              //  4 bytes — PbrFlags::GlobalMask bits
  int32_t debugMode;                 //  4 bytes — debug visualization mode
  float time;                        //  4 bytes — animation time
  float metallicOverride;            //  4 bytes — < 0 = use material
  float roughnessOverride;           //  4 bytes — < 0 = use material
  float clearcoatOverride;           //  4 bytes — < 0 = use material
  float clearcoatRoughnessOverride;  //  4 bytes
  float anisotropyOverride;          //  4 bytes — < 0 = use material
  float anisotropyRotationOverride;  //  4 bytes
  float mipBias;                     //  4 bytes — texture LOD bias (0 = mipmapped; large negative forces mip 0)
};                                   // 108 bytes total

static_assert(sizeof(PbrPushConstants) == 108,
  "PbrPushConstants must be 108 bytes to match shader layout");
static_assert(sizeof(PbrPushConstants) <= 128,
  "Push constants must fit in 128 bytes (guaranteed minimum)");

} // namespace vkwave
