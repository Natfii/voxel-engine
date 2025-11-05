#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = camera position, .w = render distance
    vec4 skyTimeData;  // .x = time of day (0-1), .y = sun intensity, .z = moon intensity, .w = star intensity
} ubo;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragTexCoord;  // Direction vector for cube map sampling

void main() {
    // Remove translation from view matrix (keep camera at center of skybox)
    mat4 viewNoTranslation = mat4(mat3(ubo.view));

    // Position skybox at far plane (w = w for perspective divide trick)
    vec4 pos = ubo.projection * viewNoTranslation * vec4(inPosition, 1.0);
    gl_Position = pos.xyww;  // Set z = w so after perspective divide, z/w = 1.0 (far plane)

    // Use vertex position as texture coordinate direction
    fragTexCoord = inPosition;
}
