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

void Chunk::setBlock(int x, int y, int z, int blockID) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return; // Out of bounds, do nothing
    }
    m_blocks[x][y][z] = blockID;
    // Note: Caller is responsible for calling generateMesh() and createBuffer() to update visuals
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
    // Note: V coordinates are flipped for side faces to prevent upside-down textures
    static constexpr std::array<float, 72> cubeUVs = {{
        // front face: 6 vertices (2 triangles) - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // back face - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // left face - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // right face - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // top face - normal orientation
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // bottom face - normal orientation
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

                // Helper to get UV coordinates for a specific face with texture variation
                auto getUVsForFace = [&](const BlockDefinition::FaceTexture& face) -> std::pair<float, float> {
                    float uMin = face.atlasX * uvScale;
                    float vMin = face.atlasY * uvScale;

                    // Apply texture variation if enabled
                    float zoomFactor = def.textureVariation;
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

                // Helper to render a face with the appropriate texture
                auto renderFace = [&](const BlockDefinition::FaceTexture& faceTexture, int cubeStart, int uvStart) {
                    auto [uMin, vMin] = getUVsForFace(faceTexture);
                    float uvScaleZoomed = uvScale;

                    // Recalculate uvScaleZoomed if texture variation is enabled
                    if (def.textureVariation > 1.0f) {
                        uvScaleZoomed = uvScale / def.textureVariation;
                    }

                    for (int i = cubeStart, uv = uvStart; i < cubeStart + 18; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScaleZoomed;
                        v.v = vMin + cubeUVs[uv+1] * uvScaleZoomed;
                        verts.push_back(v);
                    }
                };

                // Select appropriate texture for each face
                const BlockDefinition::FaceTexture& frontTex = def.useCubeMap ? def.front : def.all;
                const BlockDefinition::FaceTexture& backTex = def.useCubeMap ? def.back : def.all;
                const BlockDefinition::FaceTexture& leftTex = def.useCubeMap ? def.left : def.all;
                const BlockDefinition::FaceTexture& rightTex = def.useCubeMap ? def.right : def.all;
                const BlockDefinition::FaceTexture& topTex = def.useCubeMap ? def.top : def.all;
                const BlockDefinition::FaceTexture& bottomTex = def.useCubeMap ? def.bottom : def.all;

                // Front face (z=0, facing -Z direction)
                if (!isSolid(X, Y, Z - 1)) {
                    renderFace(frontTex, 0, 0);
                }

                // Back face (z=0.5, facing +Z direction)
                if (!isSolid(X, Y, Z + 1)) {
                    renderFace(backTex, 18, 12);
                }

                // Left face (x=0, facing -X direction)
                if (!isSolid(X - 1, Y, Z)) {
                    renderFace(leftTex, 36, 24);
                }

                // Right face (x=0.5, facing +X direction)
                if (!isSolid(X + 1, Y, Z)) {
                    renderFace(rightTex, 54, 36);
                }

                // Top face (y=0.5, facing +Y direction)
                if (!isSolid(X, Y + 1, Z)) {
                    renderFace(topTex, 72, 48);
                }

                // Bottom face (y=0, facing -Y direction)
                if (!isSolid(X, Y - 1, Z)) {
                    renderFace(bottomTex, 90, 60);
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
    // Note: V coordinates are flipped for side faces to prevent upside-down textures
    static constexpr std::array<float, 72> cubeUVs = {{
        // front face: 6 vertices (2 triangles) - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // back face - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // left face - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // right face - V flipped
        0,1,  1,1,  1,0,   0,1,  1,0,  0,0,
        // top face - normal orientation
        0,0,  1,0,  1,1,   0,0,  1,1,  0,1,
        // bottom face - normal orientation
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

                // Helper to get UV coordinates for a specific face with texture variation
                auto getUVsForFace = [&](const BlockDefinition::FaceTexture& face) -> std::pair<float, float> {
                    float uMin = face.atlasX * uvScale;
                    float vMin = face.atlasY * uvScale;

                    // Apply texture variation if enabled
                    float zoomFactor = def.textureVariation;
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

                // Helper to render a face with the appropriate texture
                auto renderFace = [&](const BlockDefinition::FaceTexture& faceTexture, int cubeStart, int uvStart) {
                    auto [uMin, vMin] = getUVsForFace(faceTexture);
                    float uvScaleZoomed = uvScale;

                    // Recalculate uvScaleZoomed if texture variation is enabled
                    if (def.textureVariation > 1.0f) {
                        uvScaleZoomed = uvScale / def.textureVariation;
                    }

                    for (int i = cubeStart, uv = uvStart; i < cubeStart + 18; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        v.y = cube[i+1] + by;
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb;
                        v.u = uMin + cubeUVs[uv+0] * uvScaleZoomed;
                        v.v = vMin + cubeUVs[uv+1] * uvScaleZoomed;
                        verts.push_back(v);
                    }
                };

                // Select appropriate texture for each face
                const BlockDefinition::FaceTexture& frontTex = def.useCubeMap ? def.front : def.all;
                const BlockDefinition::FaceTexture& backTex = def.useCubeMap ? def.back : def.all;
                const BlockDefinition::FaceTexture& leftTex = def.useCubeMap ? def.left : def.all;
                const BlockDefinition::FaceTexture& rightTex = def.useCubeMap ? def.right : def.all;
                const BlockDefinition::FaceTexture& topTex = def.useCubeMap ? def.top : def.all;
                const BlockDefinition::FaceTexture& bottomTex = def.useCubeMap ? def.bottom : def.all;

                // Front face (z=0, facing -Z direction)
                if (!isSolid(X, Y, Z - 1)) {
                    renderFace(frontTex, 0, 0);
                }

                // Back face (z=0.5, facing +Z direction)
                if (!isSolid(X, Y, Z + 1)) {
                    renderFace(backTex, 18, 12);
                }

                // Left face (x=0, facing -X direction)
                if (!isSolid(X - 1, Y, Z)) {
                    renderFace(leftTex, 36, 24);
                }

                // Right face (x=0.5, facing +X direction)
                if (!isSolid(X + 1, Y, Z)) {
                    renderFace(rightTex, 54, 36);
                }

                // Top face (y=0.5, facing +Y direction)
                if (!isSolid(X, Y + 1, Z)) {
                    renderFace(topTex, 72, 48);
                }

                // Bottom face (y=0, facing -Y direction)
                if (!isSolid(X, Y - 1, Z)) {
                    renderFace(bottomTex, 90, 60);
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
