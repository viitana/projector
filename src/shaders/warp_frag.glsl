#version 450
#pragma shader_stage(fragment)

layout(set = 0, binding = 1) uniform sampler2D colorSampler;
layout(set = 0, binding = 2) uniform sampler2D depthSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float depthBlend;

layout(location = 0) out vec4 outColor;

const float depthContrast = 20.0f;
const float depthBrightness = 0.9f;

void main() {
    vec4 color = texture(colorSampler, fragTexCoord);

    float depthValue = 1.0f - texture(depthSampler, fragTexCoord).x;
    depthValue = depthValue * 20.f;
    vec4 depth = vec4(depthValue, depthValue, depthValue, 1);

    outColor = mix(color, depth, depthBlend);
}
