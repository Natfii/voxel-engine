/**
 * @file chunk.cpp
 * @brief Implementation of chunk terrain generation and mesh optimization
 *
 * This file contains the core algorithms for:
 * - Procedural terrain generation using FastNoiseLite
 * - Optimized mesh generation with face culling
 * - Vulkan buffer management
 *
 */

#include "chunk.h"
#include "world.h"
#include "vulkan_renderer.h"
#include "block_system.h"
#include "terrain_constants.h"
#include "mesh_buffer_pool.h"
#include "logger.h"
#include "debug_state.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <cmath>

// Static member initialization
std::unique_ptr<FastNoiseLite> Chunk::s_noise = nullptr;
// Note: s_noiseMutex removed - FastNoiseLite is thread-safe for reads

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
      m_transparentVertexBuffer(VK_NULL_HANDLE),
      m_transparentVertexBufferMemory(VK_NULL_HANDLE),
      m_transparentIndexBuffer(VK_NULL_HANDLE),
      m_transparentIndexBufferMemory(VK_NULL_HANDLE),
      m_transparentVertexCount(0),
      m_transparentIndexCount(0),
      m_vertexStagingBuffer(VK_NULL_HANDLE),
      m_vertexStagingBufferMemory(VK_NULL_HANDLE),
      m_indexStagingBuffer(VK_NULL_HANDLE),
      m_indexStagingBufferMemory(VK_NULL_HANDLE),
      m_transparentVertexStagingBuffer(VK_NULL_HANDLE),
      m_transparentVertexStagingBufferMemory(VK_NULL_HANDLE),
      m_transparentIndexStagingBuffer(VK_NULL_HANDLE),
      m_transparentIndexStagingBufferMemory(VK_NULL_HANDLE),
      m_visible(false),
      m_lightingDirty(false) {

    // Initialize all blocks to air, metadata to 0, and lighting to darkness
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                m_blocks[i][j][k] = 0;
                m_blockMetadata[i][j][k] = 0;
            }
        }
    }

    // Initialize all light data to 0 (complete darkness)
    m_lightData.fill(BlockLight(0, 0));

    // Calculate world-space bounds for culling
    // Blocks are 1.0 world units in size
    float worldX = m_x * WIDTH;
    float worldY = m_y * HEIGHT;
    float worldZ = m_z * DEPTH;

    m_minBounds = glm::vec3(worldX, worldY, worldZ);
    m_maxBounds = glm::vec3(
        worldX + WIDTH,
        worldY + HEIGHT,
        worldZ + DEPTH
    );
}

Chunk::~Chunk() {
    // Note: Vulkan buffers must be destroyed via destroyBuffers() before destruction
}

/**
 * @brief Resets chunk for reuse from pool
 *
 * CHUNK POOLING OPTIMIZATION:
 * Instead of allocating/deallocating chunks (expensive), we reuse them from a pool.
 * This method clears the chunk state without freeing memory, making it ~100x faster
 * than new/delete for chunk creation.
 */
void Chunk::reset(int x, int y, int z) {
    m_x = x;
    m_y = y;
    m_z = z;

    // Clear all blocks to air and metadata to 0
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                m_blocks[i][j][k] = 0;
                m_blockMetadata[i][j][k] = 0;
            }
        }
    }

    // Reset lighting to darkness
    m_lightData.fill(BlockLight(0, 0));
    m_lightingDirty = false;

    // Recalculate bounds
    float worldX = m_x * WIDTH;
    float worldY = m_y * HEIGHT;
    float worldZ = m_z * DEPTH;

    m_minBounds = glm::vec3(worldX, worldY, worldZ);
    m_maxBounds = glm::vec3(worldX + WIDTH, worldY + HEIGHT, worldZ + DEPTH);

    // Clear mesh data (but don't deallocate vectors - they'll be reused)
    m_vertices.clear();
    m_indices.clear();
    m_transparentVertices.clear();
    m_transparentIndices.clear();

    // Reset counters
    m_vertexCount = 0;
    m_indexCount = 0;
    m_transparentVertexCount = 0;
    m_transparentIndexCount = 0;

    // Reset visibility
    m_visible = false;
}

/**
 * @brief Checks if chunk is completely empty (all air blocks)
 *
 * EMPTY CHUNK CULLING:
 * Used to skip saving/processing chunks with no terrain. Saves disk space
 * and memory for sky chunks.
 */
bool Chunk::isEmpty() const {
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                if (m_blocks[i][j][k] != 0) {
                    return false;
                }
            }
        }
    }
    return true;
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
    using namespace TerrainGeneration;

    if (s_noise == nullptr) {
        return BASE_HEIGHT; // Fallback to flat terrain if noise not initialized
    }

    // FastNoiseLite is thread-safe for reads - no mutex needed
    // This was causing serialization during parallel chunk generation
    float noise = s_noise->GetNoise(worldX, worldZ);

    // Map noise to height range [BASE_HEIGHT - HEIGHT_VARIATION, BASE_HEIGHT + HEIGHT_VARIATION]
    int height = BASE_HEIGHT + (int)(noise * HEIGHT_VARIATION);

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
void Chunk::generate(BiomeMap* biomeMap) {
    using namespace TerrainGeneration;

    if (!biomeMap) {
        std::cerr << "ERROR: BiomeMap is null in Chunk::generate()" << std::endl;
        return;
    }

    for (int x = 0; x < WIDTH; x++) {
        for (int z = 0; z < DEPTH; z++) {
            // Convert local coords to world coords (blocks are 1.0 units)
            // Cast to int64_t before multiplication to prevent overflow with large chunk coords
            float worldX = static_cast<float>(static_cast<int64_t>(m_x) * WIDTH + x);
            float worldZ = static_cast<float>(static_cast<int64_t>(m_z) * DEPTH + z);

            // Get biome at this position
            const Biome* biome = biomeMap->getBiomeAt(worldX, worldZ);
            if (!biome) {
                // Fallback: Fill with stone underground, air above
                int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);
                for (int y = 0; y < HEIGHT; y++) {
                    int worldY = static_cast<int64_t>(m_y) * HEIGHT + y;
                    if (worldY < terrainHeight) {
                        m_blocks[x][y][z] = BLOCK_STONE;
                    } else {
                        m_blocks[x][y][z] = BLOCK_AIR;
                    }
                }
                continue;
            }

            // Get terrain height from biome map
            int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);

            // CRITICAL FIX: Clamp terrain height to chunk's maximum possible Y coordinate
            // If chunk Y=2 (blocks 64-95) and terrainHeight=120, the entire chunk would be solid
            // because all worldY values (64-95) are < 120, leaving no surface to spawn on
            int maxWorldY = static_cast<int>(static_cast<int64_t>(m_y) * HEIGHT + HEIGHT - 1);
            if (terrainHeight > maxWorldY + 10) {
                // Terrain is way above this chunk - fill as underground
                terrainHeight = maxWorldY + 10;
            }

            // Check if this is an ocean area (hardcoded biome - not YAML)
            // Ocean = terrain significantly below water level
            bool isOcean = (terrainHeight < WATER_LEVEL - 8);  // 8+ blocks below water = ocean

            // Fill column
            for (int y = 0; y < HEIGHT; y++) {
                int worldY = static_cast<int64_t>(m_y) * HEIGHT + y;
                float worldYf = static_cast<float>(worldY);

                // SOLID STONE GUARANTEE: Y=2 to Y=10 is always stone (no caves)
                // This prevents giant holes to the void and ensures solid foundation
                if (worldY >= 2 && worldY <= 10) {
                    m_blocks[x][y][z] = BLOCK_STONE;
                    continue;
                }

                // Check if this is inside a cave (extended to deep underground)
                float caveDensity = biomeMap->getCaveDensityAt(worldX, worldYf, worldZ);
                bool isCave = (caveDensity < 0.45f) && (worldY > 10);  // Caves above solid foundation layer

                // Check if inside underground biome chamber (extended to deep underground)
                bool isUndergroundChamber = biomeMap->isUndergroundBiomeAt(worldX, worldYf, worldZ) && (worldY > 10);

                // Determine block placement
                if (worldY < terrainHeight) {
                    // Below surface

                    // OCEAN BIOME LOGIC (hardcoded, not YAML)
                    if (isOcean) {
                        // Ocean floor - no caves in ocean areas for clean underwater terrain
                        int depthFromSurface = terrainHeight - worldY;

                        if (depthFromSurface <= 3) {
                            // Ocean floor top layers - sand
                            m_blocks[x][y][z] = BLOCK_SAND;
                        } else {
                            // Deep ocean floor - stone
                            m_blocks[x][y][z] = BLOCK_STONE;
                        }
                        continue;
                    }

                    // LAND BIOME LOGIC
                    // If in cave, create air pocket
                    // Cave generation handles surface entrances intelligently via noise
                    // Only prevent caves in the very top layer (3 blocks) to avoid ugly surface pockmarks
                    if (isCave && worldY < terrainHeight - 3) {
                        m_blocks[x][y][z] = BLOCK_AIR;
                        continue;
                    }

                    // If in underground chamber, create large open space (only above Y=15)
                    if (isUndergroundChamber) {
                        m_blocks[x][y][z] = BLOCK_AIR;
                        continue;
                    }

                    // Solid terrain - determine block type
                    int depthFromSurface = terrainHeight - worldY;

                    if (depthFromSurface == 1) {
                        // Top layer - use biome's surface block
                        m_blocks[x][y][z] = biome->primary_surface_block;
                    } else if (depthFromSurface <= TOPSOIL_DEPTH) {
                        // Topsoil layer - dirt
                        m_blocks[x][y][z] = BLOCK_DIRT;
                    } else {
                        // Deep underground - use biome's stone block
                        m_blocks[x][y][z] = biome->primary_stone_block;
                    }

                } else if (worldY < WATER_LEVEL) {
                    // Above terrain but below water level
                    // Use ice in cold biomes (temperature < 25), water otherwise
                    if (biome->temperature < 25) {
                        m_blocks[x][y][z] = BLOCK_ICE;
                        m_blockMetadata[x][y][z] = 0;
                    } else {
                        m_blocks[x][y][z] = BLOCK_WATER;
                        m_blockMetadata[x][y][z] = 0;  // Source block
                    }
                } else {
                    // Above water level
                    m_blocks[x][y][z] = BLOCK_AIR;
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
 * - World coords: local * 1.0 (blocks are 1.0 world units)
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
void Chunk::generateMesh(World* world, bool callerHoldsLock) {
    // OCCLUSION CULLING: Skip mesh generation for fully-occluded chunks
    // Underground chunks surrounded by solid stone don't need any geometry!
    // This saves ~40% of mesh generation work for typical terrain
    if (isFullyOccluded(world, callerHoldsLock)) {
        // Clear any existing mesh data
        m_vertices.clear();
        m_indices.clear();
        m_transparentVertices.clear();
        m_transparentIndices.clear();
        m_vertexCount = 0;
        m_indexCount = 0;
        m_transparentVertexCount = 0;
        m_transparentIndexCount = 0;
        Logger::debug() << "Skipped mesh generation for fully-occluded chunk (" << m_x << ", " << m_y << ", " << m_z << ")";
        return;
    }

    /**
     * Cube Vertex Layout (indexed rendering):
     * ========================================
     *
     * Each face is a quad defined by 4 vertices (counter-clockwise winding order).
     * Blocks are 1.0 world units in size (1 block = 1.0 units).
     *
     * Vertex ordering per face (looking at face from outside):
     *   3 -------- 2
     *   |          |
     *   |          |
     *   0 -------- 1
     *
     * Index formula: Two triangles per quad
     *   Triangle 1: (0, 1, 2) - bottom-left, bottom-right, top-right
     *   Triangle 2: (0, 2, 3) - bottom-left, top-right, top-left
     *
     * Face storage order (offsets into cube array):
     *   0-11:  Front face (z=0, facing -Z)
     *   12-23: Back face (z=1.0, facing +Z)
     *   24-35: Left face (x=0, facing -X)
     *   36-47: Right face (x=1.0, facing +X)
     *   48-59: Top face (y=1.0, facing +Y)
     *   60-71: Bottom face (y=0, facing -Y)
     *
     * Each face offset contains 12 floats (4 vertices × 3 components).
     */
    static constexpr std::array<float, 72> cube = {{
        // Front face (z = 0, facing -Z) - vertices: BL, BR, TR, TL
        0,0,0,  1.0f,0,0,  1.0f,1.0f,0,  0,1.0f,0,
        // Back face (z = 1.0, facing +Z) - vertices: BR, BL, TL, TR (reversed for correct winding)
        1.0f,0,1.0f,  0,0,1.0f,  0,1.0f,1.0f,  1.0f,1.0f,1.0f,
        // Left face (x = 0, facing -X) - vertices: BL, BR, TR, TL
        0,0,1.0f,  0,0,0,  0,1.0f,0,  0,1.0f,1.0f,
        // Right face (x = 1.0, facing +X) - vertices: BL, BR, TR, TL
        1.0f,0,0,  1.0f,0,1.0f,  1.0f,1.0f,1.0f,  1.0f,1.0f,0,
        // Top face (y = 1.0, facing +Y) - vertices: BL, BR, TR, TL
        0,1.0f,0,  1.0f,1.0f,0,  1.0f,1.0f,1.0f,  0,1.0f,1.0f,
        // Bottom face (y = 0, facing -Y) - vertices: BL, BR, TR, TL
        0,0,1.0f,  1.0f,0,1.0f,  1.0f,0,0,  0,0,0
    }};

    /**
     * UV Coordinate Layout:
     * =====================
     *
     * UV coordinates map quad corners to texture atlas cells.
     * Standard UV space: (0,0) = top-left, (1,1) = bottom-right
     *
     * V-flip for side faces: Side faces need V-flip to prevent upside-down textures
     * because Vulkan's texture coordinate system differs from OpenGL.
     *
     * Top/Bottom faces: Use standard UV orientation
     * Side faces: Use V-flipped orientation (V: 0→1 instead of 1→0)
     */
    static constexpr std::array<float, 48> cubeUVs = {{
        // Front face (4 UV pairs) - V flipped: BL(0,1), BR(1,1), TR(1,0), TL(0,0)
        0,1,  1,1,  1,0,  0,0,
        // Back face - V flipped
        0,1,  1,1,  1,0,  0,0,
        // Left face - V flipped
        0,1,  1,1,  1,0,  0,0,
        // Right face - V flipped
        0,1,  1,1,  1,0,  0,0,
        // Top face - Standard orientation: BL(0,0), BR(1,0), TR(1,1), TL(0,1)
        0,0,  1,0,  1,1,  0,1,
        // Bottom face - Standard orientation
        0,0,  1,0,  1,1,  0,1
    }};

    // OPTIMIZATION: Use mesh buffer pool to reuse allocated memory (40-60% speedup)
    // Get thread-local pool for thread-safe access during parallel generation
    auto& pool = getThreadLocalMeshPool();

    // Release old mesh data back to pool before acquiring new buffers
    if (!m_vertices.empty()) {
        pool.releaseVertexBuffer(std::move(m_vertices));
    }
    if (!m_indices.empty()) {
        pool.releaseIndexBuffer(std::move(m_indices));
    }
    if (!m_transparentVertices.empty()) {
        pool.releaseVertexBuffer(std::move(m_transparentVertices));
    }
    if (!m_transparentIndices.empty()) {
        pool.releaseIndexBuffer(std::move(m_transparentIndices));
    }

    // Acquire buffers from pool (reuses allocated memory)
    std::vector<Vertex> verts = pool.acquireVertexBuffer();
    std::vector<uint32_t> indices = pool.acquireIndexBuffer();
    std::vector<Vertex> transparentVerts = pool.acquireVertexBuffer();
    std::vector<uint32_t> transparentIndices = pool.acquireIndexBuffer();

    // Reserve space for estimated visible faces (roughly 30% of blocks visible, 3 faces each on average)
    // With indexed rendering: 4 vertices per face instead of 6
    verts.reserve(WIDTH * HEIGHT * DEPTH * 12 / 10);  // 4 vertices per face
    indices.reserve(WIDTH * HEIGHT * DEPTH * 18 / 10); // 6 indices per face (same as before)
    transparentVerts.reserve(WIDTH * HEIGHT * DEPTH * 6 / 10);  // Less transparent blocks typically
    transparentIndices.reserve(WIDTH * HEIGHT * DEPTH * 9 / 10);

    // Get block registry (needed for liquid checks)
    auto& registry = BlockRegistry::instance();
    int atlasGridSize = registry.getAtlasGridSize();
    float uvScale = (atlasGridSize > 0) ? (1.0f / atlasGridSize) : 1.0f;

    // Helper: Convert local chunk coordinates to world position (eliminates code duplication)
    auto localToWorldPos = [this](int x, int y, int z) -> glm::vec3 {
        int worldBlockX = m_x * WIDTH + x;
        int worldBlockY = m_y * HEIGHT + y;
        int worldBlockZ = m_z * DEPTH + z;
        return glm::vec3(static_cast<float>(worldBlockX), static_cast<float>(worldBlockY), static_cast<float>(worldBlockZ));
    };

    // Helper lambda to check if a block is solid (non-air)
    // THIS VERSION CHECKS NEIGHBORING CHUNKS via World
    auto isSolid = [this, world, &registry, &localToWorldPos, callerHoldsLock](int x, int y, int z) -> bool {
        int blockID;
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            // Inside this chunk
            blockID = m_blocks[x][y][z];
        } else {
            // Out of bounds - check neighboring chunk via World
            glm::vec3 worldPos = localToWorldPos(x, y, z);
            // Use unsafe version if caller already holds lock (prevents deadlock)
            blockID = callerHoldsLock ? world->getBlockAtUnsafe(worldPos.x, worldPos.y, worldPos.z)
                                       : world->getBlockAt(worldPos.x, worldPos.y, worldPos.z);
        }
        if (blockID == 0) return false;
        // Bounds check before registry access to prevent crash
        if (blockID < 0 || blockID >= registry.count()) return false;
        return !registry.get(blockID).isLiquid;  // Solid = not air and not liquid
    };

    // Helper lambda to check if a block is liquid
    auto isLiquid = [this, world, &registry, &localToWorldPos, callerHoldsLock](int x, int y, int z) -> bool {
        int blockID;
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            blockID = m_blocks[x][y][z];
        } else {
            // Out of bounds - check world
            glm::vec3 worldPos = localToWorldPos(x, y, z);
            // Use unsafe version if caller already holds lock (prevents deadlock)
            blockID = callerHoldsLock ? world->getBlockAtUnsafe(worldPos.x, worldPos.y, worldPos.z)
                                       : world->getBlockAt(worldPos.x, worldPos.y, worldPos.z);
        }
        if (blockID == 0) return false;
        // Bounds check before registry access to prevent crash
        if (blockID < 0 || blockID >= registry.count()) return false;
        return registry.get(blockID).isLiquid;
    };

    // SMOOTH LIGHTING: Helper to get light at a vertex by sampling 4 adjacent blocks
    // This creates smooth gradients between different light levels (Minecraft-style)
    // Uses WORLD-SPACE vertex position to ensure consistent lighting across block boundaries
    // Uses INTERPOLATED lighting values for smooth time-based transitions
    auto getSmoothLight = [this, world, callerHoldsLock](
        float vx, float vy, float vz, int dx1, int dy1, int dz1, int dx2, int dy2, int dz2, bool isSky) -> float {

        // Convert vertex world position to block coordinates
        int blockX = static_cast<int>(std::floor(vx));
        int blockY = static_cast<int>(std::floor(vy));
        int blockZ = static_cast<int>(std::floor(vz));

        // Sample 4 blocks around this vertex in world space
        float light1, light2, light3, light4;

        auto getLightAtWorldPos = [&](int worldX, int worldY, int worldZ) -> float {
            if (callerHoldsLock) {
                // Can't safely query other chunks while holding lock
                // Convert to chunk-local coordinates
                int localX = worldX - (m_chunkX * WIDTH);
                int localY = worldY - (m_chunkY * HEIGHT);
                int localZ = worldZ - (m_chunkZ * DEPTH);
                if (localX >= 0 && localX < WIDTH && localY >= 0 && localY < HEIGHT && localZ >= 0 && localZ < DEPTH) {
                    return isSky ? getInterpolatedSkyLight(localX, localY, localZ) : getInterpolatedBlockLight(localX, localY, localZ);
                }
                return 0.0f; // Fallback for out-of-chunk
            }

            Chunk* chunk = world->getChunkAtWorldPos(worldX, worldY, worldZ);
            if (!chunk) return 0.0f;

            int localX = worldX - (chunk->getChunkX() * WIDTH);
            int localY = worldY - (chunk->getChunkY() * HEIGHT);
            int localZ = worldZ - (chunk->getChunkZ() * DEPTH);
            return isSky ? chunk->getInterpolatedSkyLight(localX, localY, localZ) : chunk->getInterpolatedBlockLight(localX, localY, localZ);
        };

        // Sample 4 blocks around the vertex position
        light1 = getLightAtWorldPos(blockX + dx1 + dx2, blockY + dy1 + dy2, blockZ + dz1 + dz2);  // Diagonal
        light2 = getLightAtWorldPos(blockX + dx1, blockY + dy1, blockZ + dz1);                     // Side 1
        light3 = getLightAtWorldPos(blockX + dx2, blockY + dy2, blockZ + dz2);                     // Side 2
        light4 = getLightAtWorldPos(blockX, blockY, blockZ);                                       // Center

        // Average the 4 samples and normalize to 0.0-1.0
        return (light1 + light2 + light3 + light4) / 4.0f / 15.0f;
    };

    // AMBIENT OCCLUSION: Darken vertices where 3 blocks meet (corners)
    // Creates depth perception and more realistic shadows (Minecraft-style)
    auto calculateAO = [&isSolid](int x, int y, int z, int dx1, int dy1, int dz1, int dx2, int dy2, int dz2) -> float {
        // Check 3 blocks adjacent to this vertex
        bool side1 = isSolid(x + dx1, y + dy1, z + dz1);
        bool side2 = isSolid(x + dx2, y + dy2, z + dz2);
        bool corner = isSolid(x + dx1 + dx2, y + dy1 + dy2, z + dz1 + dz2);

        // If both sides are blocked, full occlusion
        if (side1 && side2) {
            return 0.0f;
        }

        // Calculate AO based on number of adjacent solid blocks
        // 3 = no occlusion, 0 = full occlusion
        int blockCount = (side1 ? 1 : 0) + (side2 ? 1 : 0) + (corner ? 1 : 0);
        return (3 - blockCount) / 3.0f;
    };

    // Iterate over every block in the chunk (optimized order for cache locality)
    for(int X = 0; X < WIDTH;  ++X) {
        for(int Y = 0; Y < HEIGHT; ++Y) {
            for(int Z = 0; Z < DEPTH;  ++Z) {
                int id = m_blocks[X][Y][Z];
                if (id == 0) continue; // Skip air

                // Bounds check before registry access to prevent crash
                if (id < 0 || id >= registry.count()) {
                    continue; // Skip invalid block IDs
                }

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
                        // Cast to unsigned before multiplication to avoid signed integer overflow (UB)
                        unsigned int seed = (static_cast<unsigned int>(worldX) * 73856093U) ^
                                          (static_cast<unsigned int>(worldY) * 19349663U) ^
                                          (static_cast<unsigned int>(worldZ) * 83492791U);
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
                float bx = float(m_x * WIDTH + X);
                float by = float(m_y * HEIGHT + Y);
                float bz = float(m_z * DEPTH + Z);

                // Water level height adjustment (Minecraft-style flowing water)
                // Level 0 = source (full height), Level 7 = edge (very low)
                float waterHeightAdjust = 0.0f;
                if (def.isLiquid) {
                    uint8_t waterLevel = m_blockMetadata[X][Y][Z];
                    // Each level reduces height by 1/8th of a block (0.125 world units)
                    waterHeightAdjust = -waterLevel * (1.0f / 8.0f);
                }

                // Helper to render a face with the appropriate texture (indexed rendering)
                // heightAdjust: Optional Y-offset for water level rendering
                // adjustTopOnly: If true, only apply heightAdjust to vertices with y=0.5 (top of block)
                // useTransparent: If true, adds to transparent buffers; if false, adds to opaque buffers
                // faceNormal: Direction the face is facing (for smooth lighting calculation)
                auto renderFace = [&](const BlockDefinition::FaceTexture& faceTexture, int cubeStart, int uvStart,
                                      float heightAdjust = 0.0f, bool adjustTopOnly = false, bool useTransparent = false,
                                      glm::ivec3 faceNormal = glm::ivec3(0, 0, 0)) {
                    auto [uMin, vMin] = getUVsForFace(faceTexture);
                    float uvScaleZoomed = uvScale;

                    // For animated textures, scale UVs to cover the full animation area
                    // e.g., if animatedTiles=2, UVs should span 2x2 cells instead of 1 cell
                    if (def.animatedTiles > 1) {
                        uvScaleZoomed = uvScale * def.animatedTiles;
                    }
                    // Recalculate uvScaleZoomed if texture variation is enabled (per-face)
                    else if (faceTexture.variation > 1.0f) {
                        uvScaleZoomed = uvScale / faceTexture.variation;
                    }

                    // Choose which vectors to use based on transparency
                    auto& targetVerts = useTransparent ? transparentVerts : verts;
                    auto& targetIndices = useTransparent ? transparentIndices : indices;

                    // Get the base index for these vertices
                    uint32_t baseIndex = static_cast<uint32_t>(targetVerts.size());

                    // Create 4 vertices for this face (corners of the quad)
                    for (int i = cubeStart, uv = uvStart; i < cubeStart + 12; i += 3, uv += 2) {
                        Vertex v;
                        v.x = cube[i+0] + bx;
                        float yPos = cube[i+1];
                        // Apply height adjustment: if adjustTopOnly=true, only adjust top vertices (y=0.5)
                        if (adjustTopOnly) {
                            v.y = yPos + by + (yPos > 0.4f ? heightAdjust : 0.0f);
                        } else {
                            v.y = yPos + by + heightAdjust;
                        }
                        v.z = cube[i+2] + bz;
                        v.r = cr; v.g = cg; v.b = cb; v.a = ca;
                        v.u = uMin + cubeUVs[uv+0] * uvScaleZoomed;
                        v.v = vMin + cubeUVs[uv+1] * uvScaleZoomed;

                        // SMOOTH LIGHTING + AMBIENT OCCLUSION
                        if (DebugState::instance().lightingEnabled.getValue()) {
                            // Determine which corner of the face this vertex is at
                            // Each face has 4 vertices (corners), we sample 4 blocks per corner
                            int vertexIndex = (i - cubeStart) / 3;

                            // Calculate smooth lighting offsets based on face normal and vertex position
                            // These offsets determine which 4 blocks to sample around this vertex
                            int dx1 = 0, dy1 = 0, dz1 = 0;  // First perpendicular direction
                            int dx2 = 0, dy2 = 0, dz2 = 0;  // Second perpendicular direction

                            // Determine perpendicular directions based on face normal
                            if (faceNormal.y != 0) {
                                // Top/bottom face: perpendiculars are X and Z
                                dx1 = (vertexIndex == 0 || vertexIndex == 3) ? -1 : 0;
                                dz1 = (vertexIndex == 0 || vertexIndex == 1) ? -1 : 0;
                                dx2 = (vertexIndex == 1 || vertexIndex == 2) ? 1 : 0;
                                dz2 = (vertexIndex == 2 || vertexIndex == 3) ? 1 : 0;
                            } else if (faceNormal.x != 0) {
                                // Left/right face: perpendiculars are Y and Z
                                dy1 = (vertexIndex == 0 || vertexIndex == 3) ? 1 : 0;
                                dz1 = (vertexIndex == 0 || vertexIndex == 1) ? -1 : 0;
                                dy2 = (vertexIndex == 1 || vertexIndex == 2) ? -1 : 0;
                                dz2 = (vertexIndex == 2 || vertexIndex == 3) ? 1 : 0;
                            } else {
                                // Front/back face: perpendiculars are X and Y
                                dx1 = (vertexIndex == 0 || vertexIndex == 3) ? -1 : 0;
                                dy1 = (vertexIndex == 0 || vertexIndex == 1) ? 1 : 0;
                                dx2 = (vertexIndex == 1 || vertexIndex == 2) ? 1 : 0;
                                dy2 = (vertexIndex == 2 || vertexIndex == 3) ? -1 : 0;
                            }

                            // Get smooth lighting using VERTEX WORLD POSITION for consistent lighting across blocks
                            v.skyLight = getSmoothLight(v.x, v.y, v.z, dx1, dy1, dz1, dx2, dy2, dz2, true);
                            v.blockLight = getSmoothLight(v.x, v.y, v.z, dx1, dy1, dz1, dx2, dy2, dz2, false);

                            // Calculate ambient occlusion (still uses block position)
                            v.ao = calculateAO(X, Y, Z, dx1, dy1, dz1, dx2, dy2, dz2);
                        } else {
                            // Lighting disabled - full brightness
                            v.skyLight = 1.0f;
                            v.blockLight = 1.0f;
                            v.ao = 1.0f;
                        }

                        targetVerts.push_back(v);
                    }

                    // Create 6 indices for 2 triangles (0,1,2 and 0,2,3)
                    targetIndices.push_back(baseIndex + 0);
                    targetIndices.push_back(baseIndex + 1);
                    targetIndices.push_back(baseIndex + 2);
                    targetIndices.push_back(baseIndex + 0);
                    targetIndices.push_back(baseIndex + 2);
                    targetIndices.push_back(baseIndex + 3);
                };

                // Select appropriate texture for each face
                const BlockDefinition::FaceTexture& frontTex = def.useCubeMap ? def.front : def.all;
                const BlockDefinition::FaceTexture& backTex = def.useCubeMap ? def.back : def.all;
                const BlockDefinition::FaceTexture& leftTex = def.useCubeMap ? def.left : def.all;
                const BlockDefinition::FaceTexture& rightTex = def.useCubeMap ? def.right : def.all;
                const BlockDefinition::FaceTexture& topTex = def.useCubeMap ? def.top : def.all;
                const BlockDefinition::FaceTexture& bottomTex = def.useCubeMap ? def.bottom : def.all;

                // Face culling based on block type
                // - Solid blocks: ALWAYS render faces against air and liquids (liquids are transparent)
                // - Liquid blocks: render faces against air and solids (not other liquids to avoid z-fighting)
                bool isCurrentLiquid = def.isLiquid;
                bool isCurrentTransparent = (def.transparency > 0.0f);  // Has any transparency

                // Front face (z=0, facing -Z direction)
                {
                    bool neighborIsLiquid = isLiquid(X, Y, Z - 1);
                    bool neighborIsSolid = isSolid(X, Y, Z - 1);

                    bool shouldRender;
                    if (isCurrentLiquid) {
                        // Water: render if neighbor is not water, OR if water at different level
                        if (neighborIsLiquid) {
                            uint8_t currentLevel = m_blockMetadata[X][Y][Z];
                            uint8_t neighborLevel = (Z > 0) ? m_blockMetadata[X][Y][Z-1] : 0;
                            shouldRender = (currentLevel != neighborLevel);
                        } else {
                            shouldRender = true;
                        }
                    } else {
                        // Solid: render against non-solid (air or water)
                        shouldRender = !neighborIsSolid;
                    }

                    if (shouldRender) {
                        renderFace(frontTex, 0, 0, waterHeightAdjust, true, isCurrentTransparent, glm::ivec3(0, 0, -1));
                    }
                }

                // Back face (z=0.5, facing +Z direction)
                {
                    bool neighborIsLiquid = isLiquid(X, Y, Z + 1);
                    bool neighborIsSolid = isSolid(X, Y, Z + 1);
                    bool shouldRender;
                    if (isCurrentLiquid) {
                        if (neighborIsLiquid) {
                            uint8_t currentLevel = m_blockMetadata[X][Y][Z];
                            uint8_t neighborLevel = (Z < DEPTH-1) ? m_blockMetadata[X][Y][Z+1] : 0;
                            shouldRender = (currentLevel != neighborLevel);
                        } else {
                            shouldRender = true;
                        }
                    } else {
                        shouldRender = !neighborIsSolid;
                    }
                    if (shouldRender) {
                        renderFace(backTex, 12, 8, waterHeightAdjust, true, isCurrentTransparent, glm::ivec3(0, 0, 1));
                    }
                }

                // Left face (x=0, facing -X direction)
                {
                    bool neighborIsLiquid = isLiquid(X - 1, Y, Z);
                    bool neighborIsSolid = isSolid(X - 1, Y, Z);
                    bool shouldRender;
                    if (isCurrentLiquid) {
                        if (neighborIsLiquid) {
                            uint8_t currentLevel = m_blockMetadata[X][Y][Z];
                            uint8_t neighborLevel = (X > 0) ? m_blockMetadata[X-1][Y][Z] : 0;
                            shouldRender = (currentLevel != neighborLevel);
                        } else {
                            shouldRender = true;
                        }
                    } else {
                        shouldRender = !neighborIsSolid;
                    }
                    if (shouldRender) {
                        renderFace(leftTex, 24, 16, waterHeightAdjust, true, isCurrentTransparent, glm::ivec3(-1, 0, 0));
                    }
                }

                // Right face (x=0.5, facing +X direction)
                {
                    bool neighborIsLiquid = isLiquid(X + 1, Y, Z);
                    bool neighborIsSolid = isSolid(X + 1, Y, Z);
                    bool shouldRender;
                    if (isCurrentLiquid) {
                        if (neighborIsLiquid) {
                            uint8_t currentLevel = m_blockMetadata[X][Y][Z];
                            uint8_t neighborLevel = (X < WIDTH-1) ? m_blockMetadata[X+1][Y][Z] : 0;
                            shouldRender = (currentLevel != neighborLevel);
                        } else {
                            shouldRender = true;
                        }
                    } else {
                        shouldRender = !neighborIsSolid;
                    }
                    if (shouldRender) {
                        renderFace(rightTex, 36, 24, waterHeightAdjust, true, isCurrentTransparent, glm::ivec3(1, 0, 0));
                    }
                }

                // Top face (y=0.5, facing +Y direction)
                {
                    bool neighborIsLiquid = isLiquid(X, Y + 1, Z);
                    bool neighborIsSolid = isSolid(X, Y + 1, Z);
                    bool shouldRender = isCurrentLiquid ? !neighborIsLiquid : !neighborIsSolid;
                    if (shouldRender) {
                        // Apply water height adjustment to entire top face for flowing water effect
                        renderFace(topTex, 48, 32, waterHeightAdjust, false, isCurrentTransparent, glm::ivec3(0, 1, 0));
                    }
                }

                // Bottom face (y=0, facing -Y direction)
                {
                    bool neighborIsLiquid = isLiquid(X, Y - 1, Z);
                    bool neighborIsSolid = isSolid(X, Y - 1, Z);
                    bool shouldRender;
                    if (isCurrentLiquid) {
                        // Water: render bottom if not water below
                        // Render against both air AND solid blocks (visible from below)
                        shouldRender = !neighborIsLiquid;
                    } else {
                        // Solid: render against air/water
                        shouldRender = !neighborIsSolid;
                    }
                    if (shouldRender) {
                        renderFace(bottomTex, 60, 40, 0.0f, false, isCurrentTransparent, glm::ivec3(0, -1, 0));
                    }
                }
            }
        }
    }

    // Store opaque geometry
    m_vertexCount = static_cast<uint32_t>(verts.size());
    m_indexCount = static_cast<uint32_t>(indices.size());
    m_vertices = std::move(verts);
    m_indices = std::move(indices);

    // Store transparent geometry
    m_transparentVertexCount = static_cast<uint32_t>(transparentVerts.size());
    m_transparentIndexCount = static_cast<uint32_t>(transparentIndices.size());
    m_transparentVertices = std::move(transparentVerts);
    m_transparentIndices = std::move(transparentIndices);
}

void Chunk::createVertexBuffer(VulkanRenderer* renderer) {
    if (m_vertexCount == 0 && m_transparentVertexCount == 0) {
        return;  // No vertices to upload
    }

    // PERFORMANCE FIX: Use deferred deletion instead of vkDeviceWaitIdle()
    // Old buffers are queued for destruction after MAX_FRAMES_IN_FLIGHT frames
    // This eliminates the GPU pipeline stall that was causing spawn lag!
    destroyBuffers(renderer);

    VkDevice device = renderer->getDevice();

    // ========== CREATE OPAQUE BUFFERS ==========
    if (m_vertexCount > 0) {
        // Create vertex buffer with exception safety
        VkDeviceSize vertexBufferSize = sizeof(Vertex) * m_vertices.size();

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

        try {
        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, m_vertices.data(), (size_t)vertexBufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              m_vertexBuffer, m_vertexBufferMemory);

        renderer->copyBuffer(stagingBuffer, m_vertexBuffer, vertexBufferSize);

        // Clean up staging buffer
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
        stagingBufferMemory = VK_NULL_HANDLE;

    } catch (...) {
        // Clean up staging buffer on exception
        if (stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, stagingBuffer, nullptr);
        }
        if (stagingBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, stagingBufferMemory, nullptr);
        }
        throw;  // Re-throw to caller
    }

    // Create index buffer with exception safety
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_indices.size();

    stagingBuffer = VK_NULL_HANDLE;
    stagingBufferMemory = VK_NULL_HANDLE;

    try {
        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, m_indices.data(), (size_t)indexBufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              m_indexBuffer, m_indexBufferMemory);

        renderer->copyBuffer(stagingBuffer, m_indexBuffer, indexBufferSize);

        // Clean up staging buffer
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

    } catch (...) {
        // Clean up staging buffer on exception
        if (stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, stagingBuffer, nullptr);
        }
        if (stagingBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, stagingBufferMemory, nullptr);
        }
        // Also clean up partially created vertex buffer
        if (m_vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_vertexBuffer, nullptr);
            m_vertexBuffer = VK_NULL_HANDLE;
        }
        if (m_vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_vertexBufferMemory, nullptr);
            m_vertexBufferMemory = VK_NULL_HANDLE;
        }
        throw;  // Re-throw to caller
    }

    // Free CPU-side mesh data after successful GPU upload
    // Data is now on GPU (DEVICE_LOCAL memory), no need to keep CPU copy
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();
    }  // End of opaque buffer creation

    // ========== CREATE TRANSPARENT BUFFERS ==========
    if (m_transparentVertexCount > 0) {
        // Create transparent vertex buffer
        VkDeviceSize vertexBufferSize = sizeof(Vertex) * m_transparentVertices.size();

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

        try {
            renderer->createBuffer(vertexBufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingBufferMemory);

            void* data;
            vkMapMemory(device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
            memcpy(data, m_transparentVertices.data(), (size_t)vertexBufferSize);
            vkUnmapMemory(device, stagingBufferMemory);

            renderer->createBuffer(vertexBufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  m_transparentVertexBuffer, m_transparentVertexBufferMemory);

            renderer->copyBuffer(stagingBuffer, m_transparentVertexBuffer, vertexBufferSize);

            // Clean up staging buffer
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);
            stagingBuffer = VK_NULL_HANDLE;
            stagingBufferMemory = VK_NULL_HANDLE;

        } catch (...) {
            // Clean up staging buffer on exception
            if (stagingBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, stagingBuffer, nullptr);
            }
            if (stagingBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, stagingBufferMemory, nullptr);
            }
            throw;  // Re-throw to caller
        }

        // Create transparent index buffer
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_transparentIndices.size();

        stagingBuffer = VK_NULL_HANDLE;
        stagingBufferMemory = VK_NULL_HANDLE;

        try {
            renderer->createBuffer(indexBufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingBufferMemory);

            void* data;
            vkMapMemory(device, stagingBufferMemory, 0, indexBufferSize, 0, &data);
            memcpy(data, m_transparentIndices.data(), (size_t)indexBufferSize);
            vkUnmapMemory(device, stagingBufferMemory);

            renderer->createBuffer(indexBufferSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  m_transparentIndexBuffer, m_transparentIndexBufferMemory);

            renderer->copyBuffer(stagingBuffer, m_transparentIndexBuffer, indexBufferSize);

            // Clean up staging buffer
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);

        } catch (...) {
            // Clean up staging buffer on exception
            if (stagingBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, stagingBuffer, nullptr);
            }
            if (stagingBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, stagingBufferMemory, nullptr);
            }
            // Also clean up partially created transparent vertex buffer
            if (m_transparentVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, m_transparentVertexBuffer, nullptr);
                m_transparentVertexBuffer = VK_NULL_HANDLE;
            }
            if (m_transparentVertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, m_transparentVertexBufferMemory, nullptr);
                m_transparentVertexBufferMemory = VK_NULL_HANDLE;
            }
            throw;  // Re-throw to caller
        }

        // Free CPU-side transparent mesh data after successful GPU upload
        m_transparentVertices.clear();
        m_transparentVertices.shrink_to_fit();
        m_transparentIndices.clear();
        m_transparentIndices.shrink_to_fit();
    }  // End of transparent buffer creation
}

void Chunk::destroyBuffers(VulkanRenderer* renderer) {
    // DEFERRED DELETION: Queue buffers for destruction instead of destroying immediately
    // The renderer will destroy them after MAX_FRAMES_IN_FLIGHT frames (fence-based approach)
    // This eliminates the need for vkDeviceWaitIdle() which was causing massive lag!

    // Queue opaque buffers for deletion
    if (m_vertexBuffer != VK_NULL_HANDLE || m_vertexBufferMemory != VK_NULL_HANDLE) {
        renderer->queueBufferDeletion(m_vertexBuffer, m_vertexBufferMemory);
        m_vertexBuffer = VK_NULL_HANDLE;
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }

    if (m_indexBuffer != VK_NULL_HANDLE || m_indexBufferMemory != VK_NULL_HANDLE) {
        renderer->queueBufferDeletion(m_indexBuffer, m_indexBufferMemory);
        m_indexBuffer = VK_NULL_HANDLE;
        m_indexBufferMemory = VK_NULL_HANDLE;
    }

    // Queue transparent buffers for deletion
    if (m_transparentVertexBuffer != VK_NULL_HANDLE || m_transparentVertexBufferMemory != VK_NULL_HANDLE) {
        renderer->queueBufferDeletion(m_transparentVertexBuffer, m_transparentVertexBufferMemory);
        m_transparentVertexBuffer = VK_NULL_HANDLE;
        m_transparentVertexBufferMemory = VK_NULL_HANDLE;
    }

    if (m_transparentIndexBuffer != VK_NULL_HANDLE || m_transparentIndexBufferMemory != VK_NULL_HANDLE) {
        renderer->queueBufferDeletion(m_transparentIndexBuffer, m_transparentIndexBufferMemory);
        m_transparentIndexBuffer = VK_NULL_HANDLE;
        m_transparentIndexBufferMemory = VK_NULL_HANDLE;
    }
}

void Chunk::createVertexBufferBatched(VulkanRenderer* renderer) {
    if (m_vertexCount == 0 && m_transparentVertexCount == 0) {
        return;  // No vertices to upload
    }

    // PERFORMANCE FIX: Use deferred deletion instead of vkDeviceWaitIdle()
    // Old buffers are queued for destruction after MAX_FRAMES_IN_FLIGHT frames
    destroyBuffers(renderer);

    VkDevice device = renderer->getDevice();

    // Initialize staging buffers to NULL
    m_vertexStagingBuffer = VK_NULL_HANDLE;
    m_vertexStagingBufferMemory = VK_NULL_HANDLE;
    m_indexStagingBuffer = VK_NULL_HANDLE;
    m_indexStagingBufferMemory = VK_NULL_HANDLE;
    m_transparentVertexStagingBuffer = VK_NULL_HANDLE;
    m_transparentVertexStagingBufferMemory = VK_NULL_HANDLE;
    m_transparentIndexStagingBuffer = VK_NULL_HANDLE;
    m_transparentIndexStagingBufferMemory = VK_NULL_HANDLE;

    // ========== CREATE OPAQUE BUFFERS (BATCHED) ==========
    if (m_vertexCount > 0) {
        // Create vertex staging buffer and device buffer
        VkDeviceSize vertexBufferSize = sizeof(Vertex) * m_vertices.size();

        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_vertexStagingBuffer, m_vertexStagingBufferMemory);

        void* data;
        vkMapMemory(device, m_vertexStagingBufferMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, m_vertices.data(), (size_t)vertexBufferSize);
        vkUnmapMemory(device, m_vertexStagingBufferMemory);

        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              m_vertexBuffer, m_vertexBufferMemory);

        // Record copy command (doesn't submit yet)
        renderer->batchCopyBuffer(m_vertexStagingBuffer, m_vertexBuffer, vertexBufferSize);

        // Create index staging buffer and device buffer
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_indices.size();

        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_indexStagingBuffer, m_indexStagingBufferMemory);

        vkMapMemory(device, m_indexStagingBufferMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, m_indices.data(), (size_t)indexBufferSize);
        vkUnmapMemory(device, m_indexStagingBufferMemory);

        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              m_indexBuffer, m_indexBufferMemory);

        // Record copy command (doesn't submit yet)
        renderer->batchCopyBuffer(m_indexStagingBuffer, m_indexBuffer, indexBufferSize);
    }

    // ========== CREATE TRANSPARENT BUFFERS (BATCHED) ==========
    if (m_transparentVertexCount > 0) {
        // Create transparent vertex staging buffer and device buffer
        VkDeviceSize vertexBufferSize = sizeof(Vertex) * m_transparentVertices.size();

        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_transparentVertexStagingBuffer, m_transparentVertexStagingBufferMemory);

        void* data;
        vkMapMemory(device, m_transparentVertexStagingBufferMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, m_transparentVertices.data(), (size_t)vertexBufferSize);
        vkUnmapMemory(device, m_transparentVertexStagingBufferMemory);

        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              m_transparentVertexBuffer, m_transparentVertexBufferMemory);

        // Record copy command (doesn't submit yet)
        renderer->batchCopyBuffer(m_transparentVertexStagingBuffer, m_transparentVertexBuffer, vertexBufferSize);

        // Create transparent index staging buffer and device buffer
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_transparentIndices.size();

        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_transparentIndexStagingBuffer, m_transparentIndexStagingBufferMemory);

        vkMapMemory(device, m_transparentIndexStagingBufferMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, m_transparentIndices.data(), (size_t)indexBufferSize);
        vkUnmapMemory(device, m_transparentIndexStagingBufferMemory);

        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              m_transparentIndexBuffer, m_transparentIndexBufferMemory);

        // Record copy command (doesn't submit yet)
        renderer->batchCopyBuffer(m_transparentIndexStagingBuffer, m_transparentIndexBuffer, indexBufferSize);
    }

    // NOTE: Don't clean up staging buffers yet - caller will call cleanupStagingBuffers() after batch submit
    // NOTE: Don't free CPU-side mesh data yet - we keep it in case of errors
}

void Chunk::cleanupStagingBuffers(VulkanRenderer* renderer) {
    VkDevice device = renderer->getDevice();

    // Destroy opaque staging buffers
    if (m_vertexStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexStagingBuffer, nullptr);
        m_vertexStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexStagingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexStagingBufferMemory, nullptr);
        m_vertexStagingBufferMemory = VK_NULL_HANDLE;
    }

    if (m_indexStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexStagingBuffer, nullptr);
        m_indexStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_indexStagingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_indexStagingBufferMemory, nullptr);
        m_indexStagingBufferMemory = VK_NULL_HANDLE;
    }

    // Destroy transparent staging buffers
    if (m_transparentVertexStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_transparentVertexStagingBuffer, nullptr);
        m_transparentVertexStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_transparentVertexStagingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_transparentVertexStagingBufferMemory, nullptr);
        m_transparentVertexStagingBufferMemory = VK_NULL_HANDLE;
    }

    if (m_transparentIndexStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_transparentIndexStagingBuffer, nullptr);
        m_transparentIndexStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_transparentIndexStagingBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_transparentIndexStagingBufferMemory, nullptr);
        m_transparentIndexStagingBufferMemory = VK_NULL_HANDLE;
    }

    // Free CPU-side mesh data after successful GPU upload
    if (m_vertexCount > 0) {
        m_vertices.clear();
        m_vertices.shrink_to_fit();
        m_indices.clear();
        m_indices.shrink_to_fit();
    }

    if (m_transparentVertexCount > 0) {
        m_transparentVertices.clear();
        m_transparentVertices.shrink_to_fit();
        m_transparentIndices.clear();
        m_transparentIndices.shrink_to_fit();
    }
}

void Chunk::render(VkCommandBuffer commandBuffer, bool transparent) {
    if (transparent) {
        // Render transparent geometry
        if (m_transparentVertexCount == 0) {
            return;  // Nothing to render
        }

        // Bind transparent vertex buffer
        VkBuffer vertexBuffers[] = {m_transparentVertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Bind transparent index buffer
        vkCmdBindIndexBuffer(commandBuffer, m_transparentIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Draw indexed
        vkCmdDrawIndexed(commandBuffer, m_transparentIndexCount, 1, 0, 0, 0);
    } else {
        // Render opaque geometry
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

uint8_t Chunk::getBlockMetadata(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0;  // Out of bounds
    }
    return m_blockMetadata[x][y][z];
}

void Chunk::setBlockMetadata(int x, int y, int z, uint8_t metadata) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return;  // Out of bounds
    }
    m_blockMetadata[x][y][z] = metadata;
}

// ========== Lighting Accessors ==========

uint8_t Chunk::getSkyLight(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    return m_lightData[index].skyLight;
}

uint8_t Chunk::getBlockLight(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    return m_lightData[index].blockLight;
}

void Chunk::setSkyLight(int x, int y, int z, uint8_t value) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    m_lightData[index].skyLight = value & 0x0F;  // Clamp to 4 bits (0-15)
}

float Chunk::getInterpolatedSkyLight(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0.0f;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    return m_interpolatedLightData[index].skyLight;
}

float Chunk::getInterpolatedBlockLight(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0.0f;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    return m_interpolatedLightData[index].blockLight;
}

void Chunk::updateInterpolatedLighting(float deltaTime, float speed) {
    // Smoothly interpolate current lighting toward target lighting values
    // This creates natural, gradual lighting transitions over time
    const float lerpFactor = 1.0f - std::exp(-speed * deltaTime);  // Exponential smoothing

    for (int i = 0; i < WIDTH * HEIGHT * DEPTH; ++i) {
        const BlockLight& target = m_lightData[i];
        InterpolatedLight& current = m_interpolatedLightData[i];

        // Interpolate toward target values
        float targetSky = static_cast<float>(target.skyLight);
        float targetBlock = static_cast<float>(target.blockLight);

        current.skyLight += (targetSky - current.skyLight) * lerpFactor;
        current.blockLight += (targetBlock - current.blockLight) * lerpFactor;

        // Snap to target if very close (prevents infinite asymptotic approach)
        if (std::abs(current.skyLight - targetSky) < 0.01f) {
            current.skyLight = targetSky;
        }
        if (std::abs(current.blockLight - targetBlock) < 0.01f) {
            current.blockLight = targetBlock;
        }
    }
}

void Chunk::initializeInterpolatedLighting() {
    // Initialize interpolated values to match target values immediately
    // This prevents fade-in effect when chunks are first loaded
    for (int i = 0; i < WIDTH * HEIGHT * DEPTH; ++i) {
        const BlockLight& target = m_lightData[i];
        InterpolatedLight& current = m_interpolatedLightData[i];

        current.skyLight = static_cast<float>(target.skyLight);
        current.blockLight = static_cast<float>(target.blockLight);
    }
}

void Chunk::setBlockLight(int x, int y, int z, uint8_t value) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    m_lightData[index].blockLight = value & 0x0F;  // Clamp to 4 bits (0-15)
}

/**
 * @brief Compresses block data using Run-Length Encoding
 *
 * RLE COMPRESSION:
 * Terrain has lots of repeated blocks (layers of stone, air, etc.)
 * Format: [blockID (4 bytes), count (4 bytes), ...] repeated
 * Example: 1000 stone blocks = 8 bytes instead of 4000 bytes (99.8% compression!)
 */
void Chunk::compressBlocks(std::vector<uint8_t>& output) const {
    output.clear();
    const int totalBlocks = WIDTH * HEIGHT * DEPTH;

    int currentBlock = m_blocks[0][0][0];
    uint32_t runLength = 1;

    // Iterate through all blocks in storage order
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                if (i == 0 && j == 0 && k == 0) continue;  // Skip first block (already counted)

                int block = m_blocks[i][j][k];
                if (block == currentBlock && runLength < UINT32_MAX) {
                    runLength++;
                } else {
                    // Write run: [blockID (4 bytes), count (4 bytes)]
                    output.insert(output.end(), reinterpret_cast<const uint8_t*>(&currentBlock), reinterpret_cast<const uint8_t*>(&currentBlock) + sizeof(int));
                    output.insert(output.end(), reinterpret_cast<const uint8_t*>(&runLength), reinterpret_cast<const uint8_t*>(&runLength) + sizeof(uint32_t));

                    currentBlock = block;
                    runLength = 1;
                }
            }
        }
    }

    // Write final run
    output.insert(output.end(), reinterpret_cast<const uint8_t*>(&currentBlock), reinterpret_cast<const uint8_t*>(&currentBlock) + sizeof(int));
    output.insert(output.end(), reinterpret_cast<const uint8_t*>(&runLength), reinterpret_cast<const uint8_t*>(&runLength) + sizeof(uint32_t));
}

/**
 * @brief Decompresses block data from Run-Length Encoding
 */
bool Chunk::decompressBlocks(const std::vector<uint8_t>& input) {
    size_t offset = 0;
    int blockIndex = 0;
    const int totalBlocks = WIDTH * HEIGHT * DEPTH;

    while (offset < input.size() && blockIndex < totalBlocks) {
        // Read blockID and count
        if (offset + sizeof(int) + sizeof(uint32_t) > input.size()) {
            return false;  // Corrupted data
        }

        int blockID;
        uint32_t count;
        std::memcpy(&blockID, input.data() + offset, sizeof(int));
        offset += sizeof(int);
        std::memcpy(&count, input.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // Write run to block array
        for (uint32_t c = 0; c < count && blockIndex < totalBlocks; c++) {
            int i = blockIndex / (HEIGHT * DEPTH);
            int j = (blockIndex / DEPTH) % HEIGHT;
            int k = blockIndex % DEPTH;
            m_blocks[i][j][k] = blockID;
            blockIndex++;
        }
    }

    return blockIndex == totalBlocks;  // Success if we filled all blocks
}

/**
 * @brief Compresses metadata using Run-Length Encoding (same algorithm as blocks)
 */
void Chunk::compressMetadata(std::vector<uint8_t>& output) const {
    output.clear();

    uint8_t currentValue = m_blockMetadata[0][0][0];
    uint32_t runLength = 1;

    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                if (i == 0 && j == 0 && k == 0) continue;

                uint8_t value = m_blockMetadata[i][j][k];
                if (value == currentValue && runLength < UINT32_MAX) {
                    runLength++;
                } else {
                    // Write run: [value (1 byte), count (4 bytes)]
                    output.push_back(currentValue);
                    output.insert(output.end(), reinterpret_cast<const uint8_t*>(&runLength), reinterpret_cast<const uint8_t*>(&runLength) + sizeof(uint32_t));

                    currentValue = value;
                    runLength = 1;
                }
            }
        }
    }

    // Write final run
    output.push_back(currentValue);
    output.insert(output.end(), reinterpret_cast<const uint8_t*>(&runLength), reinterpret_cast<const uint8_t*>(&runLength) + sizeof(uint32_t));
}

/**
 * @brief Decompresses metadata from Run-Length Encoding
 */
bool Chunk::decompressMetadata(const std::vector<uint8_t>& input) {
    size_t offset = 0;
    int blockIndex = 0;
    const int totalBlocks = WIDTH * HEIGHT * DEPTH;

    while (offset < input.size() && blockIndex < totalBlocks) {
        // Read value and count
        if (offset + 1 + sizeof(uint32_t) > input.size()) {
            return false;  // Corrupted data
        }

        uint8_t value = input[offset];
        offset += 1;
        uint32_t count;
        std::memcpy(&count, input.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // Write run to metadata array
        for (uint32_t c = 0; c < count && blockIndex < totalBlocks; c++) {
            int i = blockIndex / (HEIGHT * DEPTH);
            int j = (blockIndex / DEPTH) % HEIGHT;
            int k = blockIndex % DEPTH;
            m_blockMetadata[i][j][k] = value;
            blockIndex++;
        }
    }

    return blockIndex == totalBlocks;
}

/**
 * @brief Checks if chunk is fully occluded by solid neighbors
 *
 * OCCLUSION CULLING OPTIMIZATION:
 * Underground chunks completely surrounded by stone don't need rendering at all!
 * This saves massive amounts of GPU work for invisible geometry.
 *
 * Performance: Checking 6 neighbors = 6 pointer lookups + 6 face scans
 * Benefit: Skip mesh generation + rendering for ~40% of underground chunks
 */
bool Chunk::isFullyOccluded(World* world, bool callerHoldsLock) const {
    if (!world) return false;

    // Helper lambda: Check if a chunk face is completely solid (no gaps)
    auto isFaceSolid = [](const Chunk* chunk, int face) -> bool {
        if (!chunk) return false;

        // face: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
        switch (face) {
            case 0: // +X face (x=31)
                for (int j = 0; j < HEIGHT; j++) {
                    for (int k = 0; k < DEPTH; k++) {
                        if (chunk->getBlock(WIDTH - 1, j, k) == 0) return false; // Air found
                    }
                }
                return true;
            case 1: // -X face (x=0)
                for (int j = 0; j < HEIGHT; j++) {
                    for (int k = 0; k < DEPTH; k++) {
                        if (chunk->getBlock(0, j, k) == 0) return false;
                    }
                }
                return true;
            case 2: // +Y face (y=31)
                for (int i = 0; i < WIDTH; i++) {
                    for (int k = 0; k < DEPTH; k++) {
                        if (chunk->getBlock(i, HEIGHT - 1, k) == 0) return false;
                    }
                }
                return true;
            case 3: // -Y face (y=0)
                for (int i = 0; i < WIDTH; i++) {
                    for (int k = 0; k < DEPTH; k++) {
                        if (chunk->getBlock(i, 0, k) == 0) return false;
                    }
                }
                return true;
            case 4: // +Z face (z=31)
                for (int i = 0; i < WIDTH; i++) {
                    for (int j = 0; j < HEIGHT; j++) {
                        if (chunk->getBlock(i, j, DEPTH - 1) == 0) return false;
                    }
                }
                return true;
            case 5: // -Z face (z=0)
                for (int i = 0; i < WIDTH; i++) {
                    for (int j = 0; j < HEIGHT; j++) {
                        if (chunk->getBlock(i, j, 0) == 0) return false;
                    }
                }
                return true;
            default:
                return false;
        }
    };

    // Get all 6 neighbors (use unsafe version if caller holds lock to prevent deadlock)
    Chunk* neighborPosX = callerHoldsLock ? world->getChunkAtUnsafe(m_x + 1, m_y, m_z) : world->getChunkAt(m_x + 1, m_y, m_z);
    Chunk* neighborNegX = callerHoldsLock ? world->getChunkAtUnsafe(m_x - 1, m_y, m_z) : world->getChunkAt(m_x - 1, m_y, m_z);
    Chunk* neighborPosY = callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y + 1, m_z) : world->getChunkAt(m_x, m_y + 1, m_z);
    Chunk* neighborNegY = callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y - 1, m_z) : world->getChunkAt(m_x, m_y - 1, m_z);
    Chunk* neighborPosZ = callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y, m_z + 1) : world->getChunkAt(m_x, m_y, m_z + 1);
    Chunk* neighborNegZ = callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y, m_z - 1) : world->getChunkAt(m_x, m_y, m_z - 1);

    // Check if all 6 neighbors exist and have solid facing faces
    return neighborPosX && isFaceSolid(neighborPosX, 1) &&  // Neighbor's -X face blocks our +X
           neighborNegX && isFaceSolid(neighborNegX, 0) &&  // Neighbor's +X face blocks our -X
           neighborPosY && isFaceSolid(neighborPosY, 3) &&  // Neighbor's -Y face blocks our +Y
           neighborNegY && isFaceSolid(neighborNegY, 2) &&  // Neighbor's +Y face blocks our -Y
           neighborPosZ && isFaceSolid(neighborPosZ, 5) &&  // Neighbor's -Z face blocks our +Z
           neighborNegZ && isFaceSolid(neighborNegZ, 4);    // Neighbor's +Z face blocks our -Z
}

bool Chunk::save(const std::string& worldPath) const {
    namespace fs = std::filesystem;

    try {
        // EMPTY CHUNK CULLING: Don't save empty chunks (saves disk space!)
        // Sky chunks at high Y are all air - no need to save/load them
        if (isEmpty()) {
            // Delete the file if it exists (chunk may have been cleared)
            fs::path chunksDir = fs::path(worldPath) / "chunks";
            std::string filename = "chunk_" + std::to_string(m_x) + "_" + std::to_string(m_y) + "_" + std::to_string(m_z) + ".dat";
            fs::path filepath = chunksDir / filename;
            if (fs::exists(filepath)) {
                fs::remove(filepath);
                Logger::debug() << "Deleted empty chunk file (" << m_x << ", " << m_y << ", " << m_z << ")";
            }
            return true;  // Success - nothing to save
        }

        // Create chunks directory if it doesn't exist
        fs::path chunksDir = fs::path(worldPath) / "chunks";
        fs::create_directories(chunksDir);

        // Create filename: chunk_X_Y_Z.dat
        std::string filename = "chunk_" + std::to_string(m_x) + "_" + std::to_string(m_y) + "_" + std::to_string(m_z) + ".dat";
        fs::path filepath = chunksDir / filename;

        // Open file for binary writing
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // RLE COMPRESSION: Compress block data before writing
        std::vector<uint8_t> compressedBlocks, compressedMetadata;
        compressBlocks(compressedBlocks);
        compressMetadata(compressedMetadata);

        // Write header (version 2 with RLE compression)
        constexpr uint32_t CHUNK_FILE_VERSION = 2;
        file.write(reinterpret_cast<const char*>(&CHUNK_FILE_VERSION), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_x), sizeof(int));
        file.write(reinterpret_cast<const char*>(&m_y), sizeof(int));
        file.write(reinterpret_cast<const char*>(&m_z), sizeof(int));

        // Write compressed block data size + data (variable size, typically 2-8 KB instead of 32 KB!)
        uint32_t blockDataSize = static_cast<uint32_t>(compressedBlocks.size());
        file.write(reinterpret_cast<const char*>(&blockDataSize), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(compressedBlocks.data()), blockDataSize);

        // Write compressed metadata size + data (variable size, typically <500 bytes instead of 32 KB!)
        uint32_t metadataSize = static_cast<uint32_t>(compressedMetadata.size());
        file.write(reinterpret_cast<const char*>(&metadataSize), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(compressedMetadata.data()), metadataSize);

        file.close();

        Logger::debug() << "Saved chunk (" << m_x << ", " << m_y << ", " << m_z << ") with RLE: "
                       << blockDataSize << " bytes blocks, " << metadataSize << " bytes metadata "
                       << "(was 131072 bytes uncompressed)";
        return true;

    } catch (const std::exception&) {
        return false;
    }
}

bool Chunk::load(const std::string& worldPath) {
    namespace fs = std::filesystem;

    try {
        // Build filepath
        fs::path chunksDir = fs::path(worldPath) / "chunks";
        std::string filename = "chunk_" + std::to_string(m_x) + "_" + std::to_string(m_y) + "_" + std::to_string(m_z) + ".dat";
        fs::path filepath = chunksDir / filename;

        // Check if file exists
        if (!fs::exists(filepath)) {
            return false;  // File doesn't exist - chunk needs to be generated
        }

        // Open file for binary reading
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // Read header (16 bytes)
        uint32_t version;
        int fileX, fileY, fileZ;
        file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&fileX), sizeof(int));
        file.read(reinterpret_cast<char*>(&fileY), sizeof(int));
        file.read(reinterpret_cast<char*>(&fileZ), sizeof(int));

        // Verify coordinates match
        if (fileX != m_x || fileY != m_y || fileZ != m_z) {
            file.close();
            return false;  // Coordinate mismatch
        }

        // Handle different file versions
        if (version == 1) {
            // LEGACY FORMAT: Uncompressed data (backwards compatibility)
            file.read(reinterpret_cast<char*>(m_blocks), WIDTH * HEIGHT * DEPTH * sizeof(int));
            file.read(reinterpret_cast<char*>(m_blockMetadata), WIDTH * HEIGHT * DEPTH * sizeof(uint8_t));
            file.close();
            Logger::debug() << "Loaded chunk (" << m_x << ", " << m_y << ", " << m_z << ") from legacy format";
            return true;

        } else if (version == 2) {
            // RLE COMPRESSED FORMAT: Read compressed data and decompress

            // Read compressed block data
            uint32_t blockDataSize;
            file.read(reinterpret_cast<char*>(&blockDataSize), sizeof(uint32_t));
            std::vector<uint8_t> compressedBlocks(blockDataSize);
            file.read(reinterpret_cast<char*>(compressedBlocks.data()), blockDataSize);

            // Read compressed metadata
            uint32_t metadataSize;
            file.read(reinterpret_cast<char*>(&metadataSize), sizeof(uint32_t));
            std::vector<uint8_t> compressedMetadata(metadataSize);
            file.read(reinterpret_cast<char*>(compressedMetadata.data()), metadataSize);

            file.close();

            // Decompress data
            if (!decompressBlocks(compressedBlocks)) {
                Logger::error() << "Failed to decompress block data for chunk (" << m_x << ", " << m_y << ", " << m_z << ")";
                return false;
            }
            if (!decompressMetadata(compressedMetadata)) {
                Logger::error() << "Failed to decompress metadata for chunk (" << m_x << ", " << m_y << ", " << m_z << ")";
                return false;
            }

            Logger::debug() << "Loaded chunk (" << m_x << ", " << m_y << ", " << m_z << ") from RLE format ("
                           << blockDataSize << "+" << metadataSize << " bytes)";
            return true;

        } else {
            file.close();
            Logger::error() << "Unsupported chunk file version: " << version;
            return false;  // Unsupported version
        }

    } catch (const std::exception&) {
        return false;
    }
}
