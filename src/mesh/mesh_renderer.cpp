/**
 * @file mesh_renderer.cpp
 * @brief High-level mesh rendering system implementation
 */

#include "mesh/mesh_renderer.h"
#include "vulkan_renderer.h"
#include "logger.h"
#include <glm/gtc/matrix_transform.hpp>

// ========== Construction/Destruction ==========

MeshRenderer::MeshRenderer(VulkanRenderer* renderer)
    : m_renderer(renderer) {

    // Create default material
    PBRMaterial defaultMat = PBRMaterial::createDefault();
    m_defaultMaterialId = createMaterial(defaultMat);

    Logger::info() << "MeshRenderer initialized with default material";
}

MeshRenderer::~MeshRenderer() {
    Logger::info() << "Cleaning up MeshRenderer...";

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

    Logger::info() << "MeshRenderer cleanup complete";
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
        // No visible instances - clean up any existing buffer
        if (meshData.instanceBuffer != VK_NULL_HANDLE) {
            m_renderer->destroyMeshBuffers(meshData.instanceBuffer, VK_NULL_HANDLE,
                                          meshData.instanceMemory, VK_NULL_HANDLE);
            meshData.instanceBuffer = VK_NULL_HANDLE;
            meshData.instanceMemory = VK_NULL_HANDLE;
        }
        meshData.visibleInstanceCount = 0;
        meshData.instanceBufferDirty = false;
        return;
    }

    // Store visible count for rendering
    meshData.visibleInstanceCount = static_cast<uint32_t>(instanceDataArray.size());

    // Destroy old instance buffer if it exists
    if (meshData.instanceBuffer != VK_NULL_HANDLE) {
        m_renderer->destroyMeshBuffers(meshData.instanceBuffer, VK_NULL_HANDLE,
                                      meshData.instanceMemory, VK_NULL_HANDLE);
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
    if (m_meshes.empty()) {
        return;
    }

    // Bind mesh pipeline
    m_renderer->bindPipelineCached(cmd, m_renderer->getMeshPipeline());

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

        // Get material
        auto matIt = m_materials.find(meshData.materialId);
        if (matIt == m_materials.end()) {
            matIt = m_materials.find(m_defaultMaterialId);
        }

        // Bind material (TODO: descriptor set binding when we add descriptor management)
        // For now, we'll need to create descriptor sets in a future update

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

    return total;
}
