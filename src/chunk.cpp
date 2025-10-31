// chunk.cpp
#include "chunk.h"
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
    if (!s_noise) return 16; // Default height if noise not initialized

    float noise = s_noise->GetNoise(worldX, worldZ);
    int height = 16 + (int)(noise * 12.0f);
    return height;
}

// Constructor: Initialize chunk at world grid coordinates (x, y, z)
Chunk::Chunk(int x, int y, int z) : m_x(x), m_y(y), m_z(z), m_vertexBuffer(VK_NULL_HANDLE), m_vertexBufferMemory(VK_NULL_HANDLE), m_vertexCount(0) {
    // Calculate world-space bounds (blocks are 0.5 units in size)
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

            // Sample noise for terrain
            float noise = s_noise->GetNoise(worldX, worldZ);

            // Convert noise (-1 to 1) to height (4 to 28) for more dramatic hills
            int height = 16 + (int)(noise * 12.0f);

            // Fill blocks based on height
            for (int Y = 0; Y < HEIGHT; ++Y) {
                if (Y < height - 4)      m_blocks[X][Y][Z] = stoneID;
                else if (Y < height)     m_blocks[X][Y][Z] = dirtID;
                else if (Y == height)    m_blocks[X][Y][Z] = grassID;
                else                     m_blocks[X][Y][Z] = 0; // Air
            }
        }
    }
}

Chunk::~Chunk() {
    // Vulkan cleanup is handled separately
    // The buffers should be cleaned up before destruction by the renderer
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

    std::vector<Vertex> verts;
    verts.reserve(WIDTH * HEIGHT * DEPTH * 36);

    // Helper lambda to check if a block is solid (non-air)
    auto isSolid = [this](int x, int y, int z) -> bool {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH)
            return false; // Treat out-of-bounds as air (will show boundary faces)
        return m_blocks[x][y][z] != 0;
    };

    // Iterate over every block in the chunk
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Z = 0; Z < DEPTH;  ++Z) {
            for(int Y = 0; Y < HEIGHT; ++Y) {
                int id = m_blocks[X][Y][Z];
                if (id == 0) continue; // Skip air

                // Look up block definition by ID
                const BlockDefinition& def = BlockRegistry::instance().get(id);
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

                // Face culling: only render faces exposed to air
                float bx = float(X) * 0.5f;
                float by = float(Y) * 0.5f;
                float bz = float(Z) * 0.5f;

                // Front face (z=0, facing -Z direction)
                if (!isSolid(X, Y, Z - 1)) {
                    for (int i = 0; i < 18; i += 3) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        verts.push_back(v);
                    }
                }

                // Back face (z=0.5, facing +Z direction)
                if (!isSolid(X, Y, Z + 1)) {
                    for (int i = 18; i < 36; i += 3) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        verts.push_back(v);
                    }
                }

                // Left face (x=0, facing -X direction)
                if (!isSolid(X - 1, Y, Z)) {
                    for (int i = 36; i < 54; i += 3) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        verts.push_back(v);
                    }
                }

                // Right face (x=0.5, facing +X direction)
                if (!isSolid(X + 1, Y, Z)) {
                    for (int i = 54; i < 72; i += 3) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        verts.push_back(v);
                    }
                }

                // Top face (y=0.5, facing +Y direction)
                if (!isSolid(X, Y + 1, Z)) {
                    for (int i = 72; i < 90; i += 3) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        verts.push_back(v);
                    }
                }

                // Bottom face (y=0, facing -Y direction)
                if (!isSolid(X, Y - 1, Z)) {
                    for (int i = 90; i < 108; i += 3) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
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
