#pragma once

#include <vector>
#include <array>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "voxelmath.h"
#include "FastNoiseLite.h"

// Forward declaration
class VulkanRenderer;

struct Vertex {
    float x, y, z;
    float r, g, b;

    // Vulkan vertex input descriptions
    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions();
};

class Chunk {

 static FastNoiseLite* s_noise;

public:
    static void initNoise(int seed);
    static void cleanupNoise();
    static const int WIDTH = 32;
    static const int HEIGHT = 32;
    static const int DEPTH = 32;

    Chunk(int x, int y, int z);
    ~Chunk();

    void generate();
    void createVertexBuffer(VulkanRenderer* renderer);
    void render(VkCommandBuffer commandBuffer);

    // Get terrain height at world coordinates (returns height in blocks)
    static int getTerrainHeightAt(float worldX, float worldZ);

    // Get chunk world bounds for culling
    glm::vec3 getMin() const { return m_minBounds; }
    glm::vec3 getMax() const { return m_maxBounds; }
    glm::vec3 getCenter() const { return (m_minBounds + m_maxBounds) * 0.5f; }

    // Get vertex count (useful for skipping empty chunks)
    uint32_t getVertexCount() const { return m_vertexCount; }

    // Get block ID at local chunk coordinates (returns -1 if out of bounds)
    int getBlock(int x, int y, int z) const;

    // Get chunk coordinates
    int getChunkX() const { return m_x; }
    int getChunkY() const { return m_y; }
    int getChunkZ() const { return m_z; }

private:
    int m_x, m_y, m_z;
    int m_blocks[WIDTH][HEIGHT][DEPTH];
    std::vector<Vertex> m_vertices;
    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;
    uint32_t m_vertexCount;

    // Chunk bounds in world space
    glm::vec3 m_minBounds;
    glm::vec3 m_maxBounds;
};