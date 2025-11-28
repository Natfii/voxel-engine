#version 450

// ========== Vertex Attributes ==========

// Per-vertex data (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec4 inVertexColor;  // Vertex color (RGBA)
layout(location = 5) in ivec4 inBoneIndices; // Bone indices (up to 4 bones)
layout(location = 6) in vec4 inBoneWeights;  // Bone weights (should sum to 1.0)

// Per-instance data (binding 1) - for instanced rendering
layout(location = 7) in mat4 instanceTransform;  // Locations 7-10 (mat4 = 4x vec4)
layout(location = 11) in vec4 instanceTintColor; // Location 11

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

// Bone matrices for skeletal animation (set 2, binding 0)
const int MAX_BONES = 64;
layout(set = 2, binding = 0) uniform BoneUBO {
    mat4 boneMatrices[MAX_BONES];
    int numBones;         // Number of active bones (0 = no skinning)
    float _padding[3];    // Padding to 16-byte alignment
} bones;

// ========== Outputs ==========

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;
layout(location = 5) out vec4 fragTintColor;
layout(location = 6) out float fragTimeOfDay;  // For dynamic lighting
layout(location = 7) out vec4 fragVertexColor; // Vertex color for PS1-style models

// ========== Skinning Function ==========

void applySkinning(inout vec3 position, inout vec3 normal, inout vec3 tangent) {
    // Skip skinning if no bones are active or weights are zero
    float totalWeight = inBoneWeights.x + inBoneWeights.y + inBoneWeights.z + inBoneWeights.w;
    if (bones.numBones == 0 || totalWeight < 0.001) {
        return;
    }

    // Compute skinned position and normal
    vec3 skinnedPos = vec3(0.0);
    vec3 skinnedNormal = vec3(0.0);
    vec3 skinnedTangent = vec3(0.0);

    for (int i = 0; i < 4; i++) {
        float weight = inBoneWeights[i];
        if (weight > 0.0) {
            int boneIdx = inBoneIndices[i];
            if (boneIdx >= 0 && boneIdx < bones.numBones) {
                mat4 boneMatrix = bones.boneMatrices[boneIdx];
                mat3 boneRotation = mat3(boneMatrix);

                skinnedPos += weight * (boneMatrix * vec4(position, 1.0)).xyz;
                skinnedNormal += weight * (boneRotation * normal);
                skinnedTangent += weight * (boneRotation * tangent);
            }
        }
    }

    position = skinnedPos;
    normal = normalize(skinnedNormal);
    tangent = normalize(skinnedTangent);
}

// ========== Main ==========

void main() {
    // Start with original vertex data
    vec3 localPos = inPosition;
    vec3 localNormal = inNormal;
    vec3 localTangent = inTangent;

    // Apply skeletal animation skinning
    applySkinning(localPos, localNormal, localTangent);

    // Transform position to world space
    vec4 worldPos = instanceTransform * vec4(localPos, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform to clip space
    gl_Position = camera.projection * camera.view * worldPos;

    // Transform normals (use mat3 to ignore translation)
    mat3 normalMatrix = mat3(instanceTransform);
    fragNormal = normalize(normalMatrix * localNormal);
    fragTangent = normalize(normalMatrix * localTangent);

    // Calculate bitangent for TBN matrix (used in normal mapping)
    fragBitangent = normalize(cross(fragNormal, fragTangent));

    // Pass through texture coordinates
    fragTexCoord = inTexCoord;

    // Combine instance tint with vertex color
    fragTintColor = instanceTintColor;
    fragVertexColor = inVertexColor;

    // Pass through time of day for dynamic lighting
    fragTimeOfDay = camera.skyTimeData.x;
}
