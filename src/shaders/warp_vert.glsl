#version 450
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 screen;
    float overflow;
} ubo;

layout(location = 0) out vec2 fragTexCoord;

const vec2 positions[6] = vec2[](
    vec2(-0.5, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5),
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2( 0.5,  0.5)
);

const vec2 uvs[6] = vec2[](
    vec2(0, 0),
    vec2(1, 1),
    vec2(0, 1),
    vec2(0, 0),
    vec2(1, 0),
    vec2(1, 1)
);

void main() {
    float aspect = ubo.proj[1][1] / ubo.proj[0][0];

    gl_Position =
        ubo.proj *
        ubo.view *
        ubo.screen *
        vec4(
            ubo.overflow * positions[gl_VertexIndex].x * aspect,
            ubo.overflow * positions[gl_VertexIndex].y,
            0.0,
            1.0
        );
    fragTexCoord = (((uvs[gl_VertexIndex] - 0.5f) * ubo.overflow) + 0.5f);
}
