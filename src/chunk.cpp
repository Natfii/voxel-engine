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
std::unique_ptr<FastNoiseLite> Chunk::s_noise = nullptr;

void Chunk::initNoise(int seed) {
    if (s_noise == nullptr) {
        s_noise = std::make_unique<FastNoiseLite>(seed);
        s_noise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        s_noise->SetFractalType(FastNoiseLite::FractalType_FBm);
        s_noise->SetFractalOctaves(4);
        s_noise->SetFractalLacunarity(2.0f);
        s_noise->SetFractalGain(0.5f);
        s_noise->SetFrequency(0.015f);  // Lower frequency for larger, smoother hills
    }
}

void Chunk::cleanupNoise() {
    s_noise.reset();  // Automatic cleanup with unique_ptr
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
                        m_blocks[x][y][z] = 3;  // Grass Block on top (ID 3)
                    } else if (depthFromSurface <= 4) {
                        m_blocks[x][y][z] = 2;  // Dirt below grass (ID 2)
                    } else {
                        m_blocks[x][y][z] = 1;  // Stone at bottom (ID 1)
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
void Chunk::generateMesh(World* world) {
    // Define a 0.5-unit cube (4 vertices per face, 24 total) - indexed rendering
    static constexpr std::array<float, 72> cube = {{
        // front face (z = 0) - 4 vertices
        0,0,0,  0.5f,0,0,  0.5f,0.5f,0,  0,0.5f,0,
        // back face (z = 0.5) - 4 vertices
        0.5f,0,0.5f,  0,0,0.5f,  0,0.5f,0.5f,  0.5f,0.5f,0.5f,
        // left face (x = 0) - 4 vertices
        0,0,0.5f,  0,0,0,  0,0.5f,0,  0,0.5f,0.5f,
        // right face (x = 0.5) - 4 vertices
        0.5f,0,0,  0.5f,0,0.5f,  0.5f,0.5f,0.5f,  0.5f,0.5f,0,
        // top face (y = 0.5) - 4 vertices
        0,0.5f,0,  0.5f,0.5f,0,  0.5f,0.5f,0.5f,  0,0.5f,0.5f,
        // bottom face (y = 0) - 4 vertices
        0,0,0.5f,  0.5f,0,0.5f,  0.5f,0,0,  0,0,0
    }};

    // UV coordinates for each vertex (4 per face)
    // Note: V coordinates are flipped for side faces to prevent upside-down textures
    static constexpr std::array<float, 48> cubeUVs = {{
        // front face: 4 vertices - V flipped
        0,1,  1,1,  1,0,  0,0,
        // back face - V flipped
        0,1,  1,1,  1,0,  0,0,
        // left face - V flipped
        0,1,  1,1,  1,0,  0,0,
        // right face - V flipped
        0,1,  1,1,  1,0,  0,0,
        // top face - normal orientation
        0,0,  1,0,  1,1,  0,1,
        // bottom face - normal orientation
        0,0,  1,0,  1,1,  0,1
    }};

    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    // Reserve space for estimated visible faces (roughly 30% of blocks visible, 3 faces each on average)
    // With indexed rendering: 4 vertices per face instead of 6
    verts.reserve(WIDTH * HEIGHT * DEPTH * 12 / 10);  // 4 vertices per face
    indices.reserve(WIDTH * HEIGHT * DEPTH * 18 / 10); // 6 indices per face (same as before)

    // Get block registry (needed for liquid checks)
    auto& registry = BlockRegistry::instance();
    int atlasGridSize = registry.getAtlasGridSize();
    float uvScale = (atlasGridSize > 0) ? (1.0f / atlasGridSize) : 1.0f;

    // Helper lambda to check if a block is solid (non-air)
    // THIS VERSION CHECKS NEIGHBORING CHUNKS via World
    auto isSolid = [this, world](int x, int y, int z) -> bool {
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            // Inside this chunk
            return m_blocks[x][y][z] != 0;
        }

        // Out of bounds - check neighboring chunk via World
        // Convert local coordinates to world coordinates
        int worldBlockX = m_x * WIDTH + x;
        int worldBlockY = m_y * HEIGHT + y;
        int worldBlockZ = m_z * DEPTH + z;

        // Convert to world position (blocks are 0.5 units)
        float worldX = worldBlockX * 0.5f;
        float worldY = worldBlockY * 0.5f;
        float worldZ = worldBlockZ * 0.5f;

        // Query the world (returns 0 for air or out-of-world bounds)
        int blockID = world->getBlockAt(worldX, worldY, worldZ);
        return blockID != 0;
    };

    // Helper lambda to check if a block is liquid
    auto isLiquid = [this, world, &registry](int x, int y, int z) -> bool {
        int blockID;
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            blockID = m_blocks[x][y][z];
        } else {
            // Out of bounds - check world
            int worldBlockX = m_x * WIDTH + x;
            int worldBlockY = m_y * HEIGHT + y;
            int worldBlockZ = m_z * DEPTH + z;
            float worldX = worldBlockX * 0.5f;
            float worldY = worldBlockY * 0.5f;
            float worldZ = worldBlockZ * 0.5f;
            blockID = world->getBlockAt(worldX, worldY, worldZ);
        }
        if (blockID == 0) return false;
        return registry.get(blockID).isLiquid;
    };

    // Iterate over every block in the chunk (optimized order for cache locality)
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Y = 0; Y < HEIGHT; ++Y) {
            for(int Z = 0; Z < DEPTH;  ++Z) {
                int id = m_blocks[X][Y][Z];
                if (id == 0) continue; // Skip air

                // Look up block definition by ID
                const BlockDefinition& def = registry.get(id);
                float cr, cg, cb, ca;
                if (def.hasColor) {
                    // Use the block's defined color
                    cr = def.color.r;
                    cg = def.color.g;
                    cb = def.color.b;
                } else {
                    // No color defined (likely has a texture); use white
                    cr = cg = cb = 1.0f;
                }
                // Set alpha based on transparency (1.0 - transparency for proper blending)
                ca = 1.0f - def.transparency;

                // Helper to get UV coordinates for a specific face with texture variation
                auto getUVsForFace = [&](const BlockDefinition::FaceTexture& face) -> std::pair<float, float> {
                    float uMin = face.atlasX * uvScale;
                    float vMin = face.atlasY * uvScale;

                    // Apply texture variation if enabled (per-face variation)
                    float zoomFactor = face.variation;
                    if (zoomFactor > 1.0f) {
                        int worldX = m_x * WIDTH + X;
                        int worldY = m_y * HEIGHT + Y;
                        int worldZ = m_z * DEPTH + Z;

                        // Simple hash function for pseudo-random offset
                        unsigned int seed = (worldX * 73856093) ^ (worldY * 19349663) ^ (worldZ * 83492791);
                        float randU = ((seed >> 0) & 0xFF) / 255.0f;
                        float randV = ((seed >> 8) & 0xFF) / 255.0f;

                        // Calculate how much space we can shift within
                        float maxShift = uvScale * (1.0f - 1.0f / zoomFactor);
                        float uShift = randU * maxShift;
                        float vShift = randV * maxShift;

                        uMin += uShift;
                        vMin += vShift;
                    }

                    return {uMin, vMin};
                };

                // Calculate world position for this block
                float bx = float(m_x * WIDTH + X) * 0.5f;
                float by = float(m_y * HEIGHT + Y) * 0.5f;
                float bz = float(m_z * DEPTH + Z) * 0.5f;

                // Helper to render a face with the appropriate texture (indexed rendering)
                auto renderFace = [&](const BlockDefinition::FaceTexture& faceTexture, int cubeStart, int uvStart) {
                    auto [uMin, vMin] = getUVsForFace(faceTexture);
                    float uvScaleZoomed = uvScale;

                    // Recalculate uvScaleZoomed if texture variation is enabled (per-face)
                    if (faceTexture.variation > 1.0f) {
                        uvScaleZoomed = uvScale / faceTexture.variation;
                    }

                    // Get the base index for these vertices
                    uint32_t baseIndex = static_cast<uint32_t>(verts.size());

                    // Create 4 vertices for this face (corners of the quad)
                    for (int i = cubeStart, uv = uvStart; i < cubeStart + 12; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb; v.a = ca;
                        v.u = uMin + cubeUVs[uv+0] * uvScaleZoomed;
                        v.v = vMin + cubeUVs[uv+1] * uvScaleZoomed;
                        verts.push_back(v);
                    }

                    // Create 6 indices for 2 triangles (0,1,2 and 0,2,3)
                    indices.push_back(baseIndex + 0);
                    indices.push_back(baseIndex + 1);
                    indices.push_back(baseIndex + 2);
                    indices.push_back(baseIndex + 0);
                    indices.push_back(baseIndex + 2);
                    indices.push_back(baseIndex + 3);
                };

                // Select appropriate texture for each face
                const BlockDefinition::FaceTexture& frontTex = def.useCubeMap ? def.front : def.all;
                const BlockDefinition::FaceTexture& backTex = def.useCubeMap ? def.back : def.all;
                const BlockDefinition::FaceTexture& leftTex = def.useCubeMap ? def.left : def.all;
                const BlockDefinition::FaceTexture& rightTex = def.useCubeMap ? def.right : def.all;
                const BlockDefinition::FaceTexture& topTex = def.useCubeMap ? def.top : def.all;
                const BlockDefinition::FaceTexture& bottomTex = def.useCubeMap ? def.bottom : def.all;

                // Special rendering for liquid blocks
                bool isCurrentLiquid = def.isLiquid;

                // Front face (z=0, facing -Z direction)
                if (!isSolid(X, Y, Z - 1) || (isCurrentLiquid && !isLiquid(X, Y, Z - 1))) {
                    // Render if neighbor is air, or if this is liquid and neighbor is non-liquid
                    if (!isCurrentLiquid || !isLiquid(X, Y, Z - 1)) {
                        renderFace(frontTex, 0, 0);
                    }
                }

                // Back face (z=0.5, facing +Z direction)
                if (!isSolid(X, Y, Z + 1) || (isCurrentLiquid && !isLiquid(X, Y, Z + 1))) {
                    if (!isCurrentLiquid || !isLiquid(X, Y, Z + 1)) {
                        renderFace(backTex, 12, 8);
                    }
                }

                // Left face (x=0, facing -X direction)
                if (!isSolid(X - 1, Y, Z) || (isCurrentLiquid && !isLiquid(X - 1, Y, Z))) {
                    if (!isCurrentLiquid || !isLiquid(X - 1, Y, Z)) {
                        renderFace(leftTex, 24, 16);
                    }
                }

                // Right face (x=0.5, facing +X direction)
                if (!isSolid(X + 1, Y, Z) || (isCurrentLiquid && !isLiquid(X + 1, Y, Z))) {
                    if (!isCurrentLiquid || !isLiquid(X + 1, Y, Z)) {
                        renderFace(rightTex, 36, 24);
                    }
                }

                // Top face (y=0.5, facing +Y direction)
                // For liquids: only render if exposed to air (not another liquid)
                if (!isSolid(X, Y + 1, Z)) {
                    renderFace(topTex, 48, 32);
                } else if (isCurrentLiquid && !isLiquid(X, Y + 1, Z)) {
                    renderFace(topTex, 48, 32);
                }

                // Bottom face (y=0, facing -Y direction)
                if (!isSolid(X, Y - 1, Z) || (isCurrentLiquid && !isLiquid(X, Y - 1, Z))) {
                    if (!isCurrentLiquid || !isLiquid(X, Y - 1, Z)) {
                        renderFace(bottomTex, 60, 40);
                    }
                }
            }
        }
    }

    m_vertexCount = static_cast<uint32_t>(verts.size());
    m_indexCount = static_cast<uint32_t>(indices.size());
    m_vertices = std::move(verts);
    m_indices = std::move(indices);
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
