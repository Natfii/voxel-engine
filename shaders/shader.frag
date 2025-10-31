#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;  // .xyz = camera position, .w = render distance
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Extract camera position and render distance from packed vec4
    vec3 camPos = ubo.cameraPos.xyz;
    float renderDistance = ubo.cameraPos.w;

    // Calculate distance from camera to fragment
    float distance = length(fragWorldPos - camPos);

    // Fog parameters (similar to classic OpenGL voxel engines)
    const vec3 fogColor = vec3(0.6, 0.8, 1.0); // Light blue sky color
    const float fogStart = renderDistance * 0.7;   // Fog starts at 70% of render distance
    const float fogEnd = renderDistance * 0.95;    // Full fog at 95% of render distance

    // Discard fragments well beyond fog end to avoid visible banding
    // This happens after blocks are completely hidden by fog
    if (distance > renderDistance * 1.05) {
        discard;
    }

    // Only apply fog if we're in the fog range
    vec3 finalColor = fragColor;
    if (distance > fogStart) {
        // Calculate linear fog factor (1.0 = no fog, 0.0 = full fog)
        float fogFactor = clamp((fogEnd - distance) / (fogEnd - fogStart), 0.0, 1.0);
        // Mix block color with fog color
        finalColor = mix(fogColor, fragColor, fogFactor);
    }

    outColor = vec4(finalColor, 1.0);
}
