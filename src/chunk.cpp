/**
 * @file chunk.cpp
 * @brief Implementation of chunk terrain generation and mesh optimization
 *
 * This file contains the core algorithms for:
 * - Procedural terrain generation using FastNoiseLite
 * - Optimized mesh generation with face culling
 * - Vulkan buffer management
 *
 * Created by original author, enhanced documentation by Claude (Anthropic AI Assistant)
 */

#include "chunk.h"
#include "world.h"
#include "vulkan_renderer.h"
#include "block_system.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// Static member initialization
FastNoiseLite* Chunk::s_noise = nullptr;

void Chunk::initNoise(int seed) {
    if (s_noise == nullptr) {
        s_noise = new FastNoiseLite(seed);
        s_noise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        s_noise->SetFractalType(FastNoiseLite::FractalType_FBm);
        s_noise->SetFractalOctaves(4);
        s_noise->SetFractalLacunarity(2.0f);
        s_noise->SetFractalGain(0.5f);
        s_noise->SetFrequency(0.015f);  // Lower frequency for larger, smoother hills
    }
}

void Chunk::cleanupNoise() {
    delete s_noise;
    s_noise = nullptr;
}

// Note: Vertex::getBindingDescription() and Vertex::getAttributeDescriptions()
// are now defined as inline functions in chunk.h

Chunk::Chunk(int x, int y, int z)
    : m_x(x), m_y(y), m_z(z),
      m_vertexBuffer(VK_NULL_HANDLE),
      m_vertexBufferMemory(VK_NULL_HANDLE),
      m_indexBuffer(VK_NULL_HANDLE),
      m_indexBufferMemory(VK_NULL_HANDLE),
      m_vertexCount(0),
      m_indexCount(0),
      m_visible(false) {

    // Initialize all blocks to air
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                m_blocks[i][j][k] = 0;
            }
        }
    }

    // Calculate world-space bounds for culling
    // Blocks are 0.5 world units in size
    float worldX = m_x * WIDTH * 0.5f;
    float worldY = m_y * HEIGHT * 0.5f;
    float worldZ = m_z * DEPTH * 0.5f;

    m_minBounds = glm::vec3(worldX, worldY, worldZ);
    m_maxBounds = glm::vec3(
        worldX + WIDTH * 0.5f,
        worldY + HEIGHT * 0.5f,
        worldZ + DEPTH * 0.5f
    );
}

Chunk::~Chunk() {
    // Note: Vulkan buffers must be destroyed via destroyBuffers() before destruction
}

/**
 * @brief Gets terrain height using noise function
 *
 * Terrain Generation Algorithm:
 * 1. Sample 2D Perlin noise at (worldX, worldZ)
 * 2. Noise returns value in range [-1, 1]
 * 3. Multiply by amplitude (12.0) to get variation
 * 4. Add to base height (64) to center terrain at Y=64
 * 5. This creates rolling hills with height range [52, 76]
 *
 * The base height of 64 centers the terrain in the middle of the
 * second layer of chunks (Y chunk index 2), which is visible from
 * the default spawn point.
 *
 * @param worldX World X coordinate
 * @param worldZ World Z coordinate
 * @return Height in blocks (Y coordinate)
 */
int Chunk::getTerrainHeightAt(float worldX, float worldZ) {
    if (s_noise == nullptr) {
        return 64; // Fallback to flat terrain if noise not initialized
    }

    // Sample noise and map to height range [52, 76]
    float noise = s_noise->GetNoise(worldX, worldZ);
    int height = 64 + (int)(noise * 12.0f);  // Base 64 ± 12 blocks variation

    return height;
}

/**
 * @brief Generates terrain blocks for this chunk
 *
 * Procedural Generation Algorithm:
 * 1. For each (x, z) column in the chunk:
 *    - Convert local coords to world coords
 *    - Query terrain height from noise function
 *    - Fill blocks from Y=0 up to terrain height
 * 2. Block selection:
 *    - Top block: Grass (ID 2)
 *    - Next 3 blocks: Dirt (ID 3)
 *    - Below that: Stone (ID 1)
 *
 * Performance: ~32,000 blocks processed per chunk (32³)
 */
void Chunk::generate() {
    for (int x = 0; x < WIDTH; x++) {
        for (int z = 0; z < DEPTH; z++) {
            // Convert local coords to world coords
            float worldX = (m_x * WIDTH + x) * 0.5f;
            float worldZ = (m_z * DEPTH + z) * 0.5f;

            // Get terrain height for this column
            int terrainHeight = getTerrainHeightAt(worldX, worldZ);

            // Fill column from bottom to terrain height
            for (int y = 0; y < HEIGHT; y++) {
                int worldY = m_y * HEIGHT + y;

                if (worldY < terrainHeight) {
                    // Determine block type based on depth
                    int depthFromSurface = terrainHeight - worldY;

                    if (depthFromSurface == 1) {
                        m_blocks[x][y][z] = 2;  // Grass on top
                    } else if (depthFromSurface <= 4) {
                        m_blocks[x][y][z] = 3;  // Dirt below grass
                    } else {
                        m_blocks[x][y][z] = 1;  // Stone at bottom
                    }
                } else {
                    m_blocks[x][y][z] = 0;  // Air above terrain
                }
            }
        }
    }
}

/**
 * @brief Generates optimized mesh with face culling
 *
 * Mesh Generation Algorithm:
 * ============================
 *
 * This is the core optimization that makes voxel rendering efficient.
 * Without culling, a 32x32x32 chunk would have:
 * - 32,768 blocks
 * - 196,608 faces (6 per block)
 * - 1,179,648 vertices (6 per face)
 *
 * With face culling, typical terrain chunks have:
 * - Only 2,000-5,000 visible faces
 * - 12,000-30,000 vertices
 * - ~95% reduction in vertex count!
 *
 * Algorithm Steps:
 * ----------------
 * 1. For each non-air block in the chunk:
 *
 * 2. For each of the 6 faces (+X, -X, +Y, -Y, +Z, -Z):
 *    a. Calculate neighbor position in that direction
 *    b. Query neighbor block (may be in adjacent chunk)
 *    c. If neighbor is air (not solid), face is visible:
 *       - Generate 4 vertices for the quad
 *       - Assign texture coordinates from texture atlas
 *       - Add 6 indices (2 triangles) for the face
 *    d. If neighbor is solid, face is hidden - skip it
 *
 * 3. Result: Only faces exposed to air are generated
 *
 * Coordinate Systems:
 * -------------------
 * - Local coords: (0-31, 0-31, 0-31) within chunk
 * - World coords: local * 0.5 (blocks are 0.5 world units)
 * - Texture coords: UV mapped from texture atlas
 *
 * Texture Atlas Mapping:
 * ----------------------
 * - All block textures packed in NxN grid
 * - Each texture 64x64 pixels
 * - UV coords: (atlasX / gridSize, atlasY / gridSize)
 * - Supports per-face textures (cube mapping)
 *
 * @param world World instance to query neighboring chunks
 */
void Chunk::generateMesh(class World* world) {
    m_vertices.clear();
    m_indices.clear();

    const float BLOCK_SIZE = 0.5f;  // Blocks are 0.5 world units
    const BlockRegistry& blockRegistry = BlockRegistry::instance();
    const int atlasGridSize = blockRegistry.getAtlasGridSize();
    const float uvStep = (atlasGridSize > 0) ? (1.0f / atlasGridSize) : 1.0f;

    // Iterate through all blocks in this chunk
    for (int x = 0; x < WIDTH; x++) {
        for (int y = 0; y < HEIGHT; y++) {
            for (int z = 0; z < DEPTH; z++) {
                int blockID = m_blocks[x][y][z];

                // Skip air blocks (no geometry needed)
                if (blockID == 0) continue;

                // Get block definition for texture coordinates
                const BlockDefinition* blockDef = nullptr;
                try {
                    blockDef = &blockRegistry.get(blockID);
                } catch (...) {
                    continue;  // Skip invalid block IDs
                }

                // Calculate world position
                float worldX = (m_x * WIDTH + x) * BLOCK_SIZE;
                float worldY = (m_y * HEIGHT + y) * BLOCK_SIZE;
                float worldZ = (m_z * DEPTH + z) * BLOCK_SIZE;

                // Face generation: Check all 6 faces for visibility
                // Each face is only generated if the neighbor in that direction is air

                // Helper lambda to check if a face should be rendered
                auto shouldRenderFace = [&](int dx, int dy, int dz) -> bool {
                    int nx = x + dx;
                    int ny = y + dy;
                    int nz = z + dz;

                    // Check if neighbor is within this chunk
                    if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT && nz >= 0 && nz < DEPTH) {
                        // Neighbor in same chunk
                        return m_blocks[nx][ny][nz] == 0;  // Visible if neighbor is air
                    } else {
                        // Neighbor in adjacent chunk - query world
                        float neighborWorldX = worldX + dx * BLOCK_SIZE;
                        float neighborWorldY = worldY + dy * BLOCK_SIZE;
                        float neighborWorldZ = worldZ + dz * BLOCK_SIZE;
                        int neighborBlockID = world->getBlockAt(neighborWorldX, neighborWorldY, neighborWorldZ);
                        return neighborBlockID == 0;  // Visible if neighbor is air
                    }
                };

                // Get texture coordinates for each face
                auto getFaceTexture = [&](int faceIndex) -> BlockDefinition::FaceTexture {
                    if (!blockDef->useCubeMap) {
                        return blockDef->all;  // Use same texture for all faces
                    }

                    // Cube-mapped textures (different per face)
                    switch (faceIndex) {
                        case 0: return blockDef->right;   // +X
                        case 1: return blockDef->left;    // -X
                        case 2: return blockDef->top;     // +Y
                        case 3: return blockDef->bottom;  // -Y
                        case 4: return blockDef->front;   // +Z
                        case 5: return blockDef->back;    // -Z
                        default: return blockDef->all;
                    }
                };

                // Generate each face if visible
                // Face index: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z

                // +X face (right)
                if (shouldRenderFace(1, 0, 0)) {
                    auto tex = getFaceTexture(0);
                    float u0 = tex.atlasX * uvStep;
                    float v0 = tex.atlasY * uvStep;
                    float u1 = u0 + uvStep;
                    float v1 = v0 + uvStep;

                    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY, worldZ, 1,1,1, u0, v1});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY + BLOCK_SIZE, worldZ, 1,1,1, u0, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY + BLOCK_SIZE, worldZ + BLOCK_SIZE, 1,1,1, u1, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY, worldZ + BLOCK_SIZE, 1,1,1, u1, v1});

                    // Two triangles forming the quad
                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 1);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 3);
                }

                // -X face (left)
                if (shouldRenderFace(-1, 0, 0)) {
                    auto tex = getFaceTexture(1);
                    float u0 = tex.atlasX * uvStep;
                    float v0 = tex.atlasY * uvStep;
                    float u1 = u0 + uvStep;
                    float v1 = v0 + uvStep;

                    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());
                    m_vertices.push_back({worldX, worldY, worldZ + BLOCK_SIZE, 1,1,1, u0, v1});
                    m_vertices.push_back({worldX, worldY + BLOCK_SIZE, worldZ + BLOCK_SIZE, 1,1,1, u0, v0});
                    m_vertices.push_back({worldX, worldY + BLOCK_SIZE, worldZ, 1,1,1, u1, v0});
                    m_vertices.push_back({worldX, worldY, worldZ, 1,1,1, u1, v1});

                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 1);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 3);
                }

                // +Y face (top)
                if (shouldRenderFace(0, 1, 0)) {
                    auto tex = getFaceTexture(2);
                    float u0 = tex.atlasX * uvStep;
                    float v0 = tex.atlasY * uvStep;
                    float u1 = u0 + uvStep;
                    float v1 = v0 + uvStep;

                    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());
                    m_vertices.push_back({worldX, worldY + BLOCK_SIZE, worldZ, 1,1,1, u0, v1});
                    m_vertices.push_back({worldX, worldY + BLOCK_SIZE, worldZ + BLOCK_SIZE, 1,1,1, u0, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY + BLOCK_SIZE, worldZ + BLOCK_SIZE, 1,1,1, u1, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY + BLOCK_SIZE, worldZ, 1,1,1, u1, v1});

                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 1);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 3);
                }

                // -Y face (bottom)
                if (shouldRenderFace(0, -1, 0)) {
                    auto tex = getFaceTexture(3);
                    float u0 = tex.atlasX * uvStep;
                    float v0 = tex.atlasY * uvStep;
                    float u1 = u0 + uvStep;
                    float v1 = v0 + uvStep;

                    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());
                    m_vertices.push_back({worldX, worldY, worldZ + BLOCK_SIZE, 1,1,1, u0, v1});
                    m_vertices.push_back({worldX, worldY, worldZ, 1,1,1, u0, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY, worldZ, 1,1,1, u1, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY, worldZ + BLOCK_SIZE, 1,1,1, u1, v1});

                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 1);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 3);
                }

                // +Z face (front)
                if (shouldRenderFace(0, 0, 1)) {
                    auto tex = getFaceTexture(4);
                    float u0 = tex.atlasX * uvStep;
                    float v0 = tex.atlasY * uvStep;
                    float u1 = u0 + uvStep;
                    float v1 = v0 + uvStep;

                    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());
                    m_vertices.push_back({worldX, worldY, worldZ + BLOCK_SIZE, 1,1,1, u0, v1});
                    m_vertices.push_back({worldX, worldY + BLOCK_SIZE, worldZ + BLOCK_SIZE, 1,1,1, u0, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY + BLOCK_SIZE, worldZ + BLOCK_SIZE, 1,1,1, u1, v0});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY, worldZ + BLOCK_SIZE, 1,1,1, u1, v1});

                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 1);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 3);
                }

                // -Z face (back)
                if (shouldRenderFace(0, 0, -1)) {
                    auto tex = getFaceTexture(5);
                    float u0 = tex.atlasX * uvStep;
                    float v0 = tex.atlasY * uvStep;
                    float u1 = u0 + uvStep;
                    float v1 = v0 + uvStep;

                    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY, worldZ, 1,1,1, u0, v1});
                    m_vertices.push_back({worldX + BLOCK_SIZE, worldY + BLOCK_SIZE, worldZ, 1,1,1, u0, v0});
                    m_vertices.push_back({worldX, worldY + BLOCK_SIZE, worldZ, 1,1,1, u1, v0});
                    m_vertices.push_back({worldX, worldY, worldZ, 1,1,1, u1, v1});

                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 1);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 0);
                    m_indices.push_back(baseIndex + 2);
                    m_indices.push_back(baseIndex + 3);
                }
            }
        }
    }

    m_vertexCount = static_cast<uint32_t>(m_vertices.size());
    m_indexCount = static_cast<uint32_t>(m_indices.size());
}

void Chunk::createVertexBuffer(VulkanRenderer* renderer) {
    if (m_vertexCount == 0) {
        return;  // No vertices to upload
    }

    // Destroy old buffers if they exist
    destroyBuffers(renderer);

    // Create vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * m_vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    renderer->createBuffer(vertexBufferSize,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(renderer->getDevice(), stagingBufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, m_vertices.data(), (size_t)vertexBufferSize);
    vkUnmapMemory(renderer->getDevice(), stagingBufferMemory);

    renderer->createBuffer(vertexBufferSize,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          m_vertexBuffer, m_vertexBufferMemory);

    renderer->copyBuffer(stagingBuffer, m_vertexBuffer, vertexBufferSize);

    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);

    // Create index buffer
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_indices.size();

    renderer->createBuffer(indexBufferSize,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuffer, stagingBufferMemory);

    vkMapMemory(renderer->getDevice(), stagingBufferMemory, 0, indexBufferSize, 0, &data);
    memcpy(data, m_indices.data(), (size_t)indexBufferSize);
    vkUnmapMemory(renderer->getDevice(), stagingBufferMemory);

    renderer->createBuffer(indexBufferSize,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          m_indexBuffer, m_indexBufferMemory);

    renderer->copyBuffer(stagingBuffer, m_indexBuffer, indexBufferSize);

    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);
}

void Chunk::destroyBuffers(VulkanRenderer* renderer) {
    VkDevice device = renderer->getDevice();

    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }

    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }

    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }

    if (m_indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_indexBufferMemory, nullptr);
        m_indexBufferMemory = VK_NULL_HANDLE;
    }
}

void Chunk::render(VkCommandBuffer commandBuffer) {
    if (m_vertexCount == 0) {
        return;  // Nothing to render
    }

    // Bind vertex buffer
    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // Bind index buffer
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Draw indexed
    vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
}

int Chunk::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return -1;  // Out of bounds
    }
    return m_blocks[x][y][z];
}

void Chunk::setBlock(int x, int y, int z, int blockID) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return;  // Out of bounds
    }
    m_blocks[x][y][z] = blockID;
}
