#version 450

// PBR fragment shader — Cook-Torrance BRDF with IBL
// Adapted from Vulkanstein3D's fragment.frag (iridescence, SSS, alpha modes stripped).
//
// References:
// - https://learnopengl.com/PBR/Theory
// - https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material
// - https://github.com/KhronosGroup/glTF-Sample-Viewer

// Set 0: Per-frame (ring-buffered per swapchain image)
layout(set = 0, binding = 0) uniform PbrUBO {
  mat4 viewProj;
  vec4 camPos;
  vec4 lightDirection;  // xyz=direction, w=intensity
  vec4 lightColor;      // rgb=color, a=unused
} ubo;

// Set 1: Per-material textures (bound once per material change)
layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;  // G=roughness, B=metallic
layout(set = 1, binding = 3) uniform sampler2D emissiveTexture;
layout(set = 1, binding = 4) uniform sampler2D aoTexture;                 // R=AO
layout(set = 1, binding = 5) uniform sampler2D clearcoatTexture;          // R=clearcoat strength
layout(set = 1, binding = 6) uniform sampler2D clearcoatRoughnessTexture; // G=clearcoat roughness
layout(set = 1, binding = 7) uniform sampler2D clearcoatNormalTexture;    // tangent-space coat normal
layout(set = 1, binding = 8) uniform sampler2D anisotropyTexture;         // RG=direction, B=strength

// Set 2: Per-scene globals (bound once per frame)
layout(set = 2, binding = 0) uniform sampler2D brdfLUT;
layout(set = 2, binding = 1) uniform samplerCube irradianceMap;
layout(set = 2, binding = 2) uniform samplerCube prefilterMap;

// Per-material constants — single immutable SSBO shared across all frames
// (material data never changes after load). Indexed by pc.materialIndex.
// Layout must match vkwave::GpuMaterial (std430).
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
  uint uvSets;     // bit b => texture at binding b samples fragTexCoord1
  float normalScale; // glTF normalTexture.scale
  uint _pad2;
  vec4 texXform[18]; // KHR_texture_transform: per slot [2s]=mat2, [2s+1].xy=offset
};
layout(set = 2, binding = 3, std430) readonly buffer MaterialBuffer {
  GpuMaterial materials[];
} matbuf;

// Push constant — must match PbrPushConstants (C++) and pbr.vert exactly.
// Per-material data lives in the SSBO; the *Override floats are global UI
// previews (< 0 means "use the material's authored value").
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
} pc;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;
layout(location = 7) in vec2 fragTexCoord1;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// ============================================================================
// BRDF Functions (matching glTF-Sample-Viewer)
// ============================================================================

// Fresnel-Schlick with f90 parameter
vec3 F_Schlick(vec3 f0, vec3 f90, float VdotH)
{
  float x = clamp(1.0 - VdotH, 0.0, 1.0);
  float x2 = x * x;
  float x5 = x * x2 * x2;
  return f0 + (f90 - f0) * x5;
}

// GGX/Trowbridge-Reitz Normal Distribution Function
float D_GGX(float NdotH, float alphaRoughness)
{
  float alphaSq = alphaRoughness * alphaRoughness;
  float f = (NdotH * NdotH) * (alphaSq - 1.0) + 1.0;
  return alphaSq / (PI * f * f);
}

// Smith Height-Correlated Visibility Function
float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
  float alphaSq = alphaRoughness * alphaRoughness;
  float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaSq) + alphaSq);
  float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaSq) + alphaSq);
  float GGX = GGXV + GGXL;
  return GGX > 0.0 ? 0.5 / GGX : 0.0;
}
#if 0
float V_SmithGGXCorrelatedFaste(float NdotL, float NdotV, float alphaRoughness)
{
    float a = alphaRoughness;
    float GGXV = NdotL * (NdotV * (1.0 - a) + a);
    float GGXL = NdotV * (NdotL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}
#endif
// Lambertian diffuse BRDF
vec3 BRDF_lambertian(vec3 diffuseColor)
{
  return diffuseColor / PI;
}

// Full specular GGX BRDF (Vis * D, fresnel applied separately)
float BRDF_specularGGX(float alphaRoughness, float NdotL, float NdotV, float NdotH)
{
  float V = V_GGX(NdotL, NdotV, alphaRoughness);
  float D = D_GGX(NdotH, alphaRoughness);
  return V * D;
}

// --- Anisotropy (KHR_materials_anisotropy): split roughness at/ab along T/B ---

// Anisotropic GGX/Trowbridge-Reitz Normal Distribution Function
float D_GGX_anisotropic(float NdotH, float TdotH, float BdotH, float at, float ab)
{
  float a2 = at * ab;
  vec3 f = vec3(ab * TdotH, at * BdotH, a2 * NdotH);
  float w2 = a2 / dot(f, f);
  return a2 * w2 * w2 / PI;
}

// Anisotropic Smith Height-Correlated Visibility Function
float V_GGX_anisotropic(float NdotL, float NdotV, float TdotV, float BdotV,
                        float TdotL, float BdotL, float at, float ab)
{
  float GGXV = NdotL * length(vec3(at * TdotV, ab * BdotV, NdotV));
  float GGXL = NdotV * length(vec3(at * TdotL, ab * BdotL, NdotL));
  float v = 0.5 / (GGXV + GGXL);
  return clamp(v, 0.0, 1.0);
}

// ============================================================================
// IBL Functions
// ============================================================================

// Diffuse irradiance from environment map
vec3 getIBLDiffuseLight(vec3 N)
{
  return texture(irradianceMap, N).rgb / PI;
}

// Specular radiance from prefiltered environment map
vec3 getIBLRadianceGGX(vec3 N, vec3 V, float perceptualRoughness)
{
  vec3 R = reflect(-V, N);
  const float MAX_REFLECTION_LOD = 4.0;
  float lod = perceptualRoughness * MAX_REFLECTION_LOD;
  return textureLod(prefilterMap, R, lod).rgb;
}

// Anisotropic specular radiance: bends the reflection vector toward the
// anisotropy bitangent to stretch the environment highlight (KHR reference).
vec3 getIBLRadianceAnisotropy(vec3 N, vec3 V, vec3 anisotropicB,
                              float anisotropy, float perceptualRoughness)
{
  vec3 bentNormal = cross(anisotropicB, V);
  bentNormal = normalize(cross(bentNormal, anisotropicB));
  float a = pow(1.0 - anisotropy * (1.0 - perceptualRoughness), 4.0);
  bentNormal = normalize(mix(bentNormal, N, a));

  vec3 R = reflect(-V, bentNormal);
  R = normalize(mix(R, bentNormal, perceptualRoughness * perceptualRoughness));
  const float MAX_REFLECTION_LOD = 4.0;
  return textureLod(prefilterMap, R, perceptualRoughness * MAX_REFLECTION_LOD).rgb;
}

// Roughness-dependent Fresnel with multi-scattering correction (Fdez-Aguera)
vec3 getIBLGGXFresnel(vec3 N, vec3 V, float roughness, vec3 F0, float specularWeight)
{
  float NdotV = clamp(dot(N, V), 0.0, 1.0);
  vec2 brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0), vec2(1.0));
  vec2 f_ab = texture(brdfLUT, brdfSamplePoint).rg;

  // Roughness-dependent Fresnel
  vec3 Fr = max(vec3(1.0 - roughness), F0) - F0;
  vec3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
  vec3 FssEss = specularWeight * (k_S * f_ab.x + f_ab.y);

  // Multi-scattering correction
  float Ems = 1.0 - (f_ab.x + f_ab.y);
  vec3 F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0);
  vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);

  return FssEss + FmsEms;
}

// ============================================================================
// Main
// ============================================================================

// TODO: Consider adding the Disney diffuse
//       Kulla17 energy conservation
//       4.8.8.2 dielectrics
//       Askikmin (without Fresnel) + Sheen color
void main()
{
  // Fetch this draw's material and resolve effective values. A global UI
  // override (>= 0) replaces the authored value; otherwise the material's
  // own value is used. `flags` merges the global UI toggles with the
  // material's authored capability bits so the `(flags & BIT)` tests below
  // are unchanged from the push-constant-only version.
  GpuMaterial m = matbuf.materials[pc.materialIndex];
  uint flags = pc.globalFlags | m.materialFlags;

  // Per-texture UV addressing: pick the UV set (KHR multi-UV) then apply that
  // slot's KHR_texture_transform (precomputed affine: mat2 + offset).
  #define UVSEL(slot) (((m.uvSets & (1u << (slot))) != 0u) ? fragTexCoord1 : fragTexCoord)
  #define XF(slot, uv) (mat2(m.texXform[2*(slot)].x, m.texXform[2*(slot)].y, \
                             m.texXform[2*(slot)].z, m.texXform[2*(slot)].w) * (uv) \
                        + m.texXform[2*(slot)+1].xy)
  #define UV(slot) XF(slot, UVSEL(slot))
  vec2 uvBase = UV(0);
  vec2 uvNorm = UV(1);
  vec2 uvMR   = UV(2);
  vec2 uvEmis = UV(3);
  vec2 uvAO   = UV(4);
  vec2 uvCC   = UV(5);
  vec2 uvCCR  = UV(6);
  vec2 uvCCN  = UV(7);
  vec2 uvAni  = UV(8);
  #undef UV
  #undef XF
  #undef UVSEL

  vec4  baseColorFactor = m.baseColorFactor;
  float metallicFactor  = (pc.metallicOverride  >= 0.0) ? pc.metallicOverride  : m.metallicFactor;
  float roughnessFactor = (pc.roughnessOverride >= 0.0) ? pc.roughnessOverride : m.roughnessFactor;

  bool  ccOverridden = pc.clearcoatOverride >= 0.0;
  float clearcoatFactor          = ccOverridden ? pc.clearcoatOverride          : m.clearcoatFactor;
  float clearcoatRoughnessFactor = ccOverridden ? pc.clearcoatRoughnessOverride : m.clearcoatRoughnessFactor;

  bool  aniOverridden = pc.anisotropyOverride >= 0.0;
  float anisotropyStrength = aniOverridden ? pc.anisotropyOverride         : m.anisotropyStrength;
  float anisotropyRotation = aniOverridden ? pc.anisotropyRotationOverride : m.anisotropyRotation;

  uint  alphaMode   = m.alphaMode;
  float alphaCutoff = m.alphaCutoff;

  // Alpha (needed by all paths for alpha test / blend)
  vec4 texColor = texture(baseColorTexture, uvBase);
  vec4 baseColor = texColor * baseColorFactor;
  float alpha = baseColor.a;
  if (alphaMode == 0u) alpha = 1.0;                       // opaque
  if (alphaMode == 1u && alpha < alphaCutoff) discard;  // mask

  // ---- Debug early-outs (skip BRDF/IBL when only visualizing a channel) ----

  if (pc.debugMode == 1) {
    // Normals
    vec3 N;
    if ((flags & 1u) != 0u) {
      vec3 nm = texture(normalTexture, uvNorm).rgb * 2.0 - 1.0;
      nm.xy *= m.normalScale;
      N = normalize(fragTBN * nm);
    } else {
      N = normalize(fragNormal);
    }
    outColor = vec4(N * 0.5 + 0.5, alpha);
    return;
  }

  if (pc.debugMode == 2) {
    vec3 albedo = baseColor.rgb;
    if (texColor.r > 0.99 && texColor.g > 0.99 && texColor.b > 0.99 &&
        baseColorFactor.r > 0.99 && baseColorFactor.g > 0.99 && baseColorFactor.b > 0.99)
      albedo = fragColor;
    outColor = vec4(albedo, alpha);
    return;
  }

  if (pc.debugMode == 3) {
    float metallic = clamp(texture(metallicRoughnessTexture, uvMR).b * metallicFactor, 0.0, 1.0);
    outColor = vec4(vec3(metallic), alpha);
    return;
  }

  if (pc.debugMode == 4) {
    float roughness = clamp(texture(metallicRoughnessTexture, uvMR).g * roughnessFactor, 0.0, 1.0);
    outColor = vec4(vec3(roughness), alpha);
    return;
  }

  if (pc.debugMode == 5) {
    outColor = vec4(vec3(texture(aoTexture, uvAO).r), alpha);
    return;
  }

  if (pc.debugMode == 6) {
    outColor = vec4(texture(emissiveTexture, uvEmis).rgb, alpha);
    return;
  }

  if (pc.debugMode == 7) {
    float cc = clearcoatFactor * texture(clearcoatTexture, uvCC).r;
    outColor = vec4(vec3(cc), alpha);
    return;
  }

  if (pc.debugMode == 8) {
    float a = anisotropyStrength;
    if ((flags & 32u) != 0u) a *= texture(anisotropyTexture, uvAni).b;
    outColor = vec4(vec3(a), alpha);
    return;
  }

  // ---- Full PBR path (debugMode == 0 or unknown) ----

  // Normal mapping (toggled by flags bit 0)
  vec3 N;
  if ((flags & 1u) != 0u) {
    vec3 nm = texture(normalTexture, uvNorm).rgb * 2.0 - 1.0;
    nm.xy *= m.normalScale;
    N = normalize(fragTBN * nm);
  } else {
    N = normalize(fragNormal);
  }

  vec3 albedo = baseColor.rgb;

  // Use vertex color when texture + factor are both default white
  if (texColor.r > 0.99 && texColor.g > 0.99 && texColor.b > 0.99 &&
      baseColorFactor.r > 0.99 && baseColorFactor.g > 0.99 && baseColorFactor.b > 0.99) {
    albedo = fragColor;
  }

  // Metallic/roughness (glTF: G=roughness, B=metallic)
  vec4 mrSample = texture(metallicRoughnessTexture, uvMR);
  float perceptualRoughness = clamp(mrSample.g * roughnessFactor, 0.0, 1.0);
  float metallic = clamp(mrSample.b * metallicFactor, 0.0, 1.0);

  // AO (R channel)
  float ao = texture(aoTexture, uvAO).r;

  // Alpha roughness (squared per glTF spec)
  float alphaRoughness = perceptualRoughness * perceptualRoughness;

  // View direction
  vec3 V = normalize(ubo.camPos.xyz - fragPos);

  // Directional light
  vec3 L = normalize(ubo.lightDirection.xyz);
  float lightIntensity = ubo.lightDirection.w;
  vec3 H = normalize(V + L);
  vec3 radiance = ubo.lightColor.rgb * lightIntensity;

  // F0: dielectrics ~0.04, metals use albedo
  vec3 f0_dielectric = vec3(0.04);
  vec3 F0 = mix(f0_dielectric, albedo, metallic);
  vec3 F90 = vec3(1.0);

  // Dot products
  float NdotL = clamp(dot(N, L), 0.0, 1.0);
  float NdotV = clamp(dot(N, V), 0.0, 1.0);
  float NdotH = clamp(dot(N, H), 0.0, 1.0);
  float VdotH = clamp(dot(V, H), 0.0, 1.0);

  // Fresnel
  vec3 F = F_Schlick(F0, F90, VdotH);

  // Specular BRDF + specular IBL — isotropic by default, anisotropic when enabled.
  float specularBRDF;
  vec3 f_specular_ibl;
  if ((flags & 16u) != 0u && anisotropyStrength > 0.0)
  {
    // Anisotropic direction in tangent space, rotated and optionally textured.
    vec2 dirBase = vec2(cos(anisotropyRotation), sin(anisotropyRotation));
    vec2 direction = dirBase;
    float anisotropy = anisotropyStrength;
    if ((flags & 32u) != 0u) {
      vec3 aTex = texture(anisotropyTexture, uvAni).rgb;
      direction = aTex.rg * 2.0 - 1.0;
      direction = mat2(dirBase.x, dirBase.y, -dirBase.y, dirBase.x) * normalize(direction);
      anisotropy *= aTex.b;
    }
    vec3 aniT = normalize(fragTBN * vec3(direction, 0.0));
    vec3 aniB = normalize(cross(N, aniT));

    // Split roughness: stretch along the tangent (at), keep base along bitangent (ab)
    float at = mix(alphaRoughness, 1.0, anisotropy * anisotropy);
    float ab = alphaRoughness;

    float TdotH = dot(aniT, H), BdotH = dot(aniB, H);
    float TdotV = dot(aniT, V), BdotV = dot(aniB, V);
    float TdotL = dot(aniT, L), BdotL = dot(aniB, L);

    specularBRDF = D_GGX_anisotropic(NdotH, TdotH, BdotH, at, ab)
                 * V_GGX_anisotropic(NdotL, NdotV, TdotV, BdotV, TdotL, BdotL, at, ab);
    f_specular_ibl = getIBLRadianceAnisotropy(N, V, aniB, anisotropy, perceptualRoughness);
  }
  else
  {
    specularBRDF = BRDF_specularGGX(alphaRoughness, NdotL, NdotV, NdotH);
    f_specular_ibl = getIBLRadianceGGX(N, V, perceptualRoughness);
  }

  // Diffuse BRDF
  vec3 diffuseBRDF = BRDF_lambertian(albedo);

  // Metallic workflow: metals = specular only, dielectrics = diffuse + specular
  vec3 dielectric_brdf = mix(diffuseBRDF, vec3(specularBRDF), F);
  vec3 metal_brdf = F * specularBRDF;
  vec3 brdf = mix(dielectric_brdf, metal_brdf, metallic);

  // Direct lighting
  vec3 Lo = brdf * radiance * NdotL;

  // IBL ambient lighting (f_specular_ibl computed above, iso or aniso)
  vec3 f_diffuse_ibl = getIBLDiffuseLight(N) * albedo;

  // Metal IBL: specular only with F0 = baseColor
  vec3 f_metal_fresnel = getIBLGGXFresnel(N, V, perceptualRoughness, albedo, 1.0);
  vec3 f_metal_brdf = f_metal_fresnel * f_specular_ibl;

  // Dielectric IBL: energy-conserving mix
  vec3 f_dielectric_fresnel = getIBLGGXFresnel(N, V, perceptualRoughness, f0_dielectric, 1.0);
  vec3 f_dielectric_brdf = mix(f_diffuse_ibl, f_specular_ibl, f_dielectric_fresnel);

  vec3 ambient = mix(f_dielectric_brdf, f_metal_brdf, metallic);

  // Combine
  vec3 color = ambient + Lo;

  // Apply AO
  color *= ao;

  // Add emissive (toggled by flags bit 1)
  if ((flags & 2u) != 0u)
    color += texture(emissiveTexture, uvEmis).rgb;

  // ---- Clear coat (KHR_materials_clearcoat, flags bit 2) ----
  // A thin dielectric film (IOR 1.5, F0 = 0.04) layered over the base material.
  // Follows the glTF Sample Viewer layering: the base is attenuated by the coat's
  // reflectance, then the coat's own specular lobe is added on top.
  if ((flags & 4u) != 0u && clearcoatFactor > 0.0)
  {
    float cc = clearcoatFactor * texture(clearcoatTexture, uvCC).r;
    float ccPerceptualRough = clamp(
      clearcoatRoughnessFactor * texture(clearcoatRoughnessTexture, uvCCR).g,
      0.0, 1.0);
    float ccAlpha = ccPerceptualRough * ccPerceptualRough;

    // Coat normal: dedicated map if present (flags bit 3), else the geometric
    // normal — the smooth coat does NOT inherit the base material's normal map.
    vec3 ccN;
    if ((flags & 8u) != 0u) {
      vec3 nm = texture(clearcoatNormalTexture, uvCCN).rgb * 2.0 - 1.0;
      ccN = normalize(fragTBN * nm);
    } else {
      ccN = normalize(fragNormal);
    }

    float ccNdotL = clamp(dot(ccN, L), 0.0, 1.0);
    float ccNdotV = clamp(dot(ccN, V), 0.0, 1.0);
    float ccNdotH = clamp(dot(ccN, H), 0.0, 1.0);

    const vec3 ccF0 = vec3(0.04);

    // Direct coat specular (Fresnel included, like the base specular lobe)
    float ccSpec = BRDF_specularGGX(ccAlpha, ccNdotL, ccNdotV, ccNdotH);
    vec3 ccLo = vec3(ccSpec) * F_Schlick(ccF0, F90, VdotH) * radiance * ccNdotL;

    // Indirect (IBL) coat specular, occluded by AO
    vec3 ccIBL = getIBLRadianceGGX(ccN, V, ccPerceptualRough) * ao
               * getIBLGGXFresnel(ccN, V, ccPerceptualRough, ccF0, 1.0);

    vec3 f_clearcoat = (ccLo + ccIBL) * cc;

    // Coat Fresnel at the viewing angle drives how much base shows through
    float Fc = F_Schlick(ccF0, F90, ccNdotV).x;
    color = color * (1.0 - cc * Fc) + f_clearcoat;
  }

  outColor = vec4(color, alpha);
}
