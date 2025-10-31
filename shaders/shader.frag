#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float renderDistance;
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Calculate distance from camera to fragment
    float distance = length(fragWorldPos - ubo.cameraPos);

    // Fog parameters (similar to classic OpenGL voxel engines)
    const vec3 fogColor = vec3(0.6, 0.8, 1.0); // Light blue sky color
    const float fogStart = ubo.renderDistance * 0.7;  // Fog starts at 70% of render distance
    const float fogEnd = ubo.renderDistance;          // Full fog at render distance

    // Discard fragments beyond render distance (hard culling)
    if (distance > fogEnd) {
        discard;
    }

    // Calculate linear fog factor
    float fogFactor = clamp((fogEnd - distance) / (fogEnd - fogStart), 0.0, 1.0);

    // Mix block color with fog color
    vec3 finalColor = mix(fogColor, fragColor, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
