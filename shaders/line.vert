#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = camera position, .w = render distance (unused in line shader)
    vec4 skyTimeData;  // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity (unused in line shader)
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}
