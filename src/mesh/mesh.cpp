/**
 * @file mesh.cpp
 * @brief Implementation of core mesh data structures
 */

#include "mesh/mesh.h"
#include <limits>
#include <cstring>

// ========== MeshVertex ==========

VkVertexInputBindingDescription MeshVertex::getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(MeshVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription> MeshVertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(5);

    // Position (location 0)
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(MeshVertex, position);

    // Normal (location 1)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(MeshVertex, normal);

    // TexCoord (location 2)
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(MeshVertex, texCoord);

    // Tangent (location 3)
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(MeshVertex, tangent);

    // Vertex Color (location 4)
    attributeDescriptions[4].binding = 0;
    attributeDescriptions[4].location = 4;
    attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[4].offset = offsetof(MeshVertex, color);

    return attributeDescriptions;
}

// ========== PBRMaterial ==========

PBRMaterial PBRMaterial::createDefault() {
    PBRMaterial mat;
    mat.baseColor = glm::vec4(1.0f);  // White
    mat.metallic = 0.0f;              // Non-metallic
    mat.roughness = 0.5f;             // Medium roughness
    mat.emissive = 0.0f;              // No emission
    return mat;
}

PBRMaterial PBRMaterial::createDebug(const glm::vec3& color) {
    PBRMaterial mat;
    mat.baseColor = glm::vec4(color, 1.0f);
    mat.metallic = 0.0f;
    mat.roughness = 0.8f;  // Slightly rough for visibility
    mat.emissive = 0.0f;
    return mat;
}

// ========== MaterialUBO ==========

MaterialUBO::MaterialUBO(const PBRMaterial& mat) {
    baseColor = mat.baseColor;
    metallic = mat.metallic;
    roughness = mat.roughness;
    emissive = mat.emissive;
    alphaCutoff = mat.alphaCutoff;
    albedoTexIndex = mat.albedoTexture;
    normalTexIndex = mat.normalTexture;
    metallicRoughnessTexIndex = mat.metallicRoughnessTexture;
    emissiveTexIndex = mat.emissiveTexture;
}

// ========== Mesh ==========

Mesh::Mesh(const std::string& name,
           const std::vector<MeshVertex>& vertices,
           const std::vector<uint32_t>& indices,
           uint32_t materialIndex)
    : name(name)
    , vertices(vertices)
    , indices(indices)
    , materialIndex(materialIndex) {
    calculateBounds();
}

void Mesh::calculateBounds() {
    if (vertices.empty()) {
        boundsMin = glm::vec3(0.0f);
        boundsMax = glm::vec3(0.0f);
        return;
    }

    boundsMin = glm::vec3(std::numeric_limits<float>::max());
    boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto& vertex : vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
}

void Mesh::calculateTangents() {
    if (indices.empty() || vertices.empty()) {
        return;
    }

    // Calculate tangents per triangle
    for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) break;

        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        MeshVertex& v0 = vertices[i0];
        MeshVertex& v1 = vertices[i1];
        MeshVertex& v2 = vertices[i2];

        // Edge vectors
        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;

        // UV deltas
        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        // Calculate tangent
        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(det) < 0.0001f) {
            // Degenerate triangle or UV, use default tangent
            continue;
        }

        float f = 1.0f / det;
        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        // Accumulate tangents (average across shared vertices)
        v0.tangent += tangent;
        v1.tangent += tangent;
        v2.tangent += tangent;
    }

    // Normalize tangents and apply Gram-Schmidt orthogonalization
    for (auto& vertex : vertices) {
        if (glm::length(vertex.tangent) > 0.0001f) {
            // Gram-Schmidt: T' = normalize(T - (N · T) * N)
            vertex.tangent = glm::normalize(
                vertex.tangent - vertex.normal * glm::dot(vertex.normal, vertex.tangent)
            );
        } else {
            // No valid tangent computed, create perpendicular to normal
            if (std::abs(vertex.normal.x) < 0.9f) {
                vertex.tangent = glm::normalize(glm::cross(vertex.normal, glm::vec3(1, 0, 0)));
            } else {
                vertex.tangent = glm::normalize(glm::cross(vertex.normal, glm::vec3(0, 1, 0)));
            }
        }
    }
}

size_t Mesh::getGPUMemoryUsage() const {
    size_t total = 0;
    if (vertexBuffer != VK_NULL_HANDLE) {
        total += vertices.size() * sizeof(MeshVertex);
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        total += indices.size() * sizeof(uint32_t);
    }
    return total;
}

size_t Mesh::getCPUMemoryUsage() const {
    return vertices.size() * sizeof(MeshVertex) + indices.size() * sizeof(uint32_t);
}

// ========== InstanceData ==========

VkVertexInputBindingDescription InstanceData::getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 1;  // Instance buffer is binding 1
    bindingDescription.stride = sizeof(InstanceData);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription> InstanceData::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(5);

    // mat4 transform (takes 4 attribute slots, locations 5-8)
    // Location 4 is now used by vertex color
    for (uint32_t i = 0; i < 4; i++) {
        attributeDescriptions[i].binding = 1;
        attributeDescriptions[i].location = 5 + i;
        attributeDescriptions[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[i].offset = offsetof(InstanceData, transform) + sizeof(glm::vec4) * i;
    }

    // vec4 tintColor (location 9)
    attributeDescriptions[4].binding = 1;
    attributeDescriptions[4].location = 9;
    attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[4].offset = offsetof(InstanceData, tintColor);

    return attributeDescriptions;
}
