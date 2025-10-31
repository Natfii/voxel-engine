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
    float x, y, z;      // Position
    float r, g, b;      // Color (fallback if no texture)
    float u, v;         // Texture coordinates

    // Vulkan vertex input descriptions
    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
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
    void generateMesh(class World* world);  // Generate mesh after all chunks exist
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

    // Get/Set block ID at local chunk coordinates (returns -1 if out of bounds for get)
    int getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int blockID);  // Set block and mark for remeshing

    // Get chunk coordinates
    int getChunkX() const { return m_x; }
    int getChunkY() const { return m_y; }
    int getChunkZ() const { return m_z; }

    // Visibility state for hysteresis-based culling
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

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

    // Visibility state for hysteresis-based culling
    bool m_visible;
};