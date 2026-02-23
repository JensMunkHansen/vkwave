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

// Set 2: Per-scene globals (bound once per frame)
layout(set = 2, binding = 0) uniform sampler2D brdfLUT;
layout(set = 2, binding = 1) uniform samplerCube irradianceMap;
layout(set = 2, binding = 2) uniform samplerCube prefilterMap;

layout(push_constant) uniform PushConstants {
  mat4 model;
  vec4 baseColorFactor;
  float metallicFactor;
  float roughnessFactor;
  float time;
  int debugMode;
  uint flags;
  uint alphaMode;
  float alphaCutoff;
} pc;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec2 fragTexCoord;
layout(location = 4) in mat3 fragTBN;

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

void main()
{
  // Normal mapping (toggled by flags bit 0)
  vec3 N;
  if ((pc.flags & 1u) != 0u) {
    vec3 nm = texture(normalTexture, fragTexCoord).rgb * 2.0 - 1.0;
    N = normalize(fragTBN * nm);
  } else {
    N = normalize(fragNormal);
  }

  // Base color (sRGB texture — GPU converts to linear on sample)
  vec4 texColor = texture(baseColorTexture, fragTexCoord);
  vec4 baseColor = texColor * pc.baseColorFactor;
  vec3 albedo = baseColor.rgb;

  // Alpha handling
  float alpha = baseColor.a;
  if (pc.alphaMode == 0u) alpha = 1.0;                       // opaque
  if (pc.alphaMode == 1u && alpha < pc.alphaCutoff) discard;  // mask
  // alphaMode 2 (blend): alpha passes through

  // Use vertex color when texture + factor are both default white
  if (texColor.r > 0.99 && texColor.g > 0.99 && texColor.b > 0.99 &&
      pc.baseColorFactor.r > 0.99 && pc.baseColorFactor.g > 0.99 && pc.baseColorFactor.b > 0.99) {
    albedo = fragColor;
  }

  // Metallic/roughness (glTF: G=roughness, B=metallic)
  vec4 mrSample = texture(metallicRoughnessTexture, fragTexCoord);
  float perceptualRoughness = clamp(mrSample.g * pc.roughnessFactor, 0.0, 1.0);
  float metallic = clamp(mrSample.b * pc.metallicFactor, 0.0, 1.0);

  // AO (R channel)
  float ao = texture(aoTexture, fragTexCoord).r;

  // Emissive
  vec3 emissive = texture(emissiveTexture, fragTexCoord).rgb;

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

  // Specular BRDF
  float specularBRDF = BRDF_specularGGX(alphaRoughness, NdotL, NdotV, NdotH);

  // Diffuse BRDF
  vec3 diffuseBRDF = BRDF_lambertian(albedo);

  // Metallic workflow: metals = specular only, dielectrics = diffuse + specular
  vec3 dielectric_brdf = mix(diffuseBRDF, vec3(specularBRDF), F);
  vec3 metal_brdf = F * specularBRDF;
  vec3 brdf = mix(dielectric_brdf, metal_brdf, metallic);

  // Direct lighting
  vec3 Lo = brdf * radiance * NdotL;

  // IBL ambient lighting
  vec3 f_diffuse_ibl = getIBLDiffuseLight(N) * albedo;
  vec3 f_specular_ibl = getIBLRadianceGGX(N, V, perceptualRoughness);

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
  if ((pc.flags & 2u) != 0u)
    color += emissive;

  // Debug modes
  switch(pc.debugMode) {
    case 0: outColor = vec4(color, alpha); break;           // Final HDR
    case 1: outColor = vec4(N * 0.5 + 0.5, alpha); break;  // Normals
    case 2: outColor = vec4(albedo, alpha); break;           // Base color
    case 3: outColor = vec4(vec3(metallic), alpha); break;   // Metallic
    case 4: outColor = vec4(vec3(perceptualRoughness), alpha); break; // Roughness
    case 5: outColor = vec4(vec3(ao), alpha); break;         // AO
    case 6: outColor = vec4(emissive, alpha); break;         // Emissive
    default: outColor = vec4(color, alpha); break;
  }
}
