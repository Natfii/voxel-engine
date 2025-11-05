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
Chunk::Chunk(int x, int y, int z) : m_x(x), m_y(y), m_z(z),
    m_vertexBuffer(VK_NULL_HANDLE), m_vertexBufferMemory(VK_NULL_HANDLE),
    m_indexBuffer(VK_NULL_HANDLE), m_indexBufferMemory(VK_NULL_HANDLE),
    m_vertexCount(0), m_indexCount(0), m_visible(false) {
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

// Helper struct for greedy meshing
struct MaskEntry {
    int blockID = 0;
    bool processed = false;
};

void Chunk::generate() {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(WIDTH * HEIGHT * DEPTH * 4 / 10);   // Reduced estimate due to greedy meshing
    indices.reserve(WIDTH * HEIGHT * DEPTH * 6 / 10);  // Reduced estimate due to greedy meshing

    // Get atlas grid size for UV calculations
    auto& registry = BlockRegistry::instance();
    int atlasGridSize = registry.getAtlasGridSize();
    float uvScale = (atlasGridSize > 0) ? (1.0f / atlasGridSize) : 1.0f;

    // Helper to check if a block is solid (non-air)
    auto isSolid = [this](int x, int y, int z) -> bool {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH)
            return false;
        return m_blocks[x][y][z] != 0;
    };

    // Helper to create a merged quad
    auto createQuad = [&](int x, int y, int z, int w, int h, int axis, bool positive, int blockID) {
        const BlockDefinition& def = registry.get(blockID);
        float cr, cg, cb;
        if (def.hasColor) {
            cr = def.color.r; cg = def.color.g; cb = def.color.b;
        } else {
            cr = cg = cb = 1.0f;
        }

        // Select appropriate texture based on axis/direction
        const BlockDefinition::FaceTexture* faceTexture;
        bool flipV = false;  // For side faces

        if (axis == 0) {  // X axis
            faceTexture = positive ? (def.useCubeMap ? &def.right : &def.all) : (def.useCubeMap ? &def.left : &def.all);
            flipV = true;
        } else if (axis == 1) {  // Y axis
            faceTexture = positive ? (def.useCubeMap ? &def.top : &def.all) : (def.useCubeMap ? &def.bottom : &def.all);
        } else {  // Z axis
            faceTexture = positive ? (def.useCubeMap ? &def.back : &def.all) : (def.useCubeMap ? &def.front : &def.all);
            flipV = true;
        }

        float uMin = faceTexture->atlasX * uvScale;
        float vMin = faceTexture->atlasY * uvScale;
        // Note: Texture variation disabled for merged quads for simplicity

        uint32_t baseIndex = static_cast<uint32_t>(verts.size());
        float blockSize = 0.5f;
        float bx = float(m_x * WIDTH + x) * blockSize;
        float by = float(m_y * HEIGHT + y) * blockSize;
        float bz = float(m_z * DEPTH + z) * blockSize;

        // Create 4 vertices for merged quad
        Vertex v[4];
        for (int i = 0; i < 4; i++) {
            v[i].r = cr; v[i].g = cg; v[i].b = cb;
        }

        if (axis == 0) {  // X axis (left/right faces)
            float xOffset = positive ? blockSize : 0.0f;
            v[0].x = bx + xOffset; v[0].y = by;                v[0].z = bz;
            v[1].x = bx + xOffset; v[1].y = by;                v[1].z = bz + h * blockSize;
            v[2].x = bx + xOffset; v[2].y = by + w * blockSize; v[2].z = bz + h * blockSize;
            v[3].x = bx + xOffset; v[3].y = by + w * blockSize; v[3].z = bz;
        } else if (axis == 1) {  // Y axis (top/bottom faces)
            float yOffset = positive ? blockSize : 0.0f;
            v[0].x = bx;                v[0].y = by + yOffset; v[0].z = bz;
            v[1].x = bx + w * blockSize; v[1].y = by + yOffset; v[1].z = bz;
            v[2].x = bx + w * blockSize; v[2].y = by + yOffset; v[2].z = bz + h * blockSize;
            v[3].x = bx;                v[3].y = by + yOffset; v[3].z = bz + h * blockSize;
        } else {  // Z axis (front/back faces)
            float zOffset = positive ? blockSize : 0.0f;
            v[0].x = bx;                v[0].y = by;                v[0].z = bz + zOffset;
            v[1].x = bx + w * blockSize; v[1].y = by;                v[1].z = bz + zOffset;
            v[2].x = bx + w * blockSize; v[2].y = by + h * blockSize; v[2].z = bz + zOffset;
            v[3].x = bx;                v[3].y = by + h * blockSize; v[3].z = bz + zOffset;
        }

        // UV coordinates - tile the texture across merged quad
        if (flipV) {
            v[0].u = uMin;             v[0].v = vMin + h * uvScale;
            v[1].u = uMin + w * uvScale; v[1].v = vMin + h * uvScale;
            v[2].u = uMin + w * uvScale; v[2].v = vMin;
            v[3].u = uMin;             v[3].v = vMin;
        } else {
            v[0].u = uMin;             v[0].v = vMin;
            v[1].u = uMin + w * uvScale; v[1].v = vMin;
            v[2].u = uMin + w * uvScale; v[2].v = vMin + h * uvScale;
            v[3].u = uMin;             v[3].v = vMin + h * uvScale;
        }

        for (int i = 0; i < 4; i++) verts.push_back(v[i]);

        // Create indices (winding order depends on direction)
        if ((axis == 0 && positive) || (axis == 1 && !positive) || (axis == 2 && !positive)) {
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 2);
            indices.push_back(baseIndex + 1);
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 3);
            indices.push_back(baseIndex + 2);
        } else {
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 1);
            indices.push_back(baseIndex + 2);
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 2);
            indices.push_back(baseIndex + 3);
        }
    };

    // Greedy meshing for each axis and direction
    for (int axis = 0; axis < 3; axis++) {
        for (int positive = 0; positive <= 1; positive++) {
            int u = (axis + 1) % 3;  // First tangent axis
            int v = (axis + 2) % 3;  // Second tangent axis

            int dim[3] = {WIDTH, HEIGHT, DEPTH};
            int sliceCount = dim[axis];
            int uSize = dim[u];
            int vSize = dim[v];

            std::vector<MaskEntry> mask(uSize * vSize);

            // Iterate through slices
            for (int slice = 0; slice < sliceCount; slice++) {
                // Build mask for this slice
                std::fill(mask.begin(), mask.end(), MaskEntry{});

                for (int iu = 0; iu < uSize; iu++) {
                    for (int iv = 0; iv < vSize; iv++) {
                        int pos[3];
                        pos[axis] = slice;
                        pos[u] = iu;
                        pos[v] = iv;

                        int x = pos[0], y = pos[1], z = pos[2];
                        int blockID = m_blocks[x][y][z];

                        if (blockID == 0) continue;  // Skip air

                        // Check if face is exposed
                        int nx = x, ny = y, nz = z;
                        if (axis == 0) nx += (positive ? 1 : -1);
                        else if (axis == 1) ny += (positive ? 1 : -1);
                        else nz += (positive ? 1 : -1);

                        if (!isSolid(nx, ny, nz)) {
                            mask[iu + iv * uSize].blockID = blockID;
                        }
                    }
                }

                // Greedy meshing on the mask
                for (int iu = 0; iu < uSize; iu++) {
                    for (int iv = 0; iv < vSize; iv++) {
                        int idx = iu + iv * uSize;
                        if (mask[idx].blockID == 0 || mask[idx].processed) continue;

                        int blockID = mask[idx].blockID;

                        // Expand horizontally (along u)
                        int w;
                        for (w = 1; iu + w < uSize; w++) {
                            int checkIdx = (iu + w) + iv * uSize;
                            if (mask[checkIdx].blockID != blockID || mask[checkIdx].processed)
                                break;
                        }

                        // Expand vertically (along v)
                        int h;
                        bool canExpand = true;
                        for (h = 1; iv + h < vSize && canExpand; h++) {
                            for (int du = 0; du < w; du++) {
                                int checkIdx = (iu + du) + (iv + h) * uSize;
                                if (mask[checkIdx].blockID != blockID || mask[checkIdx].processed) {
                                    canExpand = false;
                                    break;
                                }
                            }
                        }

                        // Create merged quad
                        int pos[3];
                        pos[axis] = slice;
                        pos[u] = iu;
                        pos[v] = iv;
                        createQuad(pos[0], pos[1], pos[2], w, h, axis, positive != 0, blockID);

                        // Mark as processed
                        for (int dv = 0; dv < h; dv++) {
                            for (int du = 0; du < w; du++) {
                                mask[(iu + du) + (iv + dv) * uSize].processed = true;
                            }
                        }
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

void Chunk::generateMesh(World* world) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    verts.reserve(WIDTH * HEIGHT * DEPTH * 4 / 10);   // Reduced estimate due to greedy meshing
    indices.reserve(WIDTH * HEIGHT * DEPTH * 6 / 10);  // Reduced estimate due to greedy meshing

    // Get atlas grid size for UV calculations
    auto& registry = BlockRegistry::instance();
    int atlasGridSize = registry.getAtlasGridSize();
    float uvScale = (atlasGridSize > 0) ? (1.0f / atlasGridSize) : 1.0f;

    // Helper to check if a block is solid (non-air) - checks neighboring chunks via World
    auto isSolid = [this, world](int x, int y, int z) -> bool {
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            return m_blocks[x][y][z] != 0;
        }
        // Out of bounds - check neighboring chunk
        int worldBlockX = m_x * WIDTH + x;
        int worldBlockY = m_y * HEIGHT + y;
        int worldBlockZ = m_z * DEPTH + z;
        float worldX = worldBlockX * 0.5f;
        float worldY = worldBlockY * 0.5f;
        float worldZ = worldBlockZ * 0.5f;
        int blockID = world->getBlockAt(worldX, worldY, worldZ);
        return blockID != 0;
    };

    // Helper to create a merged quad (same as generate() but with world-aware culling)
    auto createQuad = [&](int x, int y, int z, int w, int h, int axis, bool positive, int blockID) {
        const BlockDefinition& def = registry.get(blockID);
        float cr, cg, cb;
        if (def.hasColor) {
            cr = def.color.r; cg = def.color.g; cb = def.color.b;
        } else {
            cr = cg = cb = 1.0f;
        }

        const BlockDefinition::FaceTexture* faceTexture;
        bool flipV = false;

        if (axis == 0) {
            faceTexture = positive ? (def.useCubeMap ? &def.right : &def.all) : (def.useCubeMap ? &def.left : &def.all);
            flipV = true;
        } else if (axis == 1) {
            faceTexture = positive ? (def.useCubeMap ? &def.top : &def.all) : (def.useCubeMap ? &def.bottom : &def.all);
        } else {
            faceTexture = positive ? (def.useCubeMap ? &def.back : &def.all) : (def.useCubeMap ? &def.front : &def.all);
            flipV = true;
        }

        float uMin = faceTexture->atlasX * uvScale;
        float vMin = faceTexture->atlasY * uvScale;

        uint32_t baseIndex = static_cast<uint32_t>(verts.size());
        float blockSize = 0.5f;
        float bx = float(m_x * WIDTH + x) * blockSize;
        float by = float(m_y * HEIGHT + y) * blockSize;
        float bz = float(m_z * DEPTH + z) * blockSize;

        Vertex v[4];
        for (int i = 0; i < 4; i++) {
            v[i].r = cr; v[i].g = cg; v[i].b = cb;
        }

        if (axis == 0) {
            float xOffset = positive ? blockSize : 0.0f;
            v[0].x = bx + xOffset; v[0].y = by;                v[0].z = bz;
            v[1].x = bx + xOffset; v[1].y = by;                v[1].z = bz + h * blockSize;
            v[2].x = bx + xOffset; v[2].y = by + w * blockSize; v[2].z = bz + h * blockSize;
            v[3].x = bx + xOffset; v[3].y = by + w * blockSize; v[3].z = bz;
        } else if (axis == 1) {
            float yOffset = positive ? blockSize : 0.0f;
            v[0].x = bx;                v[0].y = by + yOffset; v[0].z = bz;
            v[1].x = bx + w * blockSize; v[1].y = by + yOffset; v[1].z = bz;
            v[2].x = bx + w * blockSize; v[2].y = by + yOffset; v[2].z = bz + h * blockSize;
            v[3].x = bx;                v[3].y = by + yOffset; v[3].z = bz + h * blockSize;
        } else {
            float zOffset = positive ? blockSize : 0.0f;
            v[0].x = bx;                v[0].y = by;                v[0].z = bz + zOffset;
            v[1].x = bx + w * blockSize; v[1].y = by;                v[1].z = bz + zOffset;
            v[2].x = bx + w * blockSize; v[2].y = by + h * blockSize; v[2].z = bz + zOffset;
            v[3].x = bx;                v[3].y = by + h * blockSize; v[3].z = bz + zOffset;
        }

        if (flipV) {
            v[0].u = uMin;             v[0].v = vMin + h * uvScale;
            v[1].u = uMin + w * uvScale; v[1].v = vMin + h * uvScale;
            v[2].u = uMin + w * uvScale; v[2].v = vMin;
            v[3].u = uMin;             v[3].v = vMin;
        } else {
            v[0].u = uMin;             v[0].v = vMin;
            v[1].u = uMin + w * uvScale; v[1].v = vMin;
            v[2].u = uMin + w * uvScale; v[2].v = vMin + h * uvScale;
            v[3].u = uMin;             v[3].v = vMin + h * uvScale;
        }

        for (int i = 0; i < 4; i++) verts.push_back(v[i]);

        if ((axis == 0 && positive) || (axis == 1 && !positive) || (axis == 2 && !positive)) {
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 2);
            indices.push_back(baseIndex + 1);
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 3);
            indices.push_back(baseIndex + 2);
        } else {
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 1);
            indices.push_back(baseIndex + 2);
            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 2);
            indices.push_back(baseIndex + 3);
        }
    };

    // Greedy meshing for each axis and direction (same as generate())
    for (int axis = 0; axis < 3; axis++) {
        for (int positive = 0; positive <= 1; positive++) {
            int u = (axis + 1) % 3;
            int v = (axis + 2) % 3;

            int dim[3] = {WIDTH, HEIGHT, DEPTH};
            int sliceCount = dim[axis];
            int uSize = dim[u];
            int vSize = dim[v];

            std::vector<MaskEntry> mask(uSize * vSize);

            for (int slice = 0; slice < sliceCount; slice++) {
                std::fill(mask.begin(), mask.end(), MaskEntry{});

                for (int iu = 0; iu < uSize; iu++) {
                    for (int iv = 0; iv < vSize; iv++) {
                        int pos[3];
                        pos[axis] = slice;
                        pos[u] = iu;
                        pos[v] = iv;

                        int x = pos[0], y = pos[1], z = pos[2];
                        int blockID = m_blocks[x][y][z];

                        if (blockID == 0) continue;

                        int nx = x, ny = y, nz = z;
                        if (axis == 0) nx += (positive ? 1 : -1);
                        else if (axis == 1) ny += (positive ? 1 : -1);
                        else nz += (positive ? 1 : -1);

                        if (!isSolid(nx, ny, nz)) {
                            mask[iu + iv * uSize].blockID = blockID;
                        }
                    }
                }

                for (int iu = 0; iu < uSize; iu++) {
                    for (int iv = 0; iv < vSize; iv++) {
                        int idx = iu + iv * uSize;
                        if (mask[idx].blockID == 0 || mask[idx].processed) continue;

                        int blockID = mask[idx].blockID;

                        int w;
                        for (w = 1; iu + w < uSize; w++) {
                            int checkIdx = (iu + w) + iv * uSize;
                            if (mask[checkIdx].blockID != blockID || mask[checkIdx].processed)
                                break;
                        }

                        int h;
                        bool canExpand = true;
                        for (h = 1; iv + h < vSize && canExpand; h++) {
                            for (int du = 0; du < w; du++) {
                                int checkIdx = (iu + du) + (iv + h) * uSize;
                                if (mask[checkIdx].blockID != blockID || mask[checkIdx].processed) {
                                    canExpand = false;
                                    break;
                                }
                            }
                        }

                        int pos[3];
                        pos[axis] = slice;
                        pos[u] = iu;
                        pos[v] = iv;
                        createQuad(pos[0], pos[1], pos[2], w, h, axis, positive != 0, blockID);

                        for (int dv = 0; dv < h; dv++) {
                            for (int du = 0; du < w; du++) {
                                mask[(iu + du) + (iv + dv) * uSize].processed = true;
                            }
                        }
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
    // Destroy old buffers if they exist (prevents memory leak when recreating buffers)
    destroyBuffers(renderer);

    if (m_vertices.empty()) return;

    // Create vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * m_vertices.size();

    // Create staging buffer for vertices
    VkBuffer vertexStagingBuffer;
    VkDeviceMemory vertexStagingBufferMemory;
    renderer->createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vertexStagingBuffer, vertexStagingBufferMemory);

    // Copy vertex data to staging buffer
    void* data;
    vkMapMemory(renderer->getDevice(), vertexStagingBufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, m_vertices.data(), (size_t)vertexBufferSize);
    vkUnmapMemory(renderer->getDevice(), vertexStagingBufferMemory);

    // Create vertex buffer on device
    renderer->createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          m_vertexBuffer, m_vertexBufferMemory);

    // Copy from staging to device
    renderer->copyBuffer(vertexStagingBuffer, m_vertexBuffer, vertexBufferSize);

    // Cleanup vertex staging buffer
    vkDestroyBuffer(renderer->getDevice(), vertexStagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), vertexStagingBufferMemory, nullptr);

    // Create index buffer (if we have indices)
    if (!m_indices.empty()) {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_indices.size();

        // Create staging buffer for indices
        VkBuffer indexStagingBuffer;
        VkDeviceMemory indexStagingBufferMemory;
        renderer->createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              indexStagingBuffer, indexStagingBufferMemory);

        // Copy index data to staging buffer
        vkMapMemory(renderer->getDevice(), indexStagingBufferMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, m_indices.data(), (size_t)indexBufferSize);
        vkUnmapMemory(renderer->getDevice(), indexStagingBufferMemory);

        // Create index buffer on device
        renderer->createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              m_indexBuffer, m_indexBufferMemory);

        // Copy from staging to device
        renderer->copyBuffer(indexStagingBuffer, m_indexBuffer, indexBufferSize);

        // Cleanup index staging buffer
        vkDestroyBuffer(renderer->getDevice(), indexStagingBuffer, nullptr);
        vkFreeMemory(renderer->getDevice(), indexStagingBufferMemory, nullptr);
    }
}

void Chunk::destroyBuffers(VulkanRenderer* renderer) {
    // Destroy vertex buffer
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(renderer->getDevice(), m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(renderer->getDevice(), m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }

    // Destroy index buffer
    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(renderer->getDevice(), m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }
    if (m_indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(renderer->getDevice(), m_indexBufferMemory, nullptr);
        m_indexBufferMemory = VK_NULL_HANDLE;
    }
}

void Chunk::render(VkCommandBuffer commandBuffer) {
    if (m_vertexCount == 0 || m_vertexBuffer == VK_NULL_HANDLE) return;

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // Use indexed rendering if we have an index buffer
    if (m_indexCount > 0 && m_indexBuffer != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
    } else {
        // Fallback to non-indexed rendering (shouldn't happen with current implementation)
        vkCmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
    }
}
