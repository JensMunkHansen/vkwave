#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    int debugMode;
} pc;

void main()
{
    vec3 N = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    float ambient = 0.15;
    float diffuse = max(dot(N, lightDir), 0.0);
    vec3 lit = fragColor * (ambient + diffuse);

    switch(pc.debugMode) {
        case 0: outColor = vec4(lit, 1.0); break;
        case 1: outColor = vec4(N * 0.5 + 0.5, 1.0); break;
    }
}
