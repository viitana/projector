#version 450
#pragma optimize(off)
#pragma shader_stage(fragment)

layout(set = 2, binding = 0) uniform sampler2D colorSampler;
layout(set = 2, binding = 1) uniform sampler2D normalSampler;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
    // outColor = fragColor;
    outColor = texture(colorSampler, fragTexCoord);

    // const int n = 100;

    // vec4 color = vec4(0);
    // for (int x = -n; x <= n; x++)
    // for (int y = -n; y <= n; y++)
    // {
    //     color = color + texture(colorSampler, vec2(fragTexCoord.x + x, fragTexCoord.y + y));
    // }
    // color = color / (4 * n * n);

    // outColor = color;
}
