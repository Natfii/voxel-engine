#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = camera position, .w = render distance
    vec4 skyTimeData;  // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;  // Now vec4 with alpha channel
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 fragColor;  // Now vec4 with alpha
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragWaveIntensity;  // For wave effects

void main() {
    // Calculate world position
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    vec3 finalPosition = worldPos.xyz;

    // Wave effects disabled - caused geometry distortion on water top faces
    // Water animation now handled purely by scrolling texture in fragment shader
    float waveIntensity = 0.0;

    fragWorldPos = finalPosition;
    fragWaveIntensity = waveIntensity;

    gl_Position = ubo.projection * ubo.view * vec4(finalPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
