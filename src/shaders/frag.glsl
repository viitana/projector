#version 450
#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform sampler2D colorSampler;
layout(set = 1, binding = 1) uniform sampler2D normalSampler;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
    // outColor = fragColor;
    outColor = texture(colorSampler, fragTexCoord);
}
