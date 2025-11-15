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
#include "terrain_constants.h"
#include "mesh_buffer_pool.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <mutex>
#include <fstream>
#include <filesystem>

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
      m_visible(false) {

    // Initialize all blocks to air and metadata to 0
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                m_blocks[i][j][k] = 0;
                m_blockMetadata[i][j][k] = 0;
            }
        }
    }

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

                // BEDROCK LAYER: Y=0 to Y=1 is always bedrock (unbreakable bottom)
                if (worldY >= 0 && worldY <= 1) {
                    m_blocks[x][y][z] = BLOCK_BEDROCK;
                    continue;
                }

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
                    // If in cave, create air pocket (caves only above Y=15)
                    // Prevent caves in the top 10 blocks below surface to avoid surface holes
                    if (isCave && worldY < terrainHeight - 10) {
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
                auto renderFace = [&](const BlockDefinition::FaceTexture& faceTexture, int cubeStart, int uvStart,
                                      float heightAdjust = 0.0f, bool adjustTopOnly = false, bool useTransparent = false) {
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
                        renderFace(frontTex, 0, 0, waterHeightAdjust, true, isCurrentTransparent);
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
                        renderFace(backTex, 12, 8, waterHeightAdjust, true, isCurrentTransparent);
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
                        renderFace(leftTex, 24, 16, waterHeightAdjust, true, isCurrentTransparent);
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
                        renderFace(rightTex, 36, 24, waterHeightAdjust, true, isCurrentTransparent);
                    }
                }

                // Top face (y=0.5, facing +Y direction)
                {
                    bool neighborIsLiquid = isLiquid(X, Y + 1, Z);
                    bool neighborIsSolid = isSolid(X, Y + 1, Z);
                    bool shouldRender = isCurrentLiquid ? !neighborIsLiquid : !neighborIsSolid;
                    if (shouldRender) {
                        // Apply water height adjustment to entire top face for flowing water effect
                        renderFace(topTex, 48, 32, waterHeightAdjust, false, isCurrentTransparent);
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
                        renderFace(bottomTex, 60, 40, 0.0f, false, isCurrentTransparent);
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

    // Destroy old buffers if they exist
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
    VkDevice device = renderer->getDevice();

    // Note: GPU synchronization should be done once at high level (main.cpp does vkDeviceWaitIdle)
    // before calling cleanup on all chunks. Doing it per-chunk is incredibly slow (hundreds of waits).

    // Destroy opaque buffers
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

    // Destroy transparent buffers
    if (m_transparentVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_transparentVertexBuffer, nullptr);
        m_transparentVertexBuffer = VK_NULL_HANDLE;
    }

    if (m_transparentVertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_transparentVertexBufferMemory, nullptr);
        m_transparentVertexBufferMemory = VK_NULL_HANDLE;
    }

    if (m_transparentIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_transparentIndexBuffer, nullptr);
        m_transparentIndexBuffer = VK_NULL_HANDLE;
    }

    if (m_transparentIndexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_transparentIndexBufferMemory, nullptr);
        m_transparentIndexBufferMemory = VK_NULL_HANDLE;
    }
}

void Chunk::createVertexBufferBatched(VulkanRenderer* renderer) {
    if (m_vertexCount == 0 && m_transparentVertexCount == 0) {
        return;  // No vertices to upload
    }

    // Destroy old buffers if they exist
    destroyBuffers(renderer);

    // Initialize staging buffers to NULL
    m_vertexStagingBuffer = VK_NULL_HANDLE;
    m_vertexStagingBufferMemory = VK_NULL_HANDLE;
    m_indexStagingBuffer = VK_NULL_HANDLE;
    m_indexStagingBufferMemory = VK_NULL_HANDLE;
    m_transparentVertexStagingBuffer = VK_NULL_HANDLE;
    m_transparentVertexStagingBufferMemory = VK_NULL_HANDLE;
    m_transparentIndexStagingBuffer = VK_NULL_HANDLE;
    m_transparentIndexStagingBufferMemory = VK_NULL_HANDLE;

    VkDevice device = renderer->getDevice();

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

bool Chunk::save(const std::string& worldPath) const {
    namespace fs = std::filesystem;

    try {
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

        // Write header (16 bytes)
        constexpr uint32_t CHUNK_FILE_VERSION = 1;
        file.write(reinterpret_cast<const char*>(&CHUNK_FILE_VERSION), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&m_x), sizeof(int));
        file.write(reinterpret_cast<const char*>(&m_y), sizeof(int));
        file.write(reinterpret_cast<const char*>(&m_z), sizeof(int));

        // Write block data (32 KB)
        file.write(reinterpret_cast<const char*>(m_blocks), WIDTH * HEIGHT * DEPTH * sizeof(int));

        // Write metadata (32 KB)
        file.write(reinterpret_cast<const char*>(m_blockMetadata), WIDTH * HEIGHT * DEPTH * sizeof(uint8_t));

        file.close();
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

        // Check version compatibility
        if (version != 1) {
            file.close();
            return false;  // Unsupported version
        }

        // Read block data (32 KB)
        file.read(reinterpret_cast<char*>(m_blocks), WIDTH * HEIGHT * DEPTH * sizeof(int));

        // Read metadata (32 KB)
        file.read(reinterpret_cast<char*>(m_blockMetadata), WIDTH * HEIGHT * DEPTH * sizeof(uint8_t));

        file.close();
        return true;

    } catch (const std::exception&) {
        return false;
    }
}
