#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrImage;

layout(push_constant) uniform PushConstants {
    float exposure;
    int debugMode;
} pc;

void main()
{
    vec3 hdr = texture(hdrImage, fragUV).rgb;

    // Reinhard tonemap with exposure
    vec3 mapped = vec3(1.0) - exp(-hdr * pc.exposure);

    // Gamma correction
    outColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
