#version 450
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 view;
    mat4 proj;
} globalUbo;

layout(location = 0) out vec2 fragTexCoord;

vec2 positions[6] = vec2[](
    vec2(-0.5, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5),
    vec2(-0.5, -0.5),
    vec2(0.5, -0.5),
    vec2(0.5, 0.5)
);

vec2 uvs[6] = vec2[](
    vec2(0, 0),
    vec2(1, 1),
    vec2(0, 1),
    vec2(0, 0),
    vec2(1, 0),
    vec2(1, 1)
);

void main() {
    float aspect = globalUbo.proj[1][1] / globalUbo.proj[0][0];

    gl_Position =
        globalUbo.proj *
        globalUbo.view *
        vec4(
            positions[gl_VertexIndex].x * aspect,
            positions[gl_VertexIndex].y,
            0.0,
            1.0
        );
    fragTexCoord = uvs[gl_VertexIndex];
}
