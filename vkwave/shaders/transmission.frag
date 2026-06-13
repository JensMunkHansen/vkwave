#version 450

// Transmission (refraction) fragment shader — PHASE 1: sharp transmission.
//
// Samples the opaque-scene snapshot at a screen coordinate displaced by the
// refraction vector (Snell's law from IOR) over the volume thickness, then
// applies Beer-Lambert absorption (KHR_materials_volume) and a Fresnel rim. No
// roughness blur yet (that needs the snapshot mip chain — a later phase).
//
// Reuses pbr.vert, so the push-constant block must match pbr.vert exactly.

layout(set = 0, binding = 0) uniform PbrUBO {
  mat4 viewProj;
  vec4 camPos;
  vec4 lightDirection;
  vec4 lightColor;
} ubo;

// Per-slot snapshot of the opaque HDR (the scene *behind* the glass). Rebound
// each frame to the current slot's snapshot image by the record callback.
layout(set = 0, binding = 1) uniform sampler2D snapshotTex;

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

// Full pbr.vert output interface (unused fields keep the stage interface matched).
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
  float ior = max(m.ior, 1.0);

  // Refraction: bend the view ray by Snell's law and walk it through the volume
  // a distance set by the thickness. The exit point projected to screen space is
  // where we read the opaque background — so the background appears displaced.
  vec3 refractDir = refract(-V, N, 1.0 / ior);
  if (dot(refractDir, refractDir) < 1e-6)
    refractDir = -V;                      // total internal reflection -> straight
  float thickness = max(m.thicknessFactor, 0.0);
  vec3 exitPos = fragPos + refractDir * thickness;

  vec4 clip = ubo.viewProj * vec4(exitPos, 1.0);
  vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;   // Vulkan NDC -> texture UV (no flip)
  uv = clamp(uv, vec2(0.0), vec2(1.0));
  vec3 background = texture(snapshotTex, uv).rgb;

  // Beer-Lambert absorption over the path length (KHR_materials_volume). The
  // attenuation colour is the transmitted colour at attenuationDistance.
  vec3 transmitted = background;
  if (m.attenuation.w > 0.0)
  {
    vec3 absorption = -log(clamp(m.attenuation.rgb, vec3(1e-4), vec3(1.0))) / m.attenuation.w;
    transmitted *= exp(-absorption * thickness);
  }

  // Fresnel reflection (dielectric F0 from IOR); reflect a neutral highlight for
  // a glassy rim (full environment reflection on glass is a later refinement).
  float F0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
  float fresnel = F0 + (1.0 - F0) * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);

  // Blend the surface base colour with the refracted background by transmission,
  // then add the Fresnel rim on top.
  vec3 color = mix(m.baseColorFactor.rgb, transmitted, clamp(m.transmissionFactor, 0.0, 1.0));
  color = mix(color, vec3(1.0), fresnel);

  outColor = vec4(color, 1.0);
}
