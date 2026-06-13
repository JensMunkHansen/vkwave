#version 450

// Transmission (refraction) fragment shader — PHASE 1 DUMMY.
//
// This is a plumbing test: it does NOT yet sample the snapshot or refract. It
// draws transmissive primitives with a flat, view-dependent glass tint so we can
// confirm the separate transmission pass renders into the HDR target and
// depth-tests against the opaque depth. The real refraction (sample the snapshot
// at an IOR/thickness-bent screen coordinate + Beer-Lambert tint) replaces the
// body in the next slice.
//
// Reuses pbr.vert, so the push-constant block must match pbr.vert exactly.

layout(set = 0, binding = 0) uniform PbrUBO {
  mat4 viewProj;
  vec4 camPos;
  vec4 lightDirection;
  vec4 lightColor;
} ubo;

// Mirror of vkwave::GpuMaterial (std430) — must match pbr.frag / C++ byte layout.
struct GpuMaterial {
  vec4 baseColorFactor;
  float metallicFactor;
  float roughnessFactor;
  float clearcoatFactor;
  float clearcoatRoughnessFactor;
  float anisotropyStrength;
  float anisotropyRotation;
  float alphaCutoff;
  uint alphaMode;
  uint materialFlags;
  uint uvSets;
  float normalScale;
  uint _pad2;
  vec4 texXform[18];
  float transmissionFactor;
  float ior;
  float thicknessFactor;
  float _pad3;
  vec4 attenuation; // rgb=attenuation color, w=attenuation distance (0=infinite)
};
layout(set = 1, binding = 0, std430) readonly buffer MaterialBuffer {
  GpuMaterial materials[];
} matbuf;

// Push constant — must match PbrPushConstants (C++) and pbr.vert exactly.
layout(push_constant) uniform PushConstants {
  mat4 model;
  uint materialIndex;
  uint globalFlags;
  int debugMode;
  float time;
  float metallicOverride;
  float roughnessOverride;
  float clearcoatOverride;
  float clearcoatRoughnessOverride;
  float anisotropyOverride;
  float anisotropyRotationOverride;
  float mipBias;
} pc;

// Must declare the full pbr.vert output interface (even unused fields) so the
// stage interface matches and the SPIR-V linker emits no "output not consumed"
// warnings. Phase 1 only uses fragNormal + fragPos.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;       // locations 4,5,6
layout(location = 7) in vec2 fragTexCoord1;

layout(location = 0) out vec4 outColor;

void main()
{
  GpuMaterial m = matbuf.materials[pc.materialIndex];

  vec3 N = normalize(fragNormal);
  vec3 V = normalize(ubo.camPos.xyz - fragPos);
  float fresnel = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);

  // Flat glass tint from the volume attenuation colour, brightened at grazing
  // angles by a Fresnel rim. Placeholder until snapshot refraction lands.
  vec3 glassTint = m.attenuation.rgb;
  vec3 col = mix(glassTint * 0.25, vec3(1.0), fresnel);

  outColor = vec4(col, 1.0);
}
