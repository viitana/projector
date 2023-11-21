#version 450
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 inverseProj;
    mat4 screen;
    ivec2 gridResolution;
    float screenScale;
    float uvScale;
    float depthBlend;
} ubo;
layout(set = 0, binding = 3) uniform sampler2D depthSampler;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out float depthBlend;

const vec2 positions[6] = vec2[](
    vec2(0, 0),
    vec2(1, 1),
    vec2(0, 1),
    vec2(0, 0),
    vec2(1, 0),
    vec2(1, 1)
);

vec2 getPos(int idx) {
    int sqIdx = idx / 6;
    int vertIdx = idx % 6;
    int sectionX = sqIdx % ubo.gridResolution.x;
    int sectionY = sqIdx / ubo.gridResolution.x;

    return vec2(
        (((sectionX + positions[vertIdx].x) / ubo.gridResolution.x)  - 0.5f) * 2.0f,
        (((sectionY + positions[vertIdx].y) / ubo.gridResolution.y) - 0.5f) * 2.0f
    );
}

vec2 getUV(int idx) {
    int sqIdx = idx / 6;
    int vertIdx = idx % 6;
    int sectionX = sqIdx % ubo.gridResolution.x;
    int sectionY = sqIdx / ubo.gridResolution.x;

    return vec2(
        (sectionX + positions[vertIdx].x) / ubo.gridResolution.x,
        (sectionY + positions[vertIdx].y) / ubo.gridResolution.y
    );
}

void main() {
    vec2 pos = positions[gl_VertexIndex] - 0.5f;
    vec2 uv = positions[gl_VertexIndex];

    pos = getPos(gl_VertexIndex);
    uv = getUV(gl_VertexIndex);

    gl_Position =
        ubo.proj *
        ubo.view *
        ubo.screen *
        ubo.inverseProj *
        vec4(
            pos.x,
            pos.y,
            texture(depthSampler, uv).x,
            1.0
        );
    fragTexCoord = uv;
    depthBlend = ubo.depthBlend;
}
