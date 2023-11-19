#version 450
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform GlobalUniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 screen;
    float screenScale;
    float uvScale;
} ubo;
layout(set = 0, binding = 2) uniform sampler2D depthSampler;

layout(location = 0) out vec2 fragTexCoord;

const vec2 positions[6] = vec2[](
    vec2(0, 0),
    vec2(1, 1),
    vec2(0, 1),
    vec2(0, 0),
    vec2(1, 0),
    vec2(1, 1)
);

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

const int meshWidth = 64;
const int meshHeight = 48;

vec2 getPos(int idx) {
    int sqIdx = idx / 6;
    int vertIdx = idx % 6;
    int sectionX = sqIdx % meshWidth;
    int sectionY = sqIdx / meshWidth;

    return vec2(
        ((sectionX + positions[vertIdx].x) / meshWidth)  - 0.5f,
        ((sectionY + positions[vertIdx].y) / meshHeight) - 0.5f
    );
}

vec2 getUV(int idx) {
    int sqIdx = idx / 6;
    int vertIdx = idx % 6;
    int sectionX = sqIdx % meshWidth;
    int sectionY = sqIdx / meshWidth;

    return vec2(
        ((sectionX + positions[vertIdx].x) / meshWidth),
        ((sectionY + positions[vertIdx].y) / meshHeight)
    );
}

void main() {
    float aspect = ubo.proj[1][1] / ubo.proj[0][0];

    vec2 pos = positions[gl_VertexIndex] - 0.5f;
    vec2 uv = positions[gl_VertexIndex];

    pos = getPos(gl_VertexIndex);
    uv = getUV(gl_VertexIndex);

    float z = rand(uv);
    
    z = texture(depthSampler, uv).x;
    
    gl_Position =
        ubo.proj *
        ubo.view *
        ubo.screen *
        vec4(
            ubo.screenScale * pos.x * aspect,
            ubo.screenScale * pos.y,
            0,
            1.0
        );
    fragTexCoord = (((uv - 0.5f) * ubo.uvScale) + 0.5f);
}
