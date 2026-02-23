// ============================================================================
// Tone Mapping Functions (from Khronos glTF-Sample-Viewer)
// Include with: #include "tonemap.glsl"
// ============================================================================

// Reinhard tone mapping (simple)
vec3 toneMapReinhard(vec3 color)
{
  return color / (color + vec3(1.0));
}

// ACES filmic tone map (faster approximation)
// see: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 toneMapACES_Narkowicz(vec3 color)
{
  const float A = 2.51;
  const float B = 0.03;
  const float C = 2.43;
  const float D = 0.59;
  const float E = 0.14;
  return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3(
  0.59719, 0.07600, 0.02840,
  0.35458, 0.90834, 0.13383,
  0.04823, 0.01566, 0.83777
);

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3(
   1.60475, -0.10208, -0.00327,
  -0.53108,  1.10813, -0.07276,
  -0.07367, -0.00605,  1.07602
);

// ACES filmic tone map approximation (more accurate)
// see https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 RRTAndODTFit(vec3 color)
{
  vec3 a = color * (color + 0.0245786) - 0.000090537;
  vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
  return a / b;
}

vec3 toneMapACES_Hill(vec3 color)
{
  color = ACESInputMat * color;
  color = RRTAndODTFit(color);
  color = ACESOutputMat * color;
  return clamp(color, 0.0, 1.0);
}

// Khronos PBR Neutral tone mapping
// Reference: https://github.com/KhronosGroup/ToneMapping
vec3 toneMapKhronosPbrNeutral(vec3 color)
{
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1.0 - startCompression;
  float newPeak = 1.0 - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
  return mix(color, vec3(newPeak), g);
}

// Dispatcher: apply tone mapping by mode index
// 0 = None, 1 = Reinhard, 2 = ACES Narkowicz, 3 = ACES Hill,
// 4 = ACES Hill + Boost, 5 = Khronos PBR Neutral
vec3 applyToneMap(vec3 color, int mode)
{
  if (mode == 1) {
    return toneMapReinhard(color);
  } else if (mode == 2) {
    return toneMapACES_Narkowicz(color);
  } else if (mode == 3) {
    return toneMapACES_Hill(color);
  } else if (mode == 4) {
    return toneMapACES_Hill(color / 0.6);  // Exposure boost
  } else if (mode == 5) {
    return toneMapKhronosPbrNeutral(color);
  }
  return color;  // mode 0: no tone mapping
}
