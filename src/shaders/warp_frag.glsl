#version 450
#pragma shader_stage(fragment)

layout(set = 0, binding = 1) uniform sampler2D colorSampler;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    //outColor = vec4(fragTexCoord, 0.0, 1.0);
    outColor = texture(colorSampler, fragTexCoord);
}
