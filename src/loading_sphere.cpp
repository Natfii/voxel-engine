/**
 * @file loading_sphere.cpp
 * @brief Implementation of the spinning loading sphere
 *
 * Created: 2025-11-25
 */

#include "loading_sphere.h"
#include "vulkan_renderer.h"
#include "map_preview.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LoadingSphere::LoadingSphere() {
    m_startTime = std::chrono::steady_clock::now();
}

LoadingSphere::~LoadingSphere() {
    cleanup();
}

bool LoadingSphere::initialize(VulkanRenderer* renderer) {
    if (!renderer) {
        std::cerr << "LoadingSphere::initialize: null renderer" << '\n';
        return false;
    }

    m_renderer = renderer;

    // Generate sphere mesh
    generateSphereMesh();

    // Create GPU buffers
    if (!createBuffers()) {
        std::cerr << "LoadingSphere: Failed to create GPU buffers" << '\n';
        return false;
    }

    m_initialized = true;
    m_startTime = std::chrono::steady_clock::now();

    std::cout << "[LoadingSphere] Initialized with " << m_vertices.size()
              << " vertices, " << m_indices.size() << " indices" << '\n';

    return true;
}

void LoadingSphere::cleanup() {
    if (!m_renderer) return;

    VkDevice device = m_renderer->getDevice();

    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexMemory, nullptr);
        m_vertexMemory = VK_NULL_HANDLE;
    }
    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }
    if (m_indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_indexMemory, nullptr);
        m_indexMemory = VK_NULL_HANDLE;
    }

    // Note: descriptor set is freed when pool is destroyed, no need to free explicitly
    m_descriptorSet = VK_NULL_HANDLE;
    m_initialized = false;
}

void LoadingSphere::setMapPreview(MapPreview* mapPreview) {
    m_mapPreview = mapPreview;
    m_hasMapTexture = false;  // Will be set to true when we create the descriptor set
    m_descriptorSet = VK_NULL_HANDLE;  // Reset - will be recreated on next render
    m_triedCreatingDescriptor = false;  // Reset so we can try creating descriptor for new map preview
}

void LoadingSphere::generateSphereMesh() {
    m_vertices.clear();
    m_indices.clear();

    const int latSegments = SPHERE_SEGMENTS;
    const int lonSegments = SPHERE_SEGMENTS;

    // Generate vertices with equirectangular UV mapping for map preview texture
    for (int lat = 0; lat <= latSegments; ++lat) {
        float theta = static_cast<float>(lat) * static_cast<float>(M_PI) / latSegments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= lonSegments; ++lon) {
            float phi = static_cast<float>(lon) * 2.0f * static_cast<float>(M_PI) / lonSegments;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            // Position on unit sphere
            float x = cosPhi * sinTheta;
            float y = cosTheta;
            float z = sinPhi * sinTheta;

            Vertex vertex;
            vertex.x = x * SPHERE_RADIUS;
            vertex.y = y * SPHERE_RADIUS;
            vertex.z = z * SPHERE_RADIUS;

            // Equirectangular UV mapping - full 0-1 range wraps texture around sphere
            // U goes around (longitude), V goes top to bottom (latitude)
            vertex.u = static_cast<float>(lon) / lonSegments;
            vertex.v = static_cast<float>(lat) / latSegments;

            // Bright white vertex color - texture shows through at full brightness
            vertex.r = 1.5f;
            vertex.g = 1.5f;
            vertex.b = 1.5f;
            vertex.a = 1.0f;

            // Full lighting for maximum brightness
            vertex.skyLight = 1.0f;
            vertex.blockLight = 1.0f;
            vertex.ao = 1.0f;

            m_vertices.push_back(vertex);
        }
    }

    // Generate indices
    for (int lat = 0; lat < latSegments; ++lat) {
        for (int lon = 0; lon < lonSegments; ++lon) {
            int first = lat * (lonSegments + 1) + lon;
            int second = first + lonSegments + 1;

            // Two triangles per quad
            m_indices.push_back(first);
            m_indices.push_back(second);
            m_indices.push_back(first + 1);

            m_indices.push_back(second);
            m_indices.push_back(second + 1);
            m_indices.push_back(first + 1);
        }
    }
}

bool LoadingSphere::createBuffers() {
    VkDevice device = m_renderer->getDevice();

    // Create vertex buffer
    VkDeviceSize vertexSize = sizeof(Vertex) * m_vertices.size();
    m_renderer->createBuffer(
        vertexSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vertexBuffer,
        m_vertexMemory
    );

    // Create index buffer
    VkDeviceSize indexSize = sizeof(uint32_t) * m_indices.size();
    m_renderer->createBuffer(
        indexSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_indexBuffer,
        m_indexMemory
    );

    // Upload vertex data via staging buffer
    VkBuffer vertexStagingBuffer;
    VkDeviceMemory vertexStagingMemory;
    m_renderer->createBuffer(
        vertexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertexStagingBuffer,
        vertexStagingMemory
    );

    void* data;
    vkMapMemory(device, vertexStagingMemory, 0, vertexSize, 0, &data);
    memcpy(data, m_vertices.data(), vertexSize);
    vkUnmapMemory(device, vertexStagingMemory);

    m_renderer->copyBuffer(vertexStagingBuffer, m_vertexBuffer, vertexSize);

    vkDestroyBuffer(device, vertexStagingBuffer, nullptr);
    vkFreeMemory(device, vertexStagingMemory, nullptr);

    // Upload index data via staging buffer
    VkBuffer indexStagingBuffer;
    VkDeviceMemory indexStagingMemory;
    m_renderer->createBuffer(
        indexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indexStagingBuffer,
        indexStagingMemory
    );

    vkMapMemory(device, indexStagingMemory, 0, indexSize, 0, &data);
    memcpy(data, m_indices.data(), indexSize);
    vkUnmapMemory(device, indexStagingMemory);

    m_renderer->copyBuffer(indexStagingBuffer, m_indexBuffer, indexSize);

    vkDestroyBuffer(device, indexStagingBuffer, nullptr);
    vkFreeMemory(device, indexStagingMemory, nullptr);

    return true;
}

void LoadingSphere::render() {
    if (!m_initialized) return;

    // Calculate rotation based on elapsed time
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - m_startTime).count();
    float rotationAngle = glm::radians(elapsed * m_rotationSpeed);

    // Get current swapchain extent for aspect ratio
    VkExtent2D extent = m_renderer->getSwapChainExtent();
    float aspect = static_cast<float>(extent.width) / extent.height;

    // Create MVP matrices for the sphere
    // Model: rotate around Y axis
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    // Also tilt slightly for better view
    model = glm::rotate(model, glm::radians(-15.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    // View: camera looking at origin from a distance
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 2.0f),  // Camera position
        glm::vec3(0.0f, 0.0f, 0.0f),  // Look at origin
        glm::vec3(0.0f, 1.0f, 0.0f)   // Up vector
    );

    // Projection: perspective for 3D effect
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),  // FOV
        aspect,               // Aspect ratio
        0.1f,                 // Near plane
        10.0f                 // Far plane
    );

    // Vulkan Y coordinate flip
    projection[1][1] *= -1;

    // Update uniform buffer with our matrices
    m_renderer->updateUniformBuffer(
        m_renderer->getCurrentFrame(),
        model,
        view,
        projection,
        glm::vec3(0.0f, 0.0f, 2.0f),  // Camera position
        100.0f,                        // Render distance (not used for sphere)
        false                          // Not underwater
    );

    // Get command buffer and bind pipeline
    VkCommandBuffer commandBuffer = m_renderer->getCurrentCommandBuffer();

    // Bind the sphere pipeline (uses old 48-byte Vertex format)
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_renderer->getSpherePipeline());

    // Try to use map preview texture if available
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    if (m_mapPreview && m_mapPreview->isReady() && m_hasMapTexture) {
        descriptorSet = m_descriptorSet;
    } else if (m_mapPreview && m_mapPreview->isReady() && !m_triedCreatingDescriptor) {
        // Create descriptor set for map preview texture (only try once per map preview)
        m_triedCreatingDescriptor = true;
        m_descriptorSet = m_renderer->createCustomTextureDescriptorSet(
            m_mapPreview->getImageView(),
            m_mapPreview->getSampler()
        );
        if (m_descriptorSet != VK_NULL_HANDLE) {
            m_hasMapTexture = true;
            descriptorSet = m_descriptorSet;
            std::cout << "[LoadingSphere] Using map preview texture" << '\n';
        } else {
            std::cout << "[LoadingSphere] Failed to create descriptor, using fallback" << '\n';
        }
    }

    // Fall back to default descriptor set if map preview not available
    if (descriptorSet == VK_NULL_HANDLE) {
        descriptorSet = m_renderer->getCurrentDescriptorSet();
    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_renderer->getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);

    // Bind vertex buffer
    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // Bind index buffer
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Draw the sphere
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
}
