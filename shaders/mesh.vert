#version 450

// ========== Vertex Attributes ==========

// Per-vertex data (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

// Per-instance data (binding 1) - for instanced rendering
layout(location = 4) in mat4 instanceTransform;  // Locations 4-7 (mat4 = 4x vec4)
layout(location = 8) in vec4 instanceTintColor;  // Location 8

// ========== Uniforms ==========

layout(binding = 0) uniform CameraUBO {
    mat4 model;           // Not used for meshes (use instanceTransform instead)
    mat4 view;
    mat4 projection;
    vec4 cameraPos;       // Camera position (.xyz) + render distance (.w)
    vec4 skyTimeData;     // Time data (.x=time 0-1, .y=sun, .z=moon, .w=underwater)
    vec4 liquidFogColor;  // Not used for meshes
    vec4 liquidFogDist;   // Not used for meshes
    vec4 liquidTint;      // Not used for meshes
} camera;

// ========== Outputs ==========

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;
layout(location = 5) out vec4 fragTintColor;
layout(location = 6) out float fragTimeOfDay;  // For dynamic lighting

// ========== Main ==========

void main() {
    // Transform position to world space
    vec4 worldPos = instanceTransform * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform to clip space
    gl_Position = camera.projection * camera.view * worldPos;

    // Transform normals (use mat3 to ignore translation)
    mat3 normalMatrix = mat3(instanceTransform);
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent = normalize(normalMatrix * inTangent);

    // Calculate bitangent for TBN matrix (used in normal mapping)
    fragBitangent = normalize(cross(fragNormal, fragTangent));

    // Pass through texture coordinates
    fragTexCoord = inTexCoord;

    // Pass through tint color
    fragTintColor = instanceTintColor;

    // Pass through time of day for dynamic lighting
    fragTimeOfDay = camera.skyTimeData.x;
}
