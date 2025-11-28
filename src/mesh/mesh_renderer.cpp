/**
 * @file mesh_renderer.cpp
 * @brief High-level mesh rendering system implementation
 */

#include "mesh/mesh_renderer.h"
#include "vulkan_renderer.h"
#include "logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>

// ========== Construction/Destruction ==========

MeshRenderer::MeshRenderer(VulkanRenderer* renderer)
    : m_renderer(renderer) {

    // Initialize texture resources
    initializeTextureResources();

    // Create default material
    PBRMaterial defaultMat = PBRMaterial::createDefault();
    m_defaultMaterialId = createMaterial(defaultMat);

    Logger::info() << "MeshRenderer initialized with default material and texture support";
}

MeshRenderer::~MeshRenderer() {
    Logger::info() << "Cleaning up MeshRenderer...";

    // Wait for GPU before cleanup to ensure all buffers are safe to delete
    m_renderer->waitForGPUIdle();

    // Process any remaining pending deletions
    for (const auto& pending : m_pendingDeletions) {
        if (pending.buffer != VK_NULL_HANDLE) {
            m_renderer->destroyMeshBuffers(pending.buffer, VK_NULL_HANDLE,
                                          pending.memory, VK_NULL_HANDLE);
        }
    }
    m_pendingDeletions.clear();

    // Destroy all meshes (also destroys buffers)
    for (auto& [id, meshData] : m_meshes) {
        if (meshData.mesh.hasGPUBuffers()) {
            m_renderer->destroyMeshBuffers(meshData.mesh.vertexBuffer, meshData.mesh.indexBuffer,
                                          meshData.mesh.vertexMemory, meshData.mesh.indexMemory);
        }
        if (meshData.instanceBuffer != VK_NULL_HANDLE) {
            m_renderer->destroyMeshBuffers(meshData.instanceBuffer, VK_NULL_HANDLE,
                                          meshData.instanceMemory, VK_NULL_HANDLE);
        }
    }

    // Destroy all materials
    for (auto& [id, matData] : m_materials) {
        if (matData.uniformBuffer != VK_NULL_HANDLE) {
            m_renderer->destroyMaterialBuffer(matData.uniformBuffer, matData.uniformMemory);
        }
    }

    // Clean up textures
    cleanupTextures();

    // Clean up bone buffer
    cleanupBoneBuffer();

    Logger::info() << "MeshRenderer cleanup complete";
}

// ========== Deferred Buffer Deletion ==========

void MeshRenderer::queueBufferDeletion(VkBuffer buffer, VkDeviceMemory memory) {
    if (buffer == VK_NULL_HANDLE) return;

    PendingDeletion pending;
    pending.buffer = buffer;
    pending.memory = memory;
    pending.frameNumber = m_frameNumber;
    m_pendingDeletions.push_back(pending);
}

void MeshRenderer::processPendingDeletions() {
    // Remove buffers that have been queued for enough frames
    auto it = m_pendingDeletions.begin();
    while (it != m_pendingDeletions.end()) {
        if (m_frameNumber - it->frameNumber >= FRAMES_TO_KEEP) {
            // Safe to delete now
            m_renderer->destroyMeshBuffers(it->buffer, VK_NULL_HANDLE,
                                          it->memory, VK_NULL_HANDLE);
            it = m_pendingDeletions.erase(it);
        } else {
            ++it;
        }
    }
}

// ========== Mesh Management ==========

uint32_t MeshRenderer::loadMeshFromFile(const std::string& filepath) {
    try {
        std::vector<PBRMaterial> materials;
        std::vector<Mesh> meshes = MeshLoader::loadOBJ(filepath, materials);

        if (meshes.empty()) {
            Logger::error() << "No meshes loaded from: " << filepath;
            return 0;
        }

        // For now, just load the first mesh (multi-mesh OBJ support later)
        Mesh& mesh = meshes[0];

        // Create material if provided
        uint32_t materialId = m_defaultMaterialId;
        if (!materials.empty()) {
            materialId = createMaterial(materials[0]);
        }

        mesh.materialIndex = materialId;
        return createMesh(mesh);

    } catch (const std::exception& e) {
        Logger::error() << "Failed to load mesh from " << filepath << ": " << e.what();
        return 0;
    }
}

uint32_t MeshRenderer::createMesh(const Mesh& mesh) {
    uint32_t meshId = m_nextMeshId++;

    MeshData meshData;
    meshData.mesh = mesh;
    meshData.materialId = mesh.materialIndex;

    // Upload mesh to GPU
    uploadMesh(meshData);

    m_meshes[meshId] = std::move(meshData);

    Logger::info() << "Created mesh " << meshId << ": " << mesh.vertices.size()
                  << " vertices, " << mesh.indices.size() / 3 << " triangles";

    return meshId;
}

void MeshRenderer::removeMesh(uint32_t meshId) {
    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) {
        Logger::warning() << "Mesh " << meshId << " not found";
        return;
    }

    MeshData& meshData = it->second;

    // Remove all instances
    for (uint32_t instanceId : meshData.instances) {
        m_instances.erase(instanceId);
        m_instanceCount--;
    }

    // Destroy GPU resources
    if (meshData.mesh.hasGPUBuffers()) {
        m_renderer->destroyMeshBuffers(meshData.mesh.vertexBuffer, meshData.mesh.indexBuffer,
                                      meshData.mesh.vertexMemory, meshData.mesh.indexMemory);
    }
    if (meshData.instanceBuffer != VK_NULL_HANDLE) {
        m_renderer->destroyMeshBuffers(meshData.instanceBuffer, VK_NULL_HANDLE,
                                      meshData.instanceMemory, VK_NULL_HANDLE);
    }

    m_meshes.erase(it);
    Logger::info() << "Removed mesh " << meshId;
}

void MeshRenderer::uploadMesh(MeshData& meshData) {
    if (meshData.mesh.vertices.empty() || meshData.mesh.indices.empty()) {
        Logger::error() << "Cannot upload empty mesh";
        return;
    }

    m_renderer->uploadMeshBuffers(
        meshData.mesh.vertices.data(),
        static_cast<uint32_t>(meshData.mesh.vertices.size()),
        sizeof(MeshVertex),
        meshData.mesh.indices.data(),
        static_cast<uint32_t>(meshData.mesh.indices.size()),
        meshData.mesh.vertexBuffer,
        meshData.mesh.indexBuffer,
        meshData.mesh.vertexMemory,
        meshData.mesh.indexMemory
    );
}

// ========== Material Management ==========

uint32_t MeshRenderer::createMaterial(const PBRMaterial& material) {
    uint32_t materialId = m_nextMaterialId++;

    MaterialData matData;
    matData.material = material;

    // Upload material to GPU
    uploadMaterial(matData);

    m_materials[materialId] = std::move(matData);

    Logger::info() << "Created material " << materialId;
    return materialId;
}

void MeshRenderer::updateMaterial(uint32_t materialId, const PBRMaterial& material) {
    auto it = m_materials.find(materialId);
    if (it == m_materials.end()) {
        Logger::warning() << "Material " << materialId << " not found";
        return;
    }

    MaterialData& matData = it->second;
    matData.material = material;

    // Update GPU buffer
    if (matData.uniformMapped != nullptr) {
        MaterialUBO ubo(material);
        m_renderer->updateMaterialBuffer(matData.uniformMapped, &ubo);
    }
}

void MeshRenderer::setMeshMaterial(uint32_t meshId, uint32_t materialId) {
    auto meshIt = m_meshes.find(meshId);
    if (meshIt == m_meshes.end()) {
        Logger::warning() << "Mesh " << meshId << " not found";
        return;
    }

    auto matIt = m_materials.find(materialId);
    if (matIt == m_materials.end()) {
        Logger::warning() << "Material " << materialId << " not found";
        return;
    }

    meshIt->second.materialId = materialId;
}

void MeshRenderer::uploadMaterial(MaterialData& materialData) {
    MaterialUBO ubo(materialData.material);
    m_renderer->createMaterialBuffer(
        &ubo,
        materialData.uniformBuffer,
        materialData.uniformMemory,
        materialData.uniformMapped
    );
}

// ========== Instance Management ==========

uint32_t MeshRenderer::createInstance(uint32_t meshId, const glm::mat4& transform,
                                      const glm::vec4& tintColor) {
    auto meshIt = m_meshes.find(meshId);
    if (meshIt == m_meshes.end()) {
        Logger::error() << "Cannot create instance: mesh " << meshId << " not found";
        return 0;
    }

    uint32_t instanceId = m_nextInstanceId++;

    InstanceInfo info;
    info.meshId = meshId;
    info.data.transform = transform;
    info.data.tintColor = tintColor;

    m_instances[instanceId] = info;
    meshIt->second.instances.push_back(instanceId);
    meshIt->second.instanceBufferDirty = true;
    m_instanceCount++;

    return instanceId;
}

void MeshRenderer::updateInstanceTransform(uint32_t instanceId, const glm::mat4& transform) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        Logger::warning() << "Instance " << instanceId << " not found";
        return;
    }

    it->second.data.transform = transform;

    // Mark instance buffer as dirty
    auto meshIt = m_meshes.find(it->second.meshId);
    if (meshIt != m_meshes.end()) {
        meshIt->second.instanceBufferDirty = true;
    }
}

void MeshRenderer::updateInstanceColor(uint32_t instanceId, const glm::vec4& tintColor) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        Logger::warning() << "Instance " << instanceId << " not found";
        return;
    }

    it->second.data.tintColor = tintColor;

    // Mark instance buffer as dirty
    auto meshIt = m_meshes.find(it->second.meshId);
    if (meshIt != m_meshes.end()) {
        meshIt->second.instanceBufferDirty = true;
    }
}

void MeshRenderer::removeInstance(uint32_t instanceId) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        Logger::warning() << "Instance " << instanceId << " not found";
        return;
    }

    uint32_t meshId = it->second.meshId;
    m_instances.erase(it);
    m_instanceCount--;

    // Remove from mesh's instance list
    auto meshIt = m_meshes.find(meshId);
    if (meshIt != m_meshes.end()) {
        auto& instances = meshIt->second.instances;
        instances.erase(std::remove(instances.begin(), instances.end(), instanceId), instances.end());
        meshIt->second.instanceBufferDirty = true;
    }
}

void MeshRenderer::setInstanceVisible(uint32_t instanceId, bool visible) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        Logger::warning() << "Instance " << instanceId << " not found";
        return;
    }

    if (it->second.visible != visible) {
        it->second.visible = visible;

        // Mark instance buffer as dirty
        auto meshIt = m_meshes.find(it->second.meshId);
        if (meshIt != m_meshes.end()) {
            meshIt->second.instanceBufferDirty = true;
        }
    }
}

bool MeshRenderer::isInstanceVisible(uint32_t instanceId) const {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        return false;
    }
    return it->second.visible;
}

void MeshRenderer::updateInstanceBuffer(MeshData& meshData) {
    if (meshData.instances.empty()) {
        return;
    }

    // Collect instance data (only visible instances)
    std::vector<InstanceData> instanceDataArray;
    instanceDataArray.reserve(meshData.instances.size());

    for (uint32_t instanceId : meshData.instances) {
        auto it = m_instances.find(instanceId);
        if (it != m_instances.end() && it->second.visible) {
            instanceDataArray.push_back(it->second.data);
        }
    }

    if (instanceDataArray.empty()) {
        // No visible instances - queue buffer for deferred deletion
        if (meshData.instanceBuffer != VK_NULL_HANDLE) {
            queueBufferDeletion(meshData.instanceBuffer, meshData.instanceMemory);
            meshData.instanceBuffer = VK_NULL_HANDLE;
            meshData.instanceMemory = VK_NULL_HANDLE;
        }
        meshData.visibleInstanceCount = 0;
        meshData.instanceBufferDirty = false;
        return;
    }

    // Store visible count for rendering
    meshData.visibleInstanceCount = static_cast<uint32_t>(instanceDataArray.size());

    // Queue old buffer for deferred deletion (will be deleted after FRAMES_TO_KEEP frames)
    if (meshData.instanceBuffer != VK_NULL_HANDLE) {
        queueBufferDeletion(meshData.instanceBuffer, meshData.instanceMemory);
        meshData.instanceBuffer = VK_NULL_HANDLE;
        meshData.instanceMemory = VK_NULL_HANDLE;
    }

    // Create new instance buffer
    VkBuffer dummyIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dummyIndexMemory = VK_NULL_HANDLE;

    m_renderer->uploadMeshBuffers(
        instanceDataArray.data(),
        static_cast<uint32_t>(instanceDataArray.size()),
        sizeof(InstanceData),
        nullptr,  // No indices for instance buffer
        0,
        meshData.instanceBuffer,
        dummyIndexBuffer,  // Will be VK_NULL_HANDLE
        meshData.instanceMemory,
        dummyIndexMemory   // Will be VK_NULL_HANDLE
    );

    meshData.instanceBufferDirty = false;
}

// ========== Rendering ==========

void MeshRenderer::render(VkCommandBuffer cmd) {
    // Increment frame counter and process pending deletions
    m_frameNumber++;
    processPendingDeletions();

    if (m_meshes.empty()) {
        return;
    }

    // Safety check: ensure mesh pipeline is valid
    VkPipeline meshPipeline = m_renderer->getMeshPipeline();
    if (meshPipeline == VK_NULL_HANDLE) {
        return; // Mesh pipeline not initialized
    }

    // Bind mesh pipeline and its descriptor sets (including textures and bones)
    m_renderer->bindPipelineCached(cmd, meshPipeline);
    m_renderer->bindMeshDescriptorSets(cmd, m_textureDescriptorSet, m_boneDescriptorSet);

    // Render each mesh with its instances
    for (auto& [meshId, meshData] : m_meshes) {
        if (meshData.instances.empty() || !meshData.mesh.hasGPUBuffers()) {
            continue;
        }

        // Update instance buffer if needed
        if (meshData.instanceBufferDirty) {
            updateInstanceBuffer(meshData);
        }

        // Skip if no visible instances or no buffer
        if (meshData.instanceBuffer == VK_NULL_HANDLE || meshData.visibleInstanceCount == 0) {
            continue;
        }

        // Safety checks: ensure buffers are valid
        if (meshData.mesh.vertexBuffer == VK_NULL_HANDLE ||
            meshData.mesh.indexBuffer == VK_NULL_HANDLE) {
            continue;
        }

        // Get material and push constants
        auto matIt = m_materials.find(meshData.materialId);
        if (matIt == m_materials.end()) {
            matIt = m_materials.find(m_defaultMaterialId);
        }

        // Push material constants to shader
        if (matIt != m_materials.end()) {
            const auto& mat = matIt->second.material;
            m_renderer->pushMeshMaterialConstants(cmd,
                mat.albedoTexture,
                mat.normalTexture,
                mat.metallic,
                mat.roughness);
        } else {
            // Default material values (no textures)
            m_renderer->pushMeshMaterialConstants(cmd, -1, -1, 0.0f, 0.5f);
        }

        // Bind vertex buffer (binding 0)
        VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &meshData.mesh.vertexBuffer, &vertexOffset);

        // Bind instance buffer (binding 1)
        VkDeviceSize instanceOffset = 0;
        vkCmdBindVertexBuffers(cmd, 1, 1, &meshData.instanceBuffer, &instanceOffset);

        // Bind index buffer
        vkCmdBindIndexBuffer(cmd, meshData.mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Draw instanced (only visible instances)
        uint32_t instanceCount = meshData.visibleInstanceCount;
        uint32_t indexCount = static_cast<uint32_t>(meshData.mesh.indices.size());
        vkCmdDrawIndexed(cmd, indexCount, instanceCount, 0, 0, 0);
    }
}

size_t MeshRenderer::getGPUMemoryUsage() const {
    size_t total = 0;

    for (const auto& [id, meshData] : m_meshes) {
        total += meshData.mesh.getGPUMemoryUsage();
        if (meshData.instanceBuffer != VK_NULL_HANDLE) {
            total += meshData.instances.size() * sizeof(InstanceData);
        }
    }

    for (const auto& [id, matData] : m_materials) {
        total += sizeof(MaterialUBO);
    }

    // Add texture memory
    for (const auto& tex : m_textures) {
        total += tex.width * tex.height * 4;  // RGBA
    }

    return total;
}

const PBRMaterial* MeshRenderer::getMeshMaterial(uint32_t meshId) const {
    auto meshIt = m_meshes.find(meshId);
    if (meshIt == m_meshes.end()) {
        return nullptr;
    }

    auto matIt = m_materials.find(meshIt->second.materialId);
    if (matIt == m_materials.end()) {
        matIt = m_materials.find(m_defaultMaterialId);
    }

    if (matIt != m_materials.end()) {
        return &matIt->second.material;
    }
    return nullptr;
}

bool MeshRenderer::getMeshBounds(uint32_t meshId, glm::vec3& outMin, glm::vec3& outMax) const {
    auto meshIt = m_meshes.find(meshId);
    if (meshIt == m_meshes.end()) {
        return false;
    }

    outMin = meshIt->second.mesh.boundsMin;
    outMax = meshIt->second.mesh.boundsMax;
    return true;
}

// ========== GLTF/GLB Loading ==========

uint32_t MeshRenderer::loadMeshFromGLTF(const std::string& filepath) {
    try {
        std::vector<PBRMaterial> materials;
        std::vector<TextureImage> textureImages;
        std::vector<Mesh> meshes = MeshLoader::loadGLTF(filepath, materials, textureImages);

        if (meshes.empty()) {
            Logger::error() << "No meshes loaded from: " << filepath;
            return 0;
        }

        // Upload textures and get base texture index
        int32_t baseTextureIndex = static_cast<int32_t>(m_textures.size());
        for (const auto& texImage : textureImages) {
            int32_t texIdx = uploadTexture(texImage);
            if (texIdx < 0) {
                Logger::warning() << "Failed to upload texture: " << texImage.name;
            }
        }

        // Update material texture indices to be global
        for (auto& mat : materials) {
            if (mat.albedoTexture >= 0) {
                mat.albedoTexture += baseTextureIndex;
            }
            if (mat.normalTexture >= 0) {
                mat.normalTexture += baseTextureIndex;
            }
            if (mat.metallicRoughnessTexture >= 0) {
                mat.metallicRoughnessTexture += baseTextureIndex;
            }
            if (mat.emissiveTexture >= 0) {
                mat.emissiveTexture += baseTextureIndex;
            }
        }

        // Update texture descriptor set if we added textures
        if (!textureImages.empty()) {
            updateTextureDescriptorSet();
        }

        // For now, just load the first mesh
        Mesh& mesh = meshes[0];

        // Create material if provided
        uint32_t materialId = m_defaultMaterialId;
        if (!materials.empty()) {
            materialId = createMaterial(materials[0]);
        }

        mesh.materialIndex = materialId;
        return createMesh(mesh);

    } catch (const std::exception& e) {
        Logger::error() << "Failed to load GLTF mesh from " << filepath << ": " << e.what();
        return 0;
    }
}

// ========== Automatic Skinning from Rig ==========

bool MeshRenderer::applySkinningFromRig(uint32_t meshId, const std::string& rigPath,
                                         float influenceRadius) {
    Logger::info() << "applySkinningFromRig called: meshId=" << meshId << ", rigPath=" << rigPath;

    // Find the mesh
    auto meshIt = m_meshes.find(meshId);
    if (meshIt == m_meshes.end()) {
        Logger::error() << "Cannot apply skinning: mesh " << meshId << " not found";
        return false;
    }

    MeshData& meshData = meshIt->second;
    if (meshData.mesh.vertices.empty()) {
        Logger::error() << "Cannot apply skinning: mesh has no vertices";
        return false;
    }

    try {
        // Load rig YAML
        YAML::Node root = YAML::LoadFile(rigPath);

        // Parse skinning config if present
        float radius = influenceRadius;
        std::string falloffType = "smooth";
        int maxBonesPerVertex = 4;

        if (root["skinning"]) {
            const YAML::Node& skinning = root["skinning"];
            if (skinning["influence_radius"]) {
                radius = skinning["influence_radius"].as<float>(radius);
            }
            if (skinning["falloff"]) {
                falloffType = skinning["falloff"].as<std::string>("smooth");
            }
            if (skinning["max_bones_per_vertex"]) {
                maxBonesPerVertex = skinning["max_bones_per_vertex"].as<int>(4);
            }
        }

        // Load bones from rig
        struct RigBone {
            std::string name;
            glm::vec3 position;
            float influenceRadius;
        };
        std::vector<RigBone> bones;

        if (!root["bones"]) {
            Logger::error() << "Rig has no bones";
            return false;
        }

        for (const auto& boneNode : root["bones"]) {
            RigBone bone;
            bone.name = boneNode["name"].as<std::string>("bone");
            bone.influenceRadius = radius;

            // Per-bone radius override
            if (boneNode["influence_radius"]) {
                bone.influenceRadius = boneNode["influence_radius"].as<float>(radius);
            }

            // Position - support array [x,y,z] format
            if (boneNode["position"]) {
                const YAML::Node& posNode = boneNode["position"];
                if (posNode.IsSequence() && posNode.size() >= 3) {
                    bone.position.x = posNode[0].as<float>(0.0f);
                    bone.position.y = posNode[1].as<float>(0.0f);
                    bone.position.z = posNode[2].as<float>(0.0f);
                }
            }

            bones.push_back(bone);
        }

        if (bones.empty()) {
            Logger::error() << "No bones loaded from rig";
            return false;
        }

        // Calculate model bounds to auto-adjust radius if needed
        glm::vec3 modelMin(FLT_MAX), modelMax(-FLT_MAX);
        for (const auto& vertex : meshData.mesh.vertices) {
            modelMin = glm::min(modelMin, vertex.position);
            modelMax = glm::max(modelMax, vertex.position);
        }
        float modelSize = glm::length(modelMax - modelMin);

        // If radius is too small relative to model size, auto-adjust
        if (radius < modelSize * 0.3f) {
            float oldRadius = radius;
            radius = modelSize * 0.5f;  // Use 50% of model diagonal as default
            Logger::info() << "Auto-adjusted influence radius from " << oldRadius
                          << " to " << radius << " (model size: " << modelSize << ")";

            // Update all bones to use the new radius (unless they have per-bone override)
            for (auto& bone : bones) {
                if (bone.influenceRadius == oldRadius) {
                    bone.influenceRadius = radius;
                }
            }
        }

        Logger::info() << "Applying skinning from rig with " << bones.size()
                      << " bones, radius=" << radius;

        // Debug: track vertices with weights
        int verticesWithWeights = 0;

        // Calculate bone weights for each vertex
        for (auto& vertex : meshData.mesh.vertices) {
            // Collect bone influences
            struct BoneInfluence {
                int boneIndex;
                float weight;
            };
            std::vector<BoneInfluence> influences;

            for (size_t i = 0; i < bones.size(); ++i) {
                float dist = glm::distance(vertex.position, bones[i].position);
                float boneRadius = bones[i].influenceRadius;

                if (dist < boneRadius) {
                    float normalizedDist = dist / boneRadius;
                    float weight = 0.0f;

                    // Calculate weight based on falloff type
                    if (falloffType == "linear") {
                        weight = 1.0f - normalizedDist;
                    } else if (falloffType == "sharp") {
                        weight = 1.0f - normalizedDist * normalizedDist;
                    } else {  // smooth (default)
                        // Smooth hermite interpolation
                        weight = 1.0f - (3.0f * normalizedDist * normalizedDist -
                                        2.0f * normalizedDist * normalizedDist * normalizedDist);
                    }

                    if (weight > 0.001f) {
                        influences.push_back({static_cast<int>(i), weight});
                    }
                }
            }

            // Sort by weight (highest first)
            std::sort(influences.begin(), influences.end(),
                     [](const BoneInfluence& a, const BoneInfluence& b) {
                         return a.weight > b.weight;
                     });

            // Keep only top N bones
            if (influences.size() > static_cast<size_t>(maxBonesPerVertex)) {
                influences.resize(maxBonesPerVertex);
            }

            // Normalize weights to sum to 1.0
            float totalWeight = 0.0f;
            for (const auto& inf : influences) {
                totalWeight += inf.weight;
            }

            // Set vertex bone data
            vertex.boneIndices = glm::ivec4(0);
            vertex.boneWeights = glm::vec4(0.0f);

            for (size_t i = 0; i < influences.size() && i < 4; ++i) {
                vertex.boneIndices[static_cast<int>(i)] = influences[i].boneIndex;
                vertex.boneWeights[static_cast<int>(i)] = totalWeight > 0.001f
                    ? influences[i].weight / totalWeight : 0.0f;
            }

            // Track vertices that got weights
            if (!influences.empty()) {
                verticesWithWeights++;
            }
        }

        Logger::info() << "Skinning applied: " << verticesWithWeights << "/"
                      << meshData.mesh.vertices.size() << " vertices have bone weights";

        if (verticesWithWeights == 0) {
            Logger::warning() << "No vertices received bone weights! Check bone positions vs model vertices.";
            // Print first vertex position and first bone position for debugging
            if (!meshData.mesh.vertices.empty() && !bones.empty()) {
                const auto& v = meshData.mesh.vertices[0];
                Logger::info() << "  First vertex: (" << v.position.x << ", " << v.position.y << ", " << v.position.z << ")";
                Logger::info() << "  First bone: " << bones[0].name << " at ("
                              << bones[0].position.x << ", " << bones[0].position.y << ", " << bones[0].position.z << ")";
            }
        }

        // Re-upload mesh to GPU with new bone weights
        if (meshData.mesh.hasGPUBuffers()) {
            // Destroy old buffers
            m_renderer->destroyMeshBuffers(meshData.mesh.vertexBuffer, meshData.mesh.indexBuffer,
                                          meshData.mesh.vertexMemory, meshData.mesh.indexMemory);
            meshData.mesh.vertexBuffer = VK_NULL_HANDLE;
            meshData.mesh.indexBuffer = VK_NULL_HANDLE;
            meshData.mesh.vertexMemory = VK_NULL_HANDLE;
            meshData.mesh.indexMemory = VK_NULL_HANDLE;
        }
        uploadMesh(meshData);

        Logger::info() << "Applied automatic skinning to mesh " << meshId
                      << " (" << meshData.mesh.vertices.size() << " vertices)";
        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to apply skinning from " << rigPath << ": " << e.what();
        return false;
    }
}

// ========== Texture Management ==========

void MeshRenderer::initializeTextureResources() {
    VkDevice device = m_renderer->getDevice();

    // Create texture sampler (linear filtering for smooth textures)
    m_textureSampler = m_renderer->createLinearTextureSampler();

    // Use the renderer's mesh texture descriptor set layout (created in createMeshPipeline)
    m_textureDescriptorSetLayout = m_renderer->getMeshTextureDescriptorSetLayout();

    if (m_textureDescriptorSetLayout == VK_NULL_HANDLE) {
        Logger::error() << "Mesh texture descriptor set layout not available";
        return;
    }

    // Create descriptor pool for texture descriptor sets
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_TEXTURES;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_textureDescriptorPool) != VK_SUCCESS) {
        Logger::error() << "Failed to create mesh texture descriptor pool";
        return;
    }

    // Allocate descriptor set using the renderer's layout
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_textureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_textureDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_textureDescriptorSet) != VK_SUCCESS) {
        Logger::error() << "Failed to allocate mesh texture descriptor set";
        return;
    }

    // Create a default 1x1 white texture so descriptor set is always valid
    TextureImage defaultTex;
    defaultTex.name = "default_white";
    defaultTex.width = 1;
    defaultTex.height = 1;
    defaultTex.data = {255, 255, 255, 255};  // White pixel RGBA
    uploadTexture(defaultTex);

    // Initialize descriptor set with default texture
    updateTextureDescriptorSet();

    Logger::info() << "Mesh texture resources initialized (max " << MAX_TEXTURES << " textures)";
}

int32_t MeshRenderer::uploadTexture(const TextureImage& texImage) {
    if (!texImage.isValid()) {
        Logger::warning() << "Invalid texture data: " << texImage.name;
        return -1;
    }

    if (m_textures.size() >= MAX_TEXTURES) {
        Logger::warning() << "Maximum texture count reached (" << MAX_TEXTURES << ")";
        return -1;
    }

    TextureData texData;
    texData.name = texImage.name;
    texData.width = texImage.width;
    texData.height = texImage.height;

    // Upload to GPU
    m_renderer->uploadMeshTexture(texImage.data.data(), texImage.width, texImage.height,
                                   texData.image, texData.memory);

    // Create image view
    texData.view = m_renderer->createImageView(texData.image, VK_FORMAT_R8G8B8A8_SRGB,
                                                VK_IMAGE_ASPECT_COLOR_BIT);

    int32_t index = static_cast<int32_t>(m_textures.size());
    m_textures.push_back(std::move(texData));

    Logger::info() << "Uploaded texture " << index << ": " << texImage.name
                  << " (" << texImage.width << "x" << texImage.height << ")";

    return index;
}

void MeshRenderer::updateTextureDescriptorSet() {
    if (m_textureDescriptorSet == VK_NULL_HANDLE || m_textures.empty()) {
        return;
    }

    VkDevice device = m_renderer->getDevice();

    // Create array of image infos for all textures
    std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);

    // Fill with actual textures
    for (size_t i = 0; i < m_textures.size(); ++i) {
        imageInfos[i].sampler = m_textureSampler;
        imageInfos[i].imageView = m_textures[i].view;
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // Fill remaining slots with first texture (or a default if we had one)
    VkImageView defaultView = m_textures.empty() ? VK_NULL_HANDLE : m_textures[0].view;
    for (size_t i = m_textures.size(); i < MAX_TEXTURES; ++i) {
        imageInfos[i].sampler = m_textureSampler;
        imageInfos[i].imageView = defaultView;
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_textureDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = MAX_TEXTURES;
    descriptorWrite.pImageInfo = imageInfos.data();

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    Logger::info() << "Updated texture descriptor set with " << m_textures.size() << " textures";
}

void MeshRenderer::cleanupTextures() {
    VkDevice device = m_renderer->getDevice();

    // Destroy all textures
    for (auto& tex : m_textures) {
        m_renderer->destroyMeshTexture(tex.image, tex.view, tex.memory);
    }
    m_textures.clear();

    // Destroy descriptor pool (this also frees the descriptor set)
    if (m_textureDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_textureDescriptorPool, nullptr);
        m_textureDescriptorPool = VK_NULL_HANDLE;
        m_textureDescriptorSet = VK_NULL_HANDLE;
    }

    // Note: m_textureDescriptorSetLayout is owned by VulkanRenderer, don't destroy it here
    m_textureDescriptorSetLayout = VK_NULL_HANDLE;

    // Destroy sampler
    if (m_textureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_textureSampler, nullptr);
        m_textureSampler = VK_NULL_HANDLE;
    }

    Logger::info() << "Cleaned up mesh textures";
}

// ========== Skeletal Animation (Bone Buffer) ==========

void MeshRenderer::initializeBoneBuffer() {
    if (m_boneBufferInitialized) {
        return;
    }

    VkDevice device = m_renderer->getDevice();

    // Create bone uniform buffer
    VkDeviceSize bufferSize = sizeof(BoneUBO);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_boneBuffer) != VK_SUCCESS) {
        Logger::error() << "Failed to create bone uniform buffer";
        return;
    }

    // Get memory requirements and allocate
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, m_boneBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_renderer->findMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_boneMemory) != VK_SUCCESS) {
        Logger::error() << "Failed to allocate bone buffer memory";
        vkDestroyBuffer(device, m_boneBuffer, nullptr);
        m_boneBuffer = VK_NULL_HANDLE;
        return;
    }

    vkBindBufferMemory(device, m_boneBuffer, m_boneMemory, 0);

    // Persistently map the buffer
    vkMapMemory(device, m_boneMemory, 0, bufferSize, 0, &m_boneMapped);

    // Initialize to identity matrices (no skinning)
    BoneUBO initialData;
    initialData.numBones = 0;
    for (int i = 0; i < MAX_BONES; ++i) {
        initialData.boneMatrices[i] = glm::mat4(1.0f);
    }
    memcpy(m_boneMapped, &initialData, sizeof(BoneUBO));

    // Create descriptor pool for bone descriptor set
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_boneDescriptorPool) != VK_SUCCESS) {
        Logger::error() << "Failed to create bone descriptor pool";
        cleanupBoneBuffer();
        return;
    }

    // Get the bone descriptor set layout from renderer
    VkDescriptorSetLayout boneLayout = m_renderer->getMeshBoneDescriptorSetLayout();
    if (boneLayout == VK_NULL_HANDLE) {
        Logger::error() << "Bone descriptor set layout not available";
        cleanupBoneBuffer();
        return;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo descAllocInfo{};
    descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAllocInfo.descriptorPool = m_boneDescriptorPool;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &boneLayout;

    if (vkAllocateDescriptorSets(device, &descAllocInfo, &m_boneDescriptorSet) != VK_SUCCESS) {
        Logger::error() << "Failed to allocate bone descriptor set";
        cleanupBoneBuffer();
        return;
    }

    // Update descriptor set with bone buffer
    VkDescriptorBufferInfo boneBufferInfo{};
    boneBufferInfo.buffer = m_boneBuffer;
    boneBufferInfo.offset = 0;
    boneBufferInfo.range = sizeof(BoneUBO);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_boneDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &boneBufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    m_boneBufferInitialized = true;
    Logger::info() << "Bone buffer initialized (max " << MAX_BONES << " bones)";
}

void MeshRenderer::cleanupBoneBuffer() {
    VkDevice device = m_renderer->getDevice();

    if (m_boneMapped != nullptr) {
        vkUnmapMemory(device, m_boneMemory);
        m_boneMapped = nullptr;
    }

    if (m_boneBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_boneBuffer, nullptr);
        m_boneBuffer = VK_NULL_HANDLE;
    }

    if (m_boneMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_boneMemory, nullptr);
        m_boneMemory = VK_NULL_HANDLE;
    }

    // Destroy descriptor pool (also frees descriptor set)
    if (m_boneDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_boneDescriptorPool, nullptr);
        m_boneDescriptorPool = VK_NULL_HANDLE;
    }
    m_boneDescriptorSet = VK_NULL_HANDLE;
    m_boneBufferInitialized = false;
}

void MeshRenderer::updateBoneMatrices(const glm::mat4* matrices, int count) {
    if (!m_boneBufferInitialized) {
        initializeBoneBuffer();
    }

    if (m_boneMapped == nullptr || count <= 0) {
        return;
    }

    // Clamp count to max bones
    int actualCount = std::min(count, MAX_BONES);

    // Update the mapped buffer
    BoneUBO* boneData = static_cast<BoneUBO*>(m_boneMapped);
    boneData->numBones = actualCount;

    for (int i = 0; i < actualCount; ++i) {
        boneData->boneMatrices[i] = matrices[i];
    }

    // Fill remaining slots with identity (shouldn't be accessed, but just in case)
    for (int i = actualCount; i < MAX_BONES; ++i) {
        boneData->boneMatrices[i] = glm::mat4(1.0f);
    }
}

void MeshRenderer::clearBoneMatrices() {
    if (!m_boneBufferInitialized || m_boneMapped == nullptr) {
        return;
    }

    // Set numBones to 0 to disable skinning
    BoneUBO* boneData = static_cast<BoneUBO*>(m_boneMapped);
    boneData->numBones = 0;
}
