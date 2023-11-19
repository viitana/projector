#version 450
#pragma shader_stage(fragment)

layout(set = 0, binding = 1) uniform sampler2D colorSampler;
layout(set = 0, binding = 2) uniform sampler2D depthSampler;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    //outColor = vec4(fragTexCoord, 0.0, 1.0);

    float depth = texture(depthSampler, fragTexCoord).x;

    //NearClipPlane + DepthBufferValue * (FarClipPlane - NearClipPlane);
    
    //depth = 0.001 + depth * (100.0 - 0.001);

    depth = 1 - depth;
    depth = depth * 3;

    //depth = depth * 100;
    // depth = depth - 0.5;
    // depth = depth * 2;

    //depth = depth - 1

    //outColor = texture(colorSampler, fragTexCoord);
    if (fragTexCoord.x < 0.3f)
    {
         outColor = texture(colorSampler, fragTexCoord);
    }
    else
    {
        // if (depth > 1)
        // {
        //     outColor = vec4(1, 0, 0, 1);
        // }
        // else
        {
            outColor = vec4(depth, depth, depth, 1);
        }
    }
    
    //float fragx = (gl_FragCoord.x / 2.0f) + 0.5f;
    //float fragy = (gl_FragCoord.y / 2.0f) + 0.5f;
    //outColor = texture(colorSampler, vec2(gl_FragCoord.x / 1920.0f, gl_FragCoord.y / 1080.0f));
}
