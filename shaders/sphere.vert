#version 450

// ============================================================================
// SIMPLE SPHERE VERTEX SHADER
// Uses the old 48-byte Vertex format for loading screen sphere
// Layout MUST match Vertex::getAttributeDescriptions() in chunk.h
// ============================================================================

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 skyTimeData;
    vec4 liquidFogColor;
    vec4 liquidFogDist;
    vec4 liquidTint;
} ubo;

// Old Vertex format matching chunk.h Vertex struct
// NOTE: Location order matches Vertex::getAttributeDescriptions()
layout(location = 0) in vec3 inPosition;   // x, y, z
layout(location = 1) in vec4 inColor;      // r, g, b, a
layout(location = 2) in vec2 inTexCoord;   // u, v
layout(location = 3) in float inSkyLight;
layout(location = 4) in float inBlockLight;
layout(location = 5) in float inAO;

// Outputs to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main() {
    // Transform position through MVP
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.projection * ubo.view * worldPos;

    // Pass through texture coordinates and color
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
