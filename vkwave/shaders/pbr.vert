#version 450

// PBR vertex shader — adapted from Vulkanstein3D's vertex.vert
// Computes TBN matrix for normal mapping with Gram-Schmidt re-orthogonalization.

layout(set = 0, binding = 0) uniform PbrUBO {
  mat4 viewProj;
  vec4 camPos;
  vec4 lightDirection;
  vec4 lightColor;
} ubo;

// Vertex attributes (matches vkwave::Vertex)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;  // xyz=tangent, w=handedness
layout(location = 5) in vec2 inTexCoord1; // second UV set (glTF TEXCOORD_1)

// Push constant — must match PbrPushConstants (C++) and pbr.frag exactly.
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

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) out mat3 fragTBN;  // locations 4, 5, 6
layout(location = 7) out vec2 fragTexCoord1;

void main()
{
  vec4 worldPos = pc.model * vec4(inPosition, 1.0);
  fragPos = worldPos.xyz;

  gl_Position = ubo.viewProj * worldPos;
  fragColor = inColor;
  fragTexCoord = inTexCoord;
  fragTexCoord1 = inTexCoord1;

  // Transform normal by model matrix (upper 3x3)
  mat3 normalMatrix = mat3(pc.model);
  fragNormal = normalize(normalMatrix * inNormal);

  // Compute TBN matrix for normal mapping
  vec3 N = fragNormal;

  // Construct TBN matrix for normal mapping
  bool validTBN = false;

  if (dot(inTangent.xyz, inTangent.xyz) > 0.001) {
    // Mesh provides tangent data
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    vec3 Tortho = T - dot(T, N) * N;
    float lenT = length(Tortho);
    if (lenT > 0.001) {
      T = Tortho / lenT;
      // Bitangent: cross product with handedness from tangent.w
      vec3 B = cross(N, T) * inTangent.w;
      fragTBN = mat3(T, B, N);
      validTBN = true;
    }
  }

  if (!validTBN) {
    // No tangent data or degenerate (tangent parallel to normal)
    // Construct arbitrary TBN from normal
    vec3 T = abs(N.y) < 0.99
      ? normalize(cross(N, vec3(0, 1, 0)))
      : normalize(cross(N, vec3(1, 0, 0)));
    vec3 B = cross(N, T);
    fragTBN = mat3(T, B, N);
  }
}
