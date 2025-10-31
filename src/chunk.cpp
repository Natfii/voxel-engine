// chunk.cpp
#include "chunk.h"
#include "world.h"          // for neighbor chunk checking
#include "vulkan_renderer.h"
#include "block_system.h"   // for BlockRegistry and BlockDefinition
#include "FastNoiseLite.h"  // for procedural terrain generation
#include <vector>
#include <array>
#include <cstring>  // for memcpy

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
    if (s_noise) {
        delete s_noise;
        s_noise = nullptr;
    }
}

int Chunk::getTerrainHeightAt(float worldX, float worldZ) {
    if (!s_noise) return 64; // Default height if noise not initialized

    float noise = s_noise->GetNoise(worldX, worldZ);
    int height = 64 + (int)(noise * 12.0f);  // Must match terrain generation formula
    return height;
}

// Constructor: Initialize chunk at world grid coordinates (x, y, z)
Chunk::Chunk(int x, int y, int z) : m_x(x), m_y(y), m_z(z), m_vertexBuffer(VK_NULL_HANDLE), m_vertexBufferMemory(VK_NULL_HANDLE), m_vertexCount(0), m_visible(false) {
    // Calculate world-space bounds (blocks are 0.5 units in size)
    // No epsilon - exact bounds prevent chunk overlap
    m_minBounds = glm::vec3(
        float(m_x * WIDTH) * 0.5f,
        float(m_y * HEIGHT) * 0.5f,
        float(m_z * DEPTH) * 0.5f
    );
    m_maxBounds = glm::vec3(
        float((m_x + 1) * WIDTH) * 0.5f,
        float((m_y + 1) * HEIGHT) * 0.5f,
        float((m_z + 1) * DEPTH) * 0.5f
    );

    // Determine block IDs from registry (fallback to Air=0 if not found)
    auto& registry = BlockRegistry::instance();
    int grassID = registry.getID("Grass");
    if (grassID < 0) grassID = 0;
    int dirtID = registry.getID("Dirt");
    if (dirtID < 0) dirtID = 0;
    int stoneID = registry.getID("Stone");
    if (stoneID < 0) stoneID = 0;
    
    //Begin terrain generation using noise
    for (int X = 0; X < WIDTH; ++X) {
        for (int Z = 0; Z < DEPTH; ++Z) {
            // Calculate world position
            float worldX = (m_x * WIDTH + X) * 0.5f;  // *0.5 for your scaled blocks
            float worldZ = (m_z * DEPTH + Z) * 0.5f;

            // Sample noise for terrain height (same for entire vertical column)
            float noise = s_noise->GetNoise(worldX, worldZ);

            // Convert noise (-1 to 1) to world height in blocks
            // Terrain surface ranges from Y=52 to Y=76 (average Y=64, like Minecraft sea level)
            // This leaves ~60 blocks below for underground mining and caves
            int terrainHeight = 64 + (int)(noise * 12.0f);

            // Fill blocks based on height (using world Y coordinates for proper cross-chunk generation)
            for (int Y = 0; Y < HEIGHT; ++Y) {
                // Calculate world Y coordinate for this block
                int worldY = m_y * HEIGHT + Y;

                // Minecraft-style terrain generation rules:
                if (worldY < terrainHeight - 4) {
                    // Deep underground: Stone
                    m_blocks[X][Y][Z] = stoneID;
                }
                else if (worldY < terrainHeight) {
                    // Near surface: Dirt layer (4 blocks deep)
                    m_blocks[X][Y][Z] = dirtID;
                }
                else if (worldY == terrainHeight) {
                    // Surface block: Grass ONLY if:
                    // 1. There's dirt below (implicit - this IS the terrain surface)
                    // 2. Has sunlight (nothing above in the world)
                    // Since this is the terrain surface (worldY == terrainHeight),
                    // there's nothing above it by definition - it can see the sky
                    m_blocks[X][Y][Z] = grassID;
                }
                else {
                    // Above terrain surface: Air
                    m_blocks[X][Y][Z] = 0;
                }
            }
        }
    }
}

Chunk::~Chunk() {
    // Vulkan cleanup is handled separately
    // The buffers should be cleaned up before destruction by the renderer
}

int Chunk::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return -1; // Out of bounds
    }
    return m_blocks[x][y][z];
}

void Chunk::generate() {
    // Define a 0.5-unit cube (36 vertices) - scaled for smaller blocks
    static constexpr std::array<float, 108> cube = {{
        // front face (z = 0)
        0,0,0,  0.5f,0,0,  0.5f,0.5f,0,   0,0,0,  0.5f,0.5f,0,  0,0.5f,0,
        // back face (z = 0.5)
        0.5f,0,0.5f,  0,0,0.5f,  0,0.5f,0.5f,   0.5f,0,0.5f,  0,0.5f,0.5f,  0.5f,0.5f,0.5f,
        // left face (x = 0)
        0,0,0.5f,  0,0,0,  0,0.5f,0,   0,0,0.5f,  0,0.5f,0,  0,0.5f,0.5f,
        // right face (x = 0.5)
        0.5f,0,0,  0.5f,0,0.5f,  0.5f,0.5f,0.5f,   0.5f,0,0,  0.5f,0.5f,0.5f,  0.5f,0.5f,0,
        // top face (y = 0.5)
        0,0.5f,0,  0.5f,0.5f,0,  0.5f,0.5f,0.5f,   0,0.5f,0,  0.5f,0.5f,0.5f,  0,0.5f,0.5f,
        // bottom face (y = 0)
        0,0,0.5f,  0.5f,0,0.5f,  0.5f,0,0,   0,0,0.5f,  0.5f,0,0,  0,0,0
    }};

    // UV coordinates for each vertex (matches cube structure above)
    // Each face gets standard UV mapping: (0,0) bottom-left to (1,1) top-right
    static constexpr std::array<float, 72> cubeUVs = {{
        // front face: 6 vertices (2 triangles)
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // back face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // left face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // right face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // top face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // bottom face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1
    }};

    std::vector<Vertex> verts;
    // Reserve space for estimated visible faces (roughly 30% of blocks visible, 3 faces each on average)
    verts.reserve(WIDTH * HEIGHT * DEPTH * 18 / 10);

    // Helper lambda to check if a block is solid (non-air)
    auto isSolid = [this](int x, int y, int z) -> bool {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH)
            return false; // Treat out-of-bounds as air (will show boundary faces)
        return m_blocks[x][y][z] != 0;
    };

    // Get atlas grid size for UV calculations
    auto& registry = BlockRegistry::instance();
    int atlasGridSize = registry.getAtlasGridSize();
    float uvScale = (atlasGridSize > 0) ? (1.0f / atlasGridSize) : 1.0f;

    // Iterate over every block in the chunk (optimized order for cache locality)
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Y = 0; Y < HEIGHT; ++Y) {
            for(int Z = 0; Z < DEPTH;  ++Z) {
                int id = m_blocks[X][Y][Z];
                if (id == 0) continue; // Skip air

                // Look up block definition by ID
                const BlockDefinition& def = registry.get(id);
                float cr, cg, cb;
                if (def.hasColor) {
                    // Use the block's defined color
                    cr = def.color.r;
                    cg = def.color.g;
                    cb = def.color.b;
                } else {
                    // No color defined (likely has a texture); use white
                    cr = cg = cb = 1.0f;
                }

                // Calculate atlas UV offset for this block's texture
                float uMin = def.atlasX * uvScale;
                float vMin = def.atlasY * uvScale;

                // Calculate world position for this block
                // Convert from chunk-local coordinates to world coordinates
                float bx = float(m_x * WIDTH + X) * 0.5f;
                float by = float(m_y * HEIGHT + Y) * 0.5f;
                float bz = float(m_z * DEPTH + Z) * 0.5f;

                // Front face (z=0, facing -Z direction)
                if (!isSolid(X, Y, Z - 1)) {
                    for (int i = 0, uv = 0; i < 18; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Back face (z=0.5, facing +Z direction)
                if (!isSolid(X, Y, Z + 1)) {
                    for (int i = 18, uv = 12; i < 36; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Left face (x=0, facing -X direction)
                if (!isSolid(X - 1, Y, Z)) {
                    for (int i = 36, uv = 24; i < 54; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Right face (x=0.5, facing +X direction)
                if (!isSolid(X + 1, Y, Z)) {
                    for (int i = 54, uv = 36; i < 72; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Top face (y=0.5, facing +Y direction)
                if (!isSolid(X, Y + 1, Z)) {
                    for (int i = 72, uv = 48; i < 90; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Bottom face (y=0, facing -Y direction)
                if (!isSolid(X, Y - 1, Z)) {
                    for (int i = 90, uv = 60; i < 108; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }
            }
        }
    }

    m_vertexCount = static_cast<uint32_t>(verts.size());
    m_vertices = std::move(verts);
}

void Chunk::generateMesh(World* world) {
    // Define a 0.5-unit cube (36 vertices) - scaled for smaller blocks
    static constexpr std::array<float, 108> cube = {{
        // front face (z = 0)
        0,0,0,  0.5f,0,0,  0.5f,0.5f,0,   0,0,0,  0.5f,0.5f,0,  0,0.5f,0,
        // back face (z = 0.5)
        0.5f,0,0.5f,  0,0,0.5f,  0,0.5f,0.5f,   0.5f,0,0.5f,  0,0.5f,0.5f,  0.5f,0.5f,0.5f,
        // left face (x = 0)
        0,0,0.5f,  0,0,0,  0,0.5f,0,   0,0,0.5f,  0,0.5f,0,  0,0.5f,0.5f,
        // right face (x = 0.5)
        0.5f,0,0,  0.5f,0,0.5f,  0.5f,0.5f,0.5f,   0.5f,0,0,  0.5f,0.5f,0.5f,  0.5f,0.5f,0,
        // top face (y = 0.5)
        0,0.5f,0,  0.5f,0.5f,0,  0.5f,0.5f,0.5f,   0,0.5f,0,  0.5f,0.5f,0.5f,  0,0.5f,0.5f,
        // bottom face (y = 0)
        0,0,0.5f,  0.5f,0,0.5f,  0.5f,0,0,   0,0,0.5f,  0.5f,0,0,  0,0,0
    }};

    // UV coordinates for each vertex (matches cube structure above)
    static constexpr std::array<float, 72> cubeUVs = {{
        // front face: 6 vertices (2 triangles)
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // back face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // left face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // right face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // top face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // bottom face
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1
    }};

    std::vector<Vertex> verts;
    // Reserve space for estimated visible faces (roughly 30% of blocks visible, 3 faces each on average)
    verts.reserve(WIDTH * HEIGHT * DEPTH * 18 / 10);

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

    // Get atlas grid size for UV calculations
    auto& registry = BlockRegistry::instance();
    int atlasGridSize = registry.getAtlasGridSize();
    float uvScale = (atlasGridSize > 0) ? (1.0f / atlasGridSize) : 1.0f;

    // Iterate over every block in the chunk (optimized order for cache locality)
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Y = 0; Y < HEIGHT; ++Y) {
            for(int Z = 0; Z < DEPTH;  ++Z) {
                int id = m_blocks[X][Y][Z];
                if (id == 0) continue; // Skip air

                // Look up block definition by ID
                const BlockDefinition& def = registry.get(id);
                float cr, cg, cb;
                if (def.hasColor) {
                    // Use the block's defined color
                    cr = def.color.r;
                    cg = def.color.g;
                    cb = def.color.b;
                } else {
                    // No color defined (likely has a texture); use white
                    cr = cg = cb = 1.0f;
                }

                // Calculate atlas UV offset for this block's texture
                float uMin = def.atlasX * uvScale;
                float vMin = def.atlasY * uvScale;

                // Calculate world position for this block
                // Convert from chunk-local coordinates to world coordinates
                float bx = float(m_x * WIDTH + X) * 0.5f;
                float by = float(m_y * HEIGHT + Y) * 0.5f;
                float bz = float(m_z * DEPTH + Z) * 0.5f;

                // Front face (z=0, facing -Z direction)
                if (!isSolid(X, Y, Z - 1)) {
                    for (int i = 0, uv = 0; i < 18; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Back face (z=0.5, facing +Z direction)
                if (!isSolid(X, Y, Z + 1)) {
                    for (int i = 18, uv = 12; i < 36; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Left face (x=0, facing -X direction)
                if (!isSolid(X - 1, Y, Z)) {
                    for (int i = 36, uv = 24; i < 54; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Right face (x=0.5, facing +X direction)
                if (!isSolid(X + 1, Y, Z)) {
                    for (int i = 54, uv = 36; i < 72; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Top face (y=0.5, facing +Y direction)
                if (!isSolid(X, Y + 1, Z)) {
                    for (int i = 72, uv = 48; i < 90; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }

                // Bottom face (y=0, facing -Y direction)
                if (!isSolid(X, Y - 1, Z)) {
                    for (int i = 90, uv = 60; i < 108; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScale;
                        v.v = vMin + cubeUVs[uv+1] * uvScale;
                        verts.push_back(v);
                    }
                }
            }
        }
    }

    m_vertexCount = static_cast<uint32_t>(verts.size());
    m_vertices = std::move(verts);
}

void Chunk::createVertexBuffer(VulkanRenderer* renderer) {
    if (m_vertices.empty()) return;

    VkDeviceSize bufferSize = sizeof(Vertex) * m_vertices.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    renderer->createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuffer, stagingBufferMemory);

    // Copy vertex data to staging buffer
    void* data;
    vkMapMemory(renderer->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(renderer->getDevice(), stagingBufferMemory);

    // Create vertex buffer on device
    renderer->createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          m_vertexBuffer, m_vertexBufferMemory);

    // Copy from staging to device
    renderer->copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);
}

void Chunk::render(VkCommandBuffer commandBuffer) {
    if (m_vertexCount == 0 || m_vertexBuffer == VK_NULL_HANDLE) return;

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
}
