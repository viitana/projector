#version 450
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 view;
    mat4 proj;
} globalUbo;

layout(set = 1, binding = 0) uniform UniformBlock {
    mat4 matrix;
    mat4 jointMatrix[64];
    float jointcount;
} objectUbo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    // gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    // gl_Position = objectUbo.matrix * vec4(inPosition, 1.0);
    gl_Position = globalUbo.proj * globalUbo.view * objectUbo.matrix * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
