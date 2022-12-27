#version 450
#pragma shader_stage(vertex)

vec2 positions[6] = vec2[](
    vec2(-0.5, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5),
    vec2(-0.5, -0.5),
    vec2(0.5, 0.5),
    vec2(0.5, 0.5)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
