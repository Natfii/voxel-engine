#version 450

// ============================================================================
// SIMPLE SPHERE FRAGMENT SHADER
// Used for loading screen sphere with map preview texture
// ============================================================================

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample texture and multiply by vertex color
    vec4 texColor = texture(texSampler, fragTexCoord);
    outColor = texColor * fragColor;
}
