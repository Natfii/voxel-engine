#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = camera position, .w = render distance
    vec4 skyTimeData;  // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec2 fragTexCoord;

void main() {
    // Calculate world position
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = ubo.projection * ubo.view * worldPos;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
