#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 color;
    float time;
    int debugMode;
} pc;

void main()
{
    outColor = vec4(pc.color.rgb, 1.0);
}
