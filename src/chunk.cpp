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
#include <sstream>
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
      m_lightingDirty(false),
      m_needsDecoration(false),
      m_hasLightingData(false),
      m_terrainReady(false),  // MULTI-STAGE GENERATION: Start false, set true after terrain generation
      m_isEmpty(true),      // PERFORMANCE: Start empty (all air)
      m_isEmptyValid(true)  // Cache is valid initially
{

    // Initialize all blocks to air, metadata to 0, and lighting to darkness
    // OPTIMIZATION: Use memset instead of nested loops (10-20x faster)
    std::memset(m_blocks, 0, sizeof(m_blocks));
    std::memset(m_blockMetadata, 0, sizeof(m_blockMetadata));

    // Initialize all light data to 0 (complete darkness)
    m_lightData.fill(BlockLight(0, 0));

    // MEMORY OPTIMIZATION (2025-11-25): Interpolated lighting is lazy-allocated
    // Don't allocate here - will be allocated on first use (saves 256KB per cached chunk)
    m_interpolatedLightData = nullptr;

    // BUG FIX: Initialize heightmap to -1 (indicates all air columns)
    // Without this, heightmap contains garbage memory causing incorrect lighting
    m_heightMap.fill(-1);

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
    // OPTIMIZATION: Use memset instead of nested loops (10-20x faster)
    std::memset(m_blocks, 0, sizeof(m_blocks));
    std::memset(m_blockMetadata, 0, sizeof(m_blockMetadata));

    // Reset lighting to darkness
    m_lightData.fill(BlockLight(0, 0));
    // MEMORY OPTIMIZATION (2025-11-25): Deallocate interpolated lighting on reset
    m_interpolatedLightData = nullptr;
    m_lightingDirty = false;

    // BUG FIX: Reset heightmap to -1 (all air)
    // Without this, recycled chunks use stale heightmap from previous location
    m_heightMap.fill(-1);

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

    // Reset visibility and flags
    m_visible = false;
    m_needsDecoration = false;
    m_hasLightingData = false;
    m_terrainReady = false;  // MULTI-STAGE GENERATION: Reset to false for fresh generation
    m_isEmpty = true;        // PERFORMANCE: Reset isEmpty cache
    m_isEmptyValid = true;   // Cache is valid initially

    // Reset state machine to UNLOADED (ready for fresh generation)
    m_state.store(ChunkState::UNLOADED, std::memory_order_release);
}

/**
 * @brief Checks if chunk is completely empty (all air blocks)
 *
 * PERFORMANCE OPTIMIZATION: Uses cached isEmpty state to avoid 32,768 block scans.
 * Cache is invalidated on setBlock() and recomputed lazily on first isEmpty() call.
 *
 * Before: O(32K) - scans all blocks every call
 * After: O(1) - returns cached value (recomputed only when invalid)
 * Impact: 1.6M block checks/frame → ~0 (at 50 chunks/frame unload rate)
 */
bool Chunk::isEmpty() const {
    // FAST PATH: Return cached value if valid
    if (m_isEmptyValid) {
        return m_isEmpty;
    }

    // SLOW PATH: Recompute and cache (only when cache invalidated)
    bool empty = true;
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            for (int k = 0; k < DEPTH; k++) {
                if (m_blocks[i][j][k] != 0) {
                    empty = false;
                    goto done;  // Early exit on first non-air block
                }
            }
        }
    }
done:
    m_isEmpty = empty;
    m_isEmptyValid = true;
    return empty;
}

// =============================================================================
// CHUNK STATE MACHINE (2025-11-25)
// =============================================================================

/**
 * @brief Validates and performs a state transition
 *
 * STATE MACHINE IMPLEMENTATION:
 * Validates that the requested transition is legal according to the chunk lifecycle.
 * Invalid transitions are logged for debugging but do not crash the engine.
 *
 * Valid transitions:
 *   UNLOADED -> LOADING (worker picks up chunk)
 *   LOADING -> GENERATED (terrain complete)
 *   GENERATED -> DECORATING (decoration starts)
 *   DECORATING -> AWAITING_MESH (decoration complete)
 *   AWAITING_MESH -> MESHING (mesh worker picks up)
 *   MESHING -> AWAITING_UPLOAD (mesh complete)
 *   AWAITING_UPLOAD -> UPLOADING (GPU upload starts)
 *   UPLOADING -> ACTIVE (upload complete)
 *   ACTIVE -> UNLOADING (chunk being removed)
 *   UNLOADING -> UNLOADED (cleanup complete)
 *
 * Also allows:
 *   ACTIVE -> AWAITING_MESH (block change triggers remesh)
 *   Any -> UNLOADED (forced reset/pool return)
 *
 * @param newState Target state to transition to
 * @return True if transition was valid and performed, false otherwise
 */
bool Chunk::transitionTo(ChunkState newState) {
    ChunkState current = m_state.load(std::memory_order_acquire);

    // Validate transition is legal
    bool valid = false;

    switch (current) {
        case ChunkState::UNLOADED:
            valid = (newState == ChunkState::LOADING);
            break;
        case ChunkState::LOADING:
            valid = (newState == ChunkState::GENERATED);
            break;
        case ChunkState::GENERATED:
            valid = (newState == ChunkState::DECORATING);
            break;
        case ChunkState::DECORATING:
            valid = (newState == ChunkState::AWAITING_MESH);
            break;
        case ChunkState::AWAITING_MESH:
            valid = (newState == ChunkState::MESHING);
            break;
        case ChunkState::MESHING:
            valid = (newState == ChunkState::AWAITING_UPLOAD);
            break;
        case ChunkState::AWAITING_UPLOAD:
            valid = (newState == ChunkState::UPLOADING);
            break;
        case ChunkState::UPLOADING:
            valid = (newState == ChunkState::ACTIVE);
            break;
        case ChunkState::ACTIVE:
            // ACTIVE can go to UNLOADING (removal) or AWAITING_MESH (block change)
            valid = (newState == ChunkState::UNLOADING ||
                     newState == ChunkState::AWAITING_MESH);
            break;
        case ChunkState::UNLOADING:
            valid = (newState == ChunkState::UNLOADED);
            break;
    }

    // Also allow forced reset to UNLOADED from any state (pool return)
    if (newState == ChunkState::UNLOADED) {
        valid = true;
    }

    if (valid) {
        m_state.store(newState, std::memory_order_release);
        return true;
    }

    // Log invalid transition for debugging
    Logger::warning() << "Invalid chunk state transition: "
                     << chunkStateToString(current) << " -> "
                     << chunkStateToString(newState)
                     << " at chunk (" << m_x << ", " << m_y << ", " << m_z << ")";
    return false;
}

/**
 * @brief Attempts atomic compare-and-swap state transition
 *
 * THREAD-SAFE TRANSITION:
 * Uses compare_exchange_strong for lock-free state claiming.
 * Only succeeds if current state exactly matches expectedState.
 * Perfect for worker threads competing to claim chunks from queues.
 *
 * Example usage (mesh worker):
 *   if (chunk->tryTransition(ChunkState::AWAITING_MESH, ChunkState::MESHING)) {
 *       // This worker claimed the chunk, generate mesh
 *   } else {
 *       // Another worker got it, move on
 *   }
 *
 * @param expectedState State the chunk must be in for transition to succeed
 * @param newState State to transition to if successful
 * @return True if transition succeeded, false if state didn't match expected
 */
bool Chunk::tryTransition(ChunkState expectedState, ChunkState newState) {
    return m_state.compare_exchange_strong(
        expectedState, newState,
        std::memory_order_acq_rel,
        std::memory_order_acquire
    );
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
        Logger::error() << "BiomeMap is null in Chunk::generate()";
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

                // BEDROCK LAYER: Y <= -98 is always bedrock (indestructible bottom layer)
                // This creates the absolute bottom of the world, preventing falling into void
                if (worldY <= BEDROCK_LAYER_Y) {
                    m_blocks[x][y][z] = BLOCK_BEDROCK;
                    continue;
                }

                // SOLID STONE FOUNDATION: Y=-97 to Y=10 is always stone (no caves)
                // This provides a solid foundation above bedrock and ensures stability
                if (worldY >= -97 && worldY <= 10) {
                    m_blocks[x][y][z] = BLOCK_STONE;
                    continue;
                }

                // Check if this is inside a cave (extended to deep underground)
                // Pass terrainHeight to avoid redundant calculation (was 32x per column!)
                float caveDensity = biomeMap->getCaveDensityAt(worldX, worldYf, worldZ, terrainHeight);
                bool isCave = (caveDensity < 0.45f) && (worldY > 10);  // Caves above solid foundation layer

                // Check if inside underground biome chamber (extended to deep underground)
                bool isUndergroundChamber = biomeMap->isUndergroundBiomeAt(worldX, worldYf, worldZ) && (worldY > 10);

                // Determine block placement
                if (worldY < terrainHeight) {
                    // Below surface

                    // ============================================================================
                    // WATER FLOOR LOGIC (2025-11-25): Sand at bottom of ALL water bodies
                    // ============================================================================
                    // Check if this block is under water (terrain below water level)
                    // This applies to oceans, rivers, lakes, and ponds
                    // ============================================================================
                    bool isUnderwater = (terrainHeight < WATER_LEVEL);

                    if (isUnderwater) {
                        // Underwater terrain - place sand at the bottom
                        int depthFromSurface = terrainHeight - worldY;

                        if (depthFromSurface <= 3) {
                            // Water floor top layers - sand (covers river/lake/ocean bottoms)
                            m_blocks[x][y][z] = BLOCK_SAND;
                        } else if (isOcean) {
                            // Deep ocean floor - stone (only for deep oceans)
                            m_blocks[x][y][z] = BLOCK_STONE;
                        } else {
                            // Shallow water bodies - use biome's stone/dirt
                            m_blocks[x][y][z] = (depthFromSurface <= 6) ? BLOCK_DIRT : biome->primary_stone_block;
                        }
                        continue;
                    }

                    // LAND BIOME LOGIC (terrain above water level)
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
                        // ================================================================
                        // ELEVATION-AWARE SURFACE BLOCKS (2025-11-25)
                        // ================================================================
                        // - High elevation (>SNOW_LINE): snow
                        // - Mid elevation (STONE_LINE to SNOW_LINE): exposed stone (mountains)
                        // - Low elevation: biome's normal surface block (grass/sand/etc)
                        // ================================================================
                        constexpr int STONE_LINE = 100;  // Stone starts appearing at Y=100

                        if (worldY >= SNOW_LINE + SNOW_TRANSITION) {
                            // Fully above snow line - always snow
                            m_blocks[x][y][z] = BLOCK_SNOW;
                        } else if (worldY >= SNOW_LINE) {
                            // Snow transition zone - mix snow and stone using noise
                            float snowNoise = biomeMap->getTerrainNoise(worldX, worldZ);
                            float snowChance = static_cast<float>(worldY - SNOW_LINE) / static_cast<float>(SNOW_TRANSITION);
                            if (snowNoise > (1.0f - snowChance * 2.0f)) {
                                m_blocks[x][y][z] = BLOCK_SNOW;
                            } else {
                                m_blocks[x][y][z] = BLOCK_STONE;  // Exposed stone below snow
                            }
                        } else if (worldY >= STONE_LINE && biome->height_multiplier > 1.5f) {
                            // Mid-elevation mountainous terrain - exposed stone
                            // Only for biomes with height_multiplier > 1.5 (mountains)
                            float stoneNoise = biomeMap->getTerrainNoise(worldX + 1000.0f, worldZ);
                            float stoneChance = static_cast<float>(worldY - STONE_LINE) / static_cast<float>(SNOW_LINE - STONE_LINE);
                            // Higher = more stone, noise adds variation
                            if (stoneNoise > (1.0f - stoneChance * 1.5f)) {
                                m_blocks[x][y][z] = BLOCK_STONE;
                            } else {
                                m_blocks[x][y][z] = biome->primary_surface_block;
                            }
                        } else {
                            // Low elevation - use biome's normal surface block
                            m_blocks[x][y][z] = biome->primary_surface_block;
                        }
                    } else if (depthFromSurface <= TOPSOIL_DEPTH) {
                        // Topsoil layer - dirt (or snow layer if very high elevation)
                        if (worldY >= SNOW_LINE + SNOW_TRANSITION && depthFromSurface <= 2) {
                            m_blocks[x][y][z] = BLOCK_SNOW;  // Snow layer below surface snow
                        } else {
                            m_blocks[x][y][z] = BLOCK_DIRT;
                        }
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

    // PERFORMANCE: Build heightmap for fast sky light calculation
    // This replaces expensive BFS propagation with O(1) lookups
    rebuildHeightMap();

    // FIXED (2025-11-23): Mark freshly generated chunks as needing decoration
    // This prevents re-decorating chunks loaded from disk (which would overwrite player edits)
    m_needsDecoration = true;
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
void Chunk::generateMesh(World* world, bool callerHoldsLock, int lodLevel) {
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
    std::vector<CompressedVertex> verts = pool.acquireVertexBuffer();
    std::vector<uint32_t> indices = pool.acquireIndexBuffer();
    std::vector<CompressedVertex> transparentVerts = pool.acquireVertexBuffer();
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

    // Max quad size for greedy meshing - limited by TWO constraints:
    // 1. UV constraint: quadSize <= atlasSize (prevents UV from crossing cell boundaries)
    // 2. Bit constraint: quadSize <= 31 (CompressedVertex uses 5 bits for width/height)
    // For tiled UV encoding, UV = cell + localUV * quadSize / atlasSize
    const int maxQuadSize = std::min(31, (atlasGridSize > 0) ? atlasGridSize : 4);

    // ============================================================================
    // PERFORMANCE FIX (2025-11-24): Cache neighbor chunks to eliminate hash lookups
    // ============================================================================
    // OLD: Every out-of-bounds query called world->getBlockAt() → hash lookup + mutex
    //      ~30,000 face checks per chunk × up to 4 hash lookups = 120,000 lookups/chunk
    //      At 10 chunks/frame × 60 FPS = 72 MILLION hash lookups per second!
    // NEW: Cache 6 neighbor chunk pointers upfront (6 lookups instead of 120,000)
    //      Direct chunk->getBlock() access (array lookup, no hash, no mutex)
    // IMPACT: 99.995% reduction in neighbor queries → Massive mesh generation speedup
    // ============================================================================
    // DEADLOCK FIX (2025-11-25): Use getChunkAtUnsafe when caller already holds the lock!
    // Otherwise getChunkAt tries to acquire shared_lock while caller holds unique_lock → deadlock
    Chunk* neighborPosX = world ? (callerHoldsLock ? world->getChunkAtUnsafe(m_x + 1, m_y, m_z) : world->getChunkAt(m_x + 1, m_y, m_z)) : nullptr;
    Chunk* neighborNegX = world ? (callerHoldsLock ? world->getChunkAtUnsafe(m_x - 1, m_y, m_z) : world->getChunkAt(m_x - 1, m_y, m_z)) : nullptr;
    Chunk* neighborPosY = world ? (callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y + 1, m_z) : world->getChunkAt(m_x, m_y + 1, m_z)) : nullptr;
    Chunk* neighborNegY = world ? (callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y - 1, m_z) : world->getChunkAt(m_x, m_y - 1, m_z)) : nullptr;
    Chunk* neighborPosZ = world ? (callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y, m_z + 1) : world->getChunkAt(m_x, m_y, m_z + 1)) : nullptr;
    Chunk* neighborNegZ = world ? (callerHoldsLock ? world->getChunkAtUnsafe(m_x, m_y, m_z - 1) : world->getChunkAt(m_x, m_y, m_z - 1)) : nullptr;

    // Helper: Convert local chunk coordinates to world position (eliminates code duplication)
    auto localToWorldPos = [this](int x, int y, int z) -> glm::vec3 {
        int worldBlockX = m_x * WIDTH + x;
        int worldBlockY = m_y * HEIGHT + y;
        int worldBlockZ = m_z * DEPTH + z;
        return glm::vec3(static_cast<float>(worldBlockX), static_cast<float>(worldBlockY), static_cast<float>(worldBlockZ));
    };

    // Helper: Fast neighbor block query using cached chunks (99.995% faster than world->getBlockAt!)
    auto getNeighborBlock = [this, neighborPosX, neighborNegX, neighborPosY, neighborNegY, neighborPosZ, neighborNegZ]
                            (int x, int y, int z) -> int {
        // Inside this chunk - direct array access
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            return m_blocks[x][y][z];
        }

        // Out of bounds - check cached neighbor chunks
        Chunk* neighbor = nullptr;
        int localX = x;
        int localY = y;
        int localZ = z;

        // Determine which neighbor chunk and convert to local coordinates
        if (x < 0) {
            neighbor = neighborNegX;
            localX = x + WIDTH;  // -1 becomes 31
        } else if (x >= WIDTH) {
            neighbor = neighborPosX;
            localX = x - WIDTH;  // 32 becomes 0
        } else if (y < 0) {
            neighbor = neighborNegY;
            localY = y + HEIGHT;
        } else if (y >= HEIGHT) {
            neighbor = neighborPosY;
            localY = y - HEIGHT;
        } else if (z < 0) {
            neighbor = neighborNegZ;
            localZ = z + DEPTH;
        } else if (z >= DEPTH) {
            neighbor = neighborPosZ;
            localZ = z - DEPTH;
        }

        // If neighbor exists, get block from it (direct array access - fast!)
        if (neighbor && localX >= 0 && localX < WIDTH && localY >= 0 && localY < HEIGHT && localZ >= 0 && localZ < DEPTH) {
            return neighbor->m_blocks[localX][localY][localZ];
        }

        // Neighbor doesn't exist (edge of loaded world) - return air
        return 0;
    };

    // Helper: Fast neighbor metadata query for water levels across chunk boundaries
    auto getNeighborMetadata = [this, neighborPosX, neighborNegX, neighborPosY, neighborNegY, neighborPosZ, neighborNegZ]
                               (int x, int y, int z, uint8_t defaultValue = 0) -> uint8_t {
        // Inside this chunk - direct array access
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            return m_blockMetadata[x][y][z];
        }

        // Out of bounds - check cached neighbor chunks
        Chunk* neighbor = nullptr;
        int localX = x;
        int localY = y;
        int localZ = z;

        // Determine which neighbor chunk and convert to local coordinates
        if (x < 0) {
            neighbor = neighborNegX;
            localX = x + WIDTH;  // -1 becomes 31
        } else if (x >= WIDTH) {
            neighbor = neighborPosX;
            localX = x - WIDTH;  // 32 becomes 0
        } else if (y < 0) {
            neighbor = neighborNegY;
            localY = y + HEIGHT;
        } else if (y >= HEIGHT) {
            neighbor = neighborPosY;
            localY = y - HEIGHT;
        } else if (z < 0) {
            neighbor = neighborNegZ;
            localZ = z + DEPTH;
        } else if (z >= DEPTH) {
            neighbor = neighborPosZ;
            localZ = z - DEPTH;
        }

        // If neighbor exists, get metadata from it (direct array access - fast!)
        if (neighbor && localX >= 0 && localX < WIDTH && localY >= 0 && localY < HEIGHT && localZ >= 0 && localZ < DEPTH) {
            return neighbor->m_blockMetadata[localX][localY][localZ];
        }

        // Neighbor doesn't exist (edge of loaded world) - return default
        return defaultValue;
    };

    // Helper lambda to check if a block is solid (non-air)
    // PERFORMANCE FIX: Uses cached neighbor chunks instead of world->getBlockAt()
    auto isSolid = [&getNeighborBlock, &registry](int x, int y, int z) -> bool {
        int blockID = getNeighborBlock(x, y, z);
        if (blockID == 0) return false;
        // Bounds check before registry access to prevent crash
        if (blockID < 0 || blockID >= registry.count()) return false;
        return !registry.get(blockID).isLiquid;  // Solid = not air and not liquid
    };

    // Helper lambda to check if a block is liquid
    // PERFORMANCE FIX: Uses cached neighbor chunks instead of world->getBlockAt()
    auto isLiquid = [&getNeighborBlock, &registry](int x, int y, int z) -> bool {
        int blockID = getNeighborBlock(x, y, z);
        if (blockID == 0) return false;
        // Bounds check before registry access to prevent crash
        if (blockID < 0 || blockID >= registry.count()) return false;
        return registry.get(blockID).isLiquid;
    };

    // Helper lambda to check if a block is transparent (leaves, glass, etc.)
    // PERFORMANCE FIX: Uses cached neighbor chunks instead of world->getBlockAt()
    auto isTransparent = [&getNeighborBlock, &registry](int x, int y, int z) -> bool {
        int blockID = getNeighborBlock(x, y, z);
        if (blockID == 0) return false;  // Air is not transparent, it's nothing
        if (blockID < 0 || blockID >= registry.count()) return false;
        return registry.get(blockID).transparency > 0.0f;
    };

    // Helper lambda to get the block ID at a position (for neighbor comparison)
    // PERFORMANCE FIX: Uses cached neighbor chunks instead of world->getBlockAt()
    auto getBlockID = [&getNeighborBlock](int x, int y, int z) -> int {
        return getNeighborBlock(x, y, z);
    };

    // SMOOTH LIGHTING: Helper to get light at a vertex by sampling 4 adjacent blocks
    // This creates smooth gradients between different light levels (Minecraft-style)
    // Uses WORLD-SPACE vertex position to ensure consistent lighting across block boundaries
    // Uses INTERPOLATED lighting values for smooth time-based transitions
    // PERFORMANCE FIX: Uses cached neighbor chunks instead of world->getChunkAtWorldPos()
    auto getSmoothLight = [this, neighborPosX, neighborNegX, neighborPosY, neighborNegY, neighborPosZ, neighborNegZ, callerHoldsLock](
        float vx, float vy, float vz, int dx1, int dy1, int dz1, int dx2, int dy2, int dz2, bool isSky) -> float {

        // Convert vertex world position to block coordinates
        int blockX = static_cast<int>(std::floor(vx));
        int blockY = static_cast<int>(std::floor(vy));
        int blockZ = static_cast<int>(std::floor(vz));

        // Sample 4 blocks around this vertex in world space
        float light1, light2, light3, light4;

        // PERFORMANCE FIX: Use cached neighbor chunks instead of hash lookups
        auto getLightAtWorldPos = [&](int worldX, int worldY, int worldZ) -> float {
            // Convert to chunk-local coordinates for this chunk
            int localX = worldX - (m_x * WIDTH);
            int localY = worldY - (m_y * HEIGHT);
            int localZ = worldZ - (m_z * DEPTH);

            // Inside this chunk - direct access
            if (localX >= 0 && localX < WIDTH && localY >= 0 && localY < HEIGHT && localZ >= 0 && localZ < DEPTH) {
                return isSky ? getInterpolatedSkyLight(localX, localY, localZ) : getInterpolatedBlockLight(localX, localY, localZ);
            }

            // Out of bounds - use cached neighbor chunks
            Chunk* chunk = nullptr;
            if (localX < 0) {
                chunk = neighborNegX;
                localX += WIDTH;
            } else if (localX >= WIDTH) {
                chunk = neighborPosX;
                localX -= WIDTH;
            } else if (localY < 0) {
                chunk = neighborNegY;
                localY += HEIGHT;
            } else if (localY >= HEIGHT) {
                chunk = neighborPosY;
                localY -= HEIGHT;
            } else if (localZ < 0) {
                chunk = neighborNegZ;
                localZ += DEPTH;
            } else if (localZ >= DEPTH) {
                chunk = neighborPosZ;
                localZ -= DEPTH;
            }

            // If neighbor chunk exists, get lighting from it
            if (chunk && localX >= 0 && localX < WIDTH && localY >= 0 && localY < HEIGHT && localZ >= 0 && localZ < DEPTH) {
                return isSky ? chunk->getInterpolatedSkyLight(localX, localY, localZ) : chunk->getInterpolatedBlockLight(localX, localY, localZ);
            }

            // Fallback: assume full sunlight for sky, no block light
            return isSky ? 15.0f : 0.0f;
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

    // =================================================================================================
    // BINARY GREEDY MESHING OPTIMIZATION (2025-11-25): Fast bitmask-based face tracking
    // =================================================================================================
    // PERFORMANCE FIX: Replace std::vector<bool> with uint32_t bitmasks
    // - std::vector<bool> uses proxy objects with ~10x overhead per access
    // - uint32_t arrays allow direct bit manipulation (single AND/OR instruction)
    // - 32-bit width matches chunk WIDTH (32) for efficient slice processing
    //
    // Memory layout: processed[Y][Z] contains 32 bits for X=0..31
    // Access: processed[y][z] & (1u << x) to check, |= (1u << x) to set
    // Total: 32x32 = 1024 uint32_t = 4KB per direction (same as vector<bool>)
    // =================================================================================================
    uint32_t processedPosX[HEIGHT][DEPTH] = {};  // X-facing: bits represent X positions
    uint32_t processedNegX[HEIGHT][DEPTH] = {};
    uint32_t processedPosY[WIDTH][DEPTH] = {};   // Y-facing: bits represent Y positions
    uint32_t processedNegY[WIDTH][DEPTH] = {};
    uint32_t processedPosZ[WIDTH][HEIGHT] = {};  // Z-facing: bits represent Z positions
    uint32_t processedNegZ[WIDTH][HEIGHT] = {};

    // Helper macros for fast bit operations (avoid function call overhead)
    #define IS_PROCESSED_POSX(x, y, z) (processedPosX[y][z] & (1u << (x)))
    #define IS_PROCESSED_NEGX(x, y, z) (processedNegX[y][z] & (1u << (x)))
    #define IS_PROCESSED_POSY(x, y, z) (processedPosY[x][z] & (1u << (y)))
    #define IS_PROCESSED_NEGY(x, y, z) (processedNegY[x][z] & (1u << (y)))
    #define IS_PROCESSED_POSZ(x, y, z) (processedPosZ[x][y] & (1u << (z)))
    #define IS_PROCESSED_NEGZ(x, y, z) (processedNegZ[x][y] & (1u << (z)))

    #define SET_PROCESSED_POSX(x, y, z) (processedPosX[y][z] |= (1u << (x)))
    #define SET_PROCESSED_NEGX(x, y, z) (processedNegX[y][z] |= (1u << (x)))
    #define SET_PROCESSED_POSY(x, y, z) (processedPosY[x][z] |= (1u << (y)))
    #define SET_PROCESSED_NEGY(x, y, z) (processedNegY[x][z] |= (1u << (y)))
    #define SET_PROCESSED_POSZ(x, y, z) (processedPosZ[x][y] |= (1u << (z)))
    #define SET_PROCESSED_NEGZ(x, y, z) (processedNegZ[x][y] |= (1u << (z)))

    // Iterate over every block in the chunk (optimized order for cache locality)
    for(int X = 0; X < WIDTH;  X++) {
        for(int Y = 0; Y < HEIGHT; Y++) {
            for(int Z = 0; Z < DEPTH;  Z++) {
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
                // width, height: Size of the merged quad (1.0 = single block, >1.0 = merged)
                // startX, startY, startZ: Starting position of the merged quad
                auto renderFace = [&](const BlockDefinition::FaceTexture& faceTexture, int cubeStart, int uvStart,
                                      float heightAdjust = 0.0f, bool adjustTopOnly = false, bool useTransparent = false,
                                      glm::ivec3 faceNormal = glm::ivec3(0, 0, 0),
                                      float quadWidth = 1.0f, float quadHeight = 1.0f,
                                      int startX = -1, int startY = -1, int startZ = -1) {
                    // Use provided start position or fall back to current block position
                    float blockX = (startX >= 0) ? float(m_x * WIDTH + startX) : bx;
                    float blockY = (startY >= 0) ? float(m_y * HEIGHT + startY) : by;
                    float blockZ = (startZ >= 0) ? float(m_z * DEPTH + startZ) : bz;

                    // Choose which vectors to use based on transparency
                    auto& targetVerts = useTransparent ? transparentVerts : verts;
                    auto& targetIndices = useTransparent ? transparentIndices : indices;

                    // Get the base index for these vertices
                    uint32_t baseIndex = static_cast<uint32_t>(targetVerts.size());

                    // ========== COMPRESSED VERTEX SETUP ==========
                    // Determine normal index from face normal
                    uint8_t normalIndex;
                    if (faceNormal.x > 0) normalIndex = CompressedVertex::NORMAL_POS_X;
                    else if (faceNormal.x < 0) normalIndex = CompressedVertex::NORMAL_NEG_X;
                    else if (faceNormal.y > 0) normalIndex = CompressedVertex::NORMAL_POS_Y;
                    else if (faceNormal.y < 0) normalIndex = CompressedVertex::NORMAL_NEG_Y;
                    else if (faceNormal.z > 0) normalIndex = CompressedVertex::NORMAL_POS_Z;
                    else normalIndex = CompressedVertex::NORMAL_NEG_Z;

                    // Determine if this is a top/bottom face (affects UV corner mapping)
                    bool isYFace = (faceNormal.y != 0);

                    // Get atlas cell indices (clamp to valid range)
                    float atlasSize = (uvScale > 0.0f) ? (1.0f / uvScale) : 16.0f;
                    int maxCell = static_cast<int>(atlasSize) - 1;
                    uint8_t atlasX = static_cast<uint8_t>(std::clamp(faceTexture.atlasX, 0, maxCell));
                    uint8_t atlasY = static_cast<uint8_t>(std::clamp(faceTexture.atlasY, 0, maxCell));

                    // Calculate quad dimensions (clamped to 0-31)
                    uint8_t qw = static_cast<uint8_t>(std::clamp(static_cast<int>(quadWidth), 1, 31));
                    uint8_t qh = static_cast<uint8_t>(std::clamp(static_cast<int>(quadHeight), 1, 31));

                    // Determine color tint based on block type
                    // TODO: Add isFoliage/isGrass properties to BlockDefinition for tint support
                    uint8_t colorTint = CompressedVertex::TINT_WHITE;
                    if (def.isLiquid) {
                        colorTint = CompressedVertex::TINT_WATER;
                    }

                    // Calculate lighting once per face (classic retro style)
                    uint8_t skyLightInt = 15;
                    uint8_t blockLightInt = 0;
                    uint8_t aoInt = 15;  // 15 = full brightness (1.0), no AO darkening

                    if (DebugState::instance().lightingEnabled.getValue()) {
                        int sampleX = X + faceNormal.x;
                        int sampleY = Y + faceNormal.y;
                        int sampleZ = Z + faceNormal.z;

                        if (callerHoldsLock) {
                            if (sampleX >= 0 && sampleX < WIDTH && sampleY >= 0 && sampleY < HEIGHT && sampleZ >= 0 && sampleZ < DEPTH) {
                                skyLightInt = static_cast<uint8_t>(calculateSkyLightFromHeightmap(sampleX, sampleY, sampleZ));
                                blockLightInt = static_cast<uint8_t>(std::clamp(getInterpolatedBlockLight(sampleX, sampleY, sampleZ) * 15.0f, 0.0f, 15.0f));
                            }
                        } else {
                            glm::vec3 sampleWorldPos = localToWorldPos(sampleX, sampleY, sampleZ);
                            Chunk* chunk = world->getChunkAtWorldPos(sampleWorldPos.x, sampleWorldPos.y, sampleWorldPos.z);
                            if (chunk) {
                                int localX = static_cast<int>(sampleWorldPos.x) - (chunk->getChunkX() * WIDTH);
                                int localY = static_cast<int>(sampleWorldPos.y) - (chunk->getChunkY() * HEIGHT);
                                int localZ = static_cast<int>(sampleWorldPos.z) - (chunk->getChunkZ() * DEPTH);
                                skyLightInt = static_cast<uint8_t>(chunk->calculateSkyLightFromHeightmap(localX, localY, localZ));
                                blockLightInt = static_cast<uint8_t>(std::clamp(chunk->getInterpolatedBlockLight(localX, localY, localZ) * 15.0f, 0.0f, 15.0f));
                            }
                        }
                    }

                    // Corner index mapping for UV calculation
                    // Vertex order: BL(0), BR(1), TR(2), TL(3)
                    // Side faces use V-flipped UVs, top/bottom use standard UVs
                    // Side faces V-flipped: (0,H), (W,H), (W,0), (0,0) -> corners 2,3,1,0
                    // Top/bottom standard: (0,0), (W,0), (W,H), (0,H) -> corners 0,1,3,2
                    static constexpr uint8_t sideCorners[4] = {
                        CompressedVertex::CORNER_HEIGHT,  // Vertex 0: UV(0, H)
                        CompressedVertex::CORNER_BOTH,    // Vertex 1: UV(W, H)
                        CompressedVertex::CORNER_WIDTH,   // Vertex 2: UV(W, 0)
                        CompressedVertex::CORNER_ORIGIN   // Vertex 3: UV(0, 0)
                    };
                    static constexpr uint8_t yFaceCorners[4] = {
                        CompressedVertex::CORNER_ORIGIN,  // Vertex 0: UV(0, 0)
                        CompressedVertex::CORNER_WIDTH,   // Vertex 1: UV(W, 0)
                        CompressedVertex::CORNER_BOTH,    // Vertex 2: UV(W, H)
                        CompressedVertex::CORNER_HEIGHT   // Vertex 3: UV(0, H)
                    };
                    const uint8_t* cornerMap = isYFace ? yFaceCorners : sideCorners;

                    // Create 4 vertices for this face (corners of the quad)
                    int vertexIndex = 0;
                    for (int i = cubeStart; i < cubeStart + 12; i += 3, vertexIndex++) {
                        // Scale vertex position for merged quads
                        float vx = cube[i+0];
                        float vy = cube[i+1];
                        float vz = cube[i+2];

                        // Scale based on face orientation
                        if (abs(faceNormal.x) > 0) {
                            // X-facing face: scale Y and Z
                            if (vy > 0.5f) vy *= quadHeight;
                            if (vz > 0.5f) vz *= quadWidth;
                        } else if (abs(faceNormal.y) > 0) {
                            // Y-facing face: scale X and Z
                            if (vx > 0.5f) vx *= quadWidth;
                            if (vz > 0.5f) vz *= quadHeight;
                        } else {
                            // Z-facing face: scale X and Y
                            if (vx > 0.5f) vx *= quadWidth;
                            if (vy > 0.5f) vy *= quadHeight;
                        }

                        // Calculate world position
                        float worldX = vx + blockX;
                        float worldZ = vz + blockZ;
                        float worldY;
                        if (adjustTopOnly) {
                            worldY = vy + blockY + (vy > 0.4f ? heightAdjust : 0.0f);
                        } else {
                            worldY = vy + blockY + heightAdjust;
                        }

                        // Get corner index for this vertex
                        uint8_t cornerIndex = cornerMap[vertexIndex];

                        // Pack and add compressed vertex
                        targetVerts.push_back(CompressedVertex::pack(
                            worldX, worldY, worldZ,
                            normalIndex,
                            qw, qh,
                            atlasX, atlasY,
                            cornerIndex,
                            skyLightInt, blockLightInt, aoInt,
                            colorTint
                        ));
                    }

                    // SIMPLIFIED TRIANGLE SPLIT FOR CLASSIC LIGHTING
                    // Use consistent diagonal (0-2) for all faces
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

                // Front face (z=0, facing -Z direction) - WITH BINARY GREEDY MESHING
                {
                    if (!IS_PROCESSED_NEGZ(X, Y, Z)) {
                        int neighborBlockID = getBlockID(X, Y, Z - 1);
                        bool neighborIsLiquid = isLiquid(X, Y, Z - 1);
                        bool neighborIsSolid = isSolid(X, Y, Z - 1);

                        bool shouldRender;
                        if (isCurrentLiquid) {
                            // Water: ONLY render side faces against air (neighborBlockID == 0)
                            // Don't render against solid blocks (looks weird) or other water (z-fighting)
                            shouldRender = (neighborBlockID == 0);
                        } else if (isCurrentTransparent) {
                            // Transparent blocks (leaves, glass): render unless neighbor is same block type
                            shouldRender = (neighborBlockID != id) && (neighborBlockID != 0);
                        } else {
                            // Solid opaque: render against non-solid (air or water)
                            shouldRender = !neighborIsSolid;
                        }

                        if (shouldRender) {
                            // GREEDY MESHING: Merge in +X and +Y directions
                            int width = 1;
                            int height = 1;

                            if (!isCurrentLiquid && !isCurrentTransparent) {
                                // Extend in +X direction (limited by maxQuadSize for UV tiling)
                                while (X + width < WIDTH && width < maxQuadSize) {
                                    if (IS_PROCESSED_NEGZ(X + width, Y, Z)) break;
                                    if (m_blocks[X + width][Y][Z] != id) break;
                                    if (isSolid(X + width, Y, Z - 1)) break;
                                    width++;
                                }

                                // Extend in +Y direction (limited by maxQuadSize for UV tiling)
                                bool canExtendY = true;
                                while (Y + height < HEIGHT && height < maxQuadSize && canExtendY) {
                                    for (int dx = 0; dx < width; dx++) {
                                        if (IS_PROCESSED_NEGZ(X + dx, Y + height, Z)) { canExtendY = false; break; }
                                        if (m_blocks[X + dx][Y + height][Z] != id) { canExtendY = false; break; }
                                        if (isSolid(X + dx, Y + height, Z - 1)) { canExtendY = false; break; }
                                    }
                                    if (canExtendY) height++;
                                }

                                // Mark all covered blocks as processed (binary OR)
                                for (int dy = 0; dy < height; dy++) {
                                    for (int dx = 0; dx < width; dx++) {
                                        SET_PROCESSED_NEGZ(X + dx, Y + dy, Z);
                                    }
                                }
                            } else {
                                SET_PROCESSED_NEGZ(X, Y, Z);
                            }

                            renderFace(frontTex, 0, 0, waterHeightAdjust, true, isCurrentTransparent,
                                      glm::ivec3(0, 0, -1), float(width), float(height), X, Y, Z);
                        } else {
                            SET_PROCESSED_NEGZ(X, Y, Z);
                        }
                    }
                }

                // Back face (z=0.5, facing +Z direction) - WITH BINARY GREEDY MESHING
                {
                    if (!IS_PROCESSED_POSZ(X, Y, Z)) {
                        int neighborBlockID = getBlockID(X, Y, Z + 1);
                        bool neighborIsLiquid = isLiquid(X, Y, Z + 1);
                        bool neighborIsSolid = isSolid(X, Y, Z + 1);
                        bool shouldRender;
                        if (isCurrentLiquid) {
                            // Water: ONLY render side faces against air (neighborBlockID == 0)
                            // Don't render against solid blocks (looks weird) or other water (z-fighting)
                            shouldRender = (neighborBlockID == 0);
                        } else if (isCurrentTransparent) {
                            shouldRender = (neighborBlockID != id) && (neighborBlockID != 0);
                        } else {
                            shouldRender = !neighborIsSolid;
                        }

                        if (shouldRender) {
                            // GREEDY MESHING: Merge in +X and +Y directions
                            int width = 1;
                            int height = 1;

                            if (!isCurrentLiquid && !isCurrentTransparent) {
                                // Extend in +X direction
                                while (X + width < WIDTH && width < maxQuadSize) {
                                    if (IS_PROCESSED_POSZ(X + width, Y, Z)) break;
                                    if (m_blocks[X + width][Y][Z] != id) break;
                                    if (isSolid(X + width, Y, Z + 1)) break;
                                    width++;
                                }

                                // Extend in +Y direction
                                bool canExtendY = true;
                                while (Y + height < HEIGHT && height < maxQuadSize && canExtendY) {
                                    for (int dx = 0; dx < width; dx++) {
                                        if (IS_PROCESSED_POSZ(X + dx, Y + height, Z)) { canExtendY = false; break; }
                                        if (m_blocks[X + dx][Y + height][Z] != id) { canExtendY = false; break; }
                                        if (isSolid(X + dx, Y + height, Z + 1)) { canExtendY = false; break; }
                                    }
                                    if (canExtendY) height++;
                                }

                                // Mark all covered blocks as processed (binary OR)
                                for (int dy = 0; dy < height; dy++) {
                                    for (int dx = 0; dx < width; dx++) {
                                        SET_PROCESSED_POSZ(X + dx, Y + dy, Z);
                                    }
                                }
                            } else {
                                SET_PROCESSED_POSZ(X, Y, Z);
                            }

                            renderFace(backTex, 12, 8, waterHeightAdjust, true, isCurrentTransparent,
                                      glm::ivec3(0, 0, 1), float(width), float(height), X, Y, Z);
                        } else {
                            SET_PROCESSED_POSZ(X, Y, Z);
                        }
                    }
                }

                // Left face (x=0, facing -X direction) - WITH BINARY GREEDY MESHING
                {
                    if (!IS_PROCESSED_NEGX(X, Y, Z)) {
                        int neighborBlockID = getBlockID(X - 1, Y, Z);
                        bool neighborIsLiquid = isLiquid(X - 1, Y, Z);
                        bool neighborIsSolid = isSolid(X - 1, Y, Z);
                        bool shouldRender;
                        if (isCurrentLiquid) {
                            // Water: ONLY render side faces against air (neighborBlockID == 0)
                            // Don't render against solid blocks (looks weird) or other water (z-fighting)
                            shouldRender = (neighborBlockID == 0);
                        } else if (isCurrentTransparent) {
                            shouldRender = (neighborBlockID != id) && (neighborBlockID != 0);
                        } else {
                            shouldRender = !neighborIsSolid;
                        }

                        if (shouldRender) {
                            // GREEDY MESHING: Merge in +Z and +Y directions
                            int width = 1;
                            int height = 1;

                            if (!isCurrentLiquid && !isCurrentTransparent) {
                                // Extend in +Z direction (limited by maxQuadSize for UV tiling)
                                while (Z + width < DEPTH && width < maxQuadSize) {
                                    if (IS_PROCESSED_NEGX(X, Y, Z + width)) break;
                                    if (m_blocks[X][Y][Z + width] != id) break;
                                    if (isSolid(X - 1, Y, Z + width)) break;
                                    width++;
                                }

                                // Extend in +Y direction (limited by maxQuadSize for UV tiling)
                                bool canExtendY = true;
                                while (Y + height < HEIGHT && height < maxQuadSize && canExtendY) {
                                    for (int dz = 0; dz < width; dz++) {
                                        if (IS_PROCESSED_NEGX(X, Y + height, Z + dz)) { canExtendY = false; break; }
                                        if (m_blocks[X][Y + height][Z + dz] != id) { canExtendY = false; break; }
                                        if (isSolid(X - 1, Y + height, Z + dz)) { canExtendY = false; break; }
                                    }
                                    if (canExtendY) height++;
                                }

                                // Mark all covered blocks as processed (binary OR)
                                for (int dy = 0; dy < height; dy++) {
                                    for (int dz = 0; dz < width; dz++) {
                                        SET_PROCESSED_NEGX(X, Y + dy, Z + dz);
                                    }
                                }
                            } else {
                                SET_PROCESSED_NEGX(X, Y, Z);
                            }

                            renderFace(leftTex, 24, 16, waterHeightAdjust, true, isCurrentTransparent,
                                      glm::ivec3(-1, 0, 0), float(width), float(height), X, Y, Z);
                        } else {
                            SET_PROCESSED_NEGX(X, Y, Z);
                        }
                    }
                }

                // Right face (x=0.5, facing +X direction) - WITH BINARY GREEDY MESHING
                {
                    if (!IS_PROCESSED_POSX(X, Y, Z)) {
                        int neighborBlockID = getBlockID(X + 1, Y, Z);
                        bool neighborIsLiquid = isLiquid(X + 1, Y, Z);
                        bool neighborIsSolid = isSolid(X + 1, Y, Z);
                        bool shouldRender;
                        if (isCurrentLiquid) {
                            // Water: ONLY render side faces against air (neighborBlockID == 0)
                            // Don't render against solid blocks (looks weird) or other water (z-fighting)
                            shouldRender = (neighborBlockID == 0);
                        } else if (isCurrentTransparent) {
                            shouldRender = (neighborBlockID != id) && (neighborBlockID != 0);
                        } else {
                            shouldRender = !neighborIsSolid;
                        }

                        if (shouldRender) {
                            // GREEDY MESHING: Merge in +Z and +Y directions
                            int width = 1;
                            int height = 1;

                            if (!isCurrentLiquid && !isCurrentTransparent) {
                                // Extend in +Z direction (limited by maxQuadSize for UV tiling)
                                while (Z + width < DEPTH && width < maxQuadSize) {
                                    if (IS_PROCESSED_POSX(X, Y, Z + width)) break;
                                    if (m_blocks[X][Y][Z + width] != id) break;
                                    if (isSolid(X + 1, Y, Z + width)) break;
                                    width++;
                                }

                                // Extend in +Y direction (limited by maxQuadSize for UV tiling)
                                bool canExtendY = true;
                                while (Y + height < HEIGHT && height < maxQuadSize && canExtendY) {
                                    for (int dz = 0; dz < width; dz++) {
                                        if (IS_PROCESSED_POSX(X, Y + height, Z + dz)) { canExtendY = false; break; }
                                        if (m_blocks[X][Y + height][Z + dz] != id) { canExtendY = false; break; }
                                        if (isSolid(X + 1, Y + height, Z + dz)) { canExtendY = false; break; }
                                    }
                                    if (canExtendY) height++;
                                }

                                // Mark all covered blocks as processed (binary OR)
                                for (int dy = 0; dy < height; dy++) {
                                    for (int dz = 0; dz < width; dz++) {
                                        SET_PROCESSED_POSX(X, Y + dy, Z + dz);
                                    }
                                }
                            } else {
                                SET_PROCESSED_POSX(X, Y, Z);
                            }

                            renderFace(rightTex, 36, 24, waterHeightAdjust, true, isCurrentTransparent,
                                      glm::ivec3(1, 0, 0), float(width), float(height), X, Y, Z);
                        } else {
                            SET_PROCESSED_POSX(X, Y, Z);
                        }
                    }
                }

                // Top face (y=0.5, facing +Y direction) - WITH GREEDY MESHING
                {
                    if (!IS_PROCESSED_POSY(X, Y, Z)) {
                        int neighborBlockID = getBlockID(X, Y + 1, Z);
                        bool neighborIsLiquid = isLiquid(X, Y + 1, Z);
                        bool neighborIsSolid = isSolid(X, Y + 1, Z);
                        bool shouldRender;
                        if (isCurrentLiquid) {
                            // Water: ONLY render top face against air (neighborBlockID == 0)
                            // Don't render against solid blocks or other water
                            shouldRender = (neighborBlockID == 0);
                        } else if (isCurrentTransparent) {
                            shouldRender = (neighborBlockID != id) && (neighborBlockID != 0);
                        } else {
                            shouldRender = !neighborIsSolid;
                        }

                        if (shouldRender) {
                            // GREEDY MESHING: Try to extend this face horizontally and vertically
                            // Only merge solid opaque blocks (skip liquids and transparent)
                            int width = 1;
                            int height = 1;

                            if (!isCurrentLiquid && !isCurrentTransparent) {
                                // Extend in +X direction (limited by maxQuadSize for UV tiling)
                                while (X + width < WIDTH && width < maxQuadSize) {
                                    if (IS_PROCESSED_POSY(X + width, Y, Z)) break;
                                    if (m_blocks[X + width][Y][Z] != id) break;
                                    if (isSolid(X + width, Y + 1, Z)) break; // Neighbor is solid
                                    width++;
                                }

                                // Extend in +Z direction (limited by maxQuadSize for UV tiling)
                                bool canExtendZ = true;
                                while (Z + height < DEPTH && height < maxQuadSize && canExtendZ) {
                                    // Check if entire row can be extended
                                    for (int dx = 0; dx < width; dx++) {
                                        if (IS_PROCESSED_POSY(X + dx, Y, Z + height)) { canExtendZ = false; break; }
                                        if (m_blocks[X + dx][Y][Z + height] != id) { canExtendZ = false; break; }
                                        if (isSolid(X + dx, Y + 1, Z + height)) { canExtendZ = false; break; }
                                    }
                                    if (canExtendZ) height++;
                                }

                                // Mark all covered blocks as processed (binary OR)
                                for (int dz = 0; dz < height; dz++) {
                                    for (int dx = 0; dx < width; dx++) {
                                        SET_PROCESSED_POSY(X + dx, Y, Z + dz);
                                    }
                                }
                            } else {
                                // Transparent/liquid blocks: don't merge, just mark as processed
                                SET_PROCESSED_POSY(X, Y, Z);
                            }

                            // Render the merged quad
                            renderFace(topTex, 48, 32, waterHeightAdjust, false, isCurrentTransparent,
                                      glm::ivec3(0, 1, 0), float(width), float(height), X, Y, Z);
                        } else {
                            SET_PROCESSED_POSY(X, Y, Z); // Mark as processed even if not rendered
                        }
                    }
                }

                // Bottom face (y=0, facing -Y direction) - WITH GREEDY MESHING
                {
                    if (!IS_PROCESSED_NEGY(X, Y, Z)) {
                        int neighborBlockID = getBlockID(X, Y - 1, Z);
                        bool neighborIsLiquid = isLiquid(X, Y - 1, Z);
                        bool neighborIsSolid = isSolid(X, Y - 1, Z);
                        bool shouldRender;
                        if (isCurrentLiquid) {
                            // Water: ONLY render bottom face against air (neighborBlockID == 0)
                            // Don't render against solid blocks or other water
                            shouldRender = (neighborBlockID == 0);
                        } else if (isCurrentTransparent) {
                            shouldRender = (neighborBlockID != id) && (neighborBlockID != 0);
                        } else {
                            // Solid: render against air/water
                            shouldRender = !neighborIsSolid;
                        }

                        if (shouldRender) {
                            // GREEDY MESHING: Merge in +X and +Z directions
                            int width = 1;
                            int height = 1;

                            if (!isCurrentLiquid && !isCurrentTransparent) {
                                // Extend in +X direction (limited by maxQuadSize for UV tiling)
                                while (X + width < WIDTH && width < maxQuadSize) {
                                    if (IS_PROCESSED_NEGY(X + width, Y, Z)) break;
                                    if (m_blocks[X + width][Y][Z] != id) break;
                                    if (isSolid(X + width, Y - 1, Z)) break;
                                    width++;
                                }

                                // Extend in +Z direction (limited by maxQuadSize for UV tiling)
                                bool canExtendZ = true;
                                while (Z + height < DEPTH && height < maxQuadSize && canExtendZ) {
                                    for (int dx = 0; dx < width; dx++) {
                                        if (IS_PROCESSED_NEGY(X + dx, Y, Z + height)) { canExtendZ = false; break; }
                                        if (m_blocks[X + dx][Y][Z + height] != id) { canExtendZ = false; break; }
                                        if (isSolid(X + dx, Y - 1, Z + height)) { canExtendZ = false; break; }
                                    }
                                    if (canExtendZ) height++;
                                }

                                // Mark all covered blocks as processed (binary OR)
                                for (int dz = 0; dz < height; dz++) {
                                    for (int dx = 0; dx < width; dx++) {
                                        SET_PROCESSED_NEGY(X + dx, Y, Z + dz);
                                    }
                                }
                            } else {
                                SET_PROCESSED_NEGY(X, Y, Z);
                            }

                            renderFace(bottomTex, 60, 40, 0.0f, false, isCurrentTransparent,
                                      glm::ivec3(0, -1, 0), float(width), float(height), X, Y, Z);
                        } else {
                            SET_PROCESSED_NEGY(X, Y, Z);
                        }
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
        VkDeviceSize vertexBufferSize = sizeof(CompressedVertex) * m_vertices.size();

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
        VkDeviceSize vertexBufferSize = sizeof(CompressedVertex) * m_transparentVertices.size();

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

#if USE_INDIRECT_DRAWING
    // INDIRECT DRAWING: Chunks don't own individual GPU buffers!
    // They only write to mega-buffers, so there's nothing to destroy here.
    // This makes chunk unloading nearly instant (no GPU synchronization needed).
    return;
#else
    // LEGACY PATH: Queue individual chunk buffers for deletion
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
#endif
}

void Chunk::createVertexBufferBatched(VulkanRenderer* renderer) {
    if (m_vertexCount == 0 && m_transparentVertexCount == 0) {
        return;  // No vertices to upload
    }

#if USE_INDIRECT_DRAWING
    // ========== INDIRECT DRAWING PATH (MEGA-BUFFER) ==========
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

    // ========== ALLOCATE AND UPLOAD OPAQUE GEOMETRY ==========
    if (m_vertexCount > 0) {
        VkDeviceSize vertexBufferSize = sizeof(CompressedVertex) * m_vertices.size();
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_indices.size();

        // OPTIMIZATION: Only allocate new space if we don't already have space
        // This prevents memory leaks when chunks regenerate meshes (lighting updates, etc.)
        bool needsNewAllocation = (m_megaBufferVertexOffset == 0 && m_megaBufferIndexOffset == 0);

        if (needsNewAllocation) {
            // Allocate space in mega-buffer
            if (!renderer->allocateMegaBufferSpace(vertexBufferSize, indexBufferSize, false,
                                                    m_megaBufferVertexOffset, m_megaBufferIndexOffset)) {
                Logger::error() << "Failed to allocate mega-buffer space for chunk at ("
                               << m_x << ", " << m_z << ")";
                return;
            }

            // Calculate base vertex for indexed drawing
            m_megaBufferBaseVertex = static_cast<uint32_t>(m_megaBufferVertexOffset / sizeof(CompressedVertex));
        }
        // else: Reuse existing allocation (chunk is updating its mesh)

        // Create staging buffers
        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_vertexStagingBuffer, m_vertexStagingBufferMemory);

        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_indexStagingBuffer, m_indexStagingBufferMemory);

        // Copy data to staging buffers
        void* data;
        vkMapMemory(device, m_vertexStagingBufferMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, m_vertices.data(), (size_t)vertexBufferSize);
        vkUnmapMemory(device, m_vertexStagingBufferMemory);

        vkMapMemory(device, m_indexStagingBufferMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, m_indices.data(), (size_t)indexBufferSize);
        vkUnmapMemory(device, m_indexStagingBufferMemory);

        // Record batched copy to mega-buffer
        renderer->batchCopyToMegaBuffer(m_vertexStagingBuffer, m_indexStagingBuffer,
                                       vertexBufferSize, indexBufferSize,
                                       m_megaBufferVertexOffset, m_megaBufferIndexOffset,
                                       false);
    }

    // ========== ALLOCATE AND UPLOAD TRANSPARENT GEOMETRY ==========
    if (m_transparentVertexCount > 0) {
        VkDeviceSize vertexBufferSize = sizeof(CompressedVertex) * m_transparentVertices.size();
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_transparentIndices.size();

        // OPTIMIZATION: Only allocate new space if we don't already have space
        // This prevents memory leak when chunks regenerate meshes for lighting updates
        bool needsNewAllocation = (m_megaBufferTransparentVertexOffset == 0 &&
                                   m_megaBufferTransparentIndexOffset == 0);

        if (needsNewAllocation) {
            // Allocate space in transparent mega-buffer
            if (!renderer->allocateMegaBufferSpace(vertexBufferSize, indexBufferSize, true,
                                                    m_megaBufferTransparentVertexOffset,
                                                    m_megaBufferTransparentIndexOffset)) {
                Logger::error() << "Failed to allocate transparent mega-buffer space for chunk at ("
                               << m_x << ", " << m_z << ")";
                return;
            }

            // Calculate base vertex for transparent indexed drawing
            m_megaBufferTransparentBaseVertex = static_cast<uint32_t>(m_megaBufferTransparentVertexOffset / sizeof(CompressedVertex));
        }
        // else: Reuse existing allocation (chunk is updating its mesh)

        // Create staging buffers
        renderer->createBuffer(vertexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_transparentVertexStagingBuffer, m_transparentVertexStagingBufferMemory);

        renderer->createBuffer(indexBufferSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              m_transparentIndexStagingBuffer, m_transparentIndexStagingBufferMemory);

        // Copy data to staging buffers
        void* data;
        vkMapMemory(device, m_transparentVertexStagingBufferMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, m_transparentVertices.data(), (size_t)vertexBufferSize);
        vkUnmapMemory(device, m_transparentVertexStagingBufferMemory);

        vkMapMemory(device, m_transparentIndexStagingBufferMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, m_transparentIndices.data(), (size_t)indexBufferSize);
        vkUnmapMemory(device, m_transparentIndexStagingBufferMemory);

        // Record batched copy to transparent mega-buffer
        renderer->batchCopyToMegaBuffer(m_transparentVertexStagingBuffer, m_transparentIndexStagingBuffer,
                                       vertexBufferSize, indexBufferSize,
                                       m_megaBufferTransparentVertexOffset, m_megaBufferTransparentIndexOffset,
                                       true);
    }

#else
    // ========== LEGACY PATH (PER-CHUNK BUFFERS) ==========
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
        VkDeviceSize vertexBufferSize = sizeof(CompressedVertex) * m_vertices.size();

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
        VkDeviceSize vertexBufferSize = sizeof(CompressedVertex) * m_transparentVertices.size();

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
#endif

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
    // THREAD SAFETY (2025-11-23): Lock for concurrent reads during parallel mesh generation
    std::lock_guard<std::mutex> lock(m_blockDataMutex);
    return m_blocks[x][y][z];
}

void Chunk::setBlock(int x, int y, int z, int blockID) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return;  // Out of bounds
    }
    // THREAD SAFETY (2025-11-23): Lock for concurrent writes during parallel decoration
    std::lock_guard<std::mutex> lock(m_blockDataMutex);
    m_blocks[x][y][z] = blockID;

    // PERFORMANCE: Invalidate isEmpty cache (will be recomputed lazily)
    m_isEmptyValid = false;

    // PERFORMANCE: Update heightmap for fast sky light calculation
    // Only update if this block change might affect the highest block in the column
    updateHeightAt(x, z);
}

uint8_t Chunk::getBlockMetadata(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0;  // Out of bounds
    }
    // THREAD SAFETY (2025-11-23): Lock for concurrent reads
    std::lock_guard<std::mutex> lock(m_blockDataMutex);
    return m_blockMetadata[x][y][z];
}

void Chunk::setBlockMetadata(int x, int y, int z, uint8_t metadata) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return;  // Out of bounds
    }
    // THREAD SAFETY (2025-11-23): Lock for concurrent writes (water level changes)
    std::lock_guard<std::mutex> lock(m_blockDataMutex);
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
    // PERFORMANCE OPTIMIZATION (2025-11-23): Return direct lighting value (no interpolation)
    // Interpolation was causing 40-80M operations/sec on lower-end hardware
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0.0f;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    return static_cast<float>(m_lightData[index].skyLight);
}

float Chunk::getInterpolatedBlockLight(int x, int y, int z) const {
    // PERFORMANCE OPTIMIZATION (2025-11-23): Return direct lighting value (no interpolation)
    // Interpolation was causing 40-80M operations/sec on lower-end hardware
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return 0.0f;  // Out of bounds
    }
    int index = x + y * WIDTH + z * WIDTH * HEIGHT;
    return static_cast<float>(m_lightData[index].blockLight);
}

// ========== Lazy Interpolated Lighting (Memory Optimization 2025-11-25) ==========

void Chunk::ensureInterpolatedLightingAllocated() {
    if (!m_interpolatedLightData) {
        m_interpolatedLightData = std::make_unique<InterpolatedLightArray>();
        // Initialize to match target values (prevents fade-in)
        for (int i = 0; i < WIDTH * HEIGHT * DEPTH; ++i) {
            (*m_interpolatedLightData)[i].skyLight = static_cast<float>(m_lightData[i].skyLight);
            (*m_interpolatedLightData)[i].blockLight = static_cast<float>(m_lightData[i].blockLight);
        }
    }
}

void Chunk::deallocateInterpolatedLighting() {
    m_interpolatedLightData.reset();
}

void Chunk::updateInterpolatedLighting(float deltaTime, float speed) {
    // MEMORY OPTIMIZATION: Only update if allocated (visible chunk)
    if (!m_interpolatedLightData) return;

    // Smoothly interpolate current lighting toward target lighting values
    // This creates natural, gradual lighting transitions over time
    const float lerpFactor = 1.0f - std::exp(-speed * deltaTime);  // Exponential smoothing

    for (int i = 0; i < WIDTH * HEIGHT * DEPTH; ++i) {
        const BlockLight& target = m_lightData[i];
        InterpolatedLight& current = (*m_interpolatedLightData)[i];

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
    // MEMORY OPTIMIZATION: Lazy allocate on first use
    ensureInterpolatedLightingAllocated();

    // Initialize interpolated values to match target values immediately
    // This prevents fade-in effect when chunks are first loaded
    for (int i = 0; i < WIDTH * HEIGHT * DEPTH; ++i) {
        const BlockLight& target = m_lightData[i];
        InterpolatedLight& current = (*m_interpolatedLightData)[i];

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

// ========== Heightmap Implementation (Fast Sky Light) ==========

void Chunk::updateHeightAt(int x, int z) {
    if (x < 0 || x >= WIDTH || z < 0 || z >= DEPTH) return;

    // Scan from top to bottom to find highest OPAQUE block
    // BUG FIX: Must check transparency to avoid treating water/ice/leaves as solid
    int16_t highestY = -1;
    for (int y = HEIGHT - 1; y >= 0; y--) {
        int blockID = m_blocks[x][y][z];
        if (blockID != 0) {  // Not air
            // Check if block is opaque (blocks sunlight)
            auto& registry = BlockRegistry::instance();
            if (blockID >= 0 && blockID < registry.count()) {
                const BlockDefinition& blockDef = registry.get(blockID);
                // Only FULLY opaque blocks (transparency == 0) should block sunlight
                // This allows water, ice, glass, and leaves to let sunlight through
                // Minecraft-style: transparent blocks don't stop sunlight column
                if (blockDef.transparency == 0.0f) {
                    highestY = static_cast<int16_t>(y);
                    break;
                }
            } else {
                // Invalid block ID - treat as opaque to be safe
                highestY = static_cast<int16_t>(y);
                break;
            }
        }
    }

    m_heightMap[x * DEPTH + z] = highestY;
}

void Chunk::rebuildHeightMap() {
    // Rebuild entire heightmap by scanning all columns
    for (int x = 0; x < WIDTH; x++) {
        for (int z = 0; z < DEPTH; z++) {
            updateHeightAt(x, z);
        }
    }
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
 * @brief Compresses lighting data using Run-Length Encoding
 *
 * LIGHTING PERSISTENCE OPTIMIZATION (2025-11-23):
 * Saves lighting data to disk to eliminate 3-5 second recalculation on world load!
 * BlockLight is 1 byte (4 bits sky + 4 bits block), highly compressible with RLE.
 * Typical compression: 32 KB → 1-3 KB (90%+ reduction)
 */
void Chunk::compressLighting(std::vector<uint8_t>& output) const {
    output.clear();

    // BlockLight is 1 byte, so we can treat it as uint8_t
    uint8_t currentValue = *reinterpret_cast<const uint8_t*>(&m_lightData[0]);
    uint32_t runLength = 1;

    for (size_t idx = 1; idx < m_lightData.size(); idx++) {
        uint8_t value = *reinterpret_cast<const uint8_t*>(&m_lightData[idx]);

        if (value == currentValue && runLength < UINT32_MAX) {
            runLength++;
        } else {
            // Write run: [value (1 byte), count (4 bytes)]
            output.push_back(currentValue);
            output.insert(output.end(), reinterpret_cast<const uint8_t*>(&runLength),
                         reinterpret_cast<const uint8_t*>(&runLength) + sizeof(uint32_t));

            currentValue = value;
            runLength = 1;
        }
    }

    // Write final run
    output.push_back(currentValue);
    output.insert(output.end(), reinterpret_cast<const uint8_t*>(&runLength),
                 reinterpret_cast<const uint8_t*>(&runLength) + sizeof(uint32_t));
}

/**
 * @brief Decompresses lighting data from Run-Length Encoding
 */
bool Chunk::decompressLighting(const std::vector<uint8_t>& input) {
    size_t offset = 0;
    size_t blockIndex = 0;
    const size_t totalBlocks = m_lightData.size();

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

        // Write run to lighting array
        for (uint32_t c = 0; c < count && blockIndex < totalBlocks; c++) {
            *reinterpret_cast<uint8_t*>(&m_lightData[blockIndex]) = value;
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
            // OPTIMIZATION: Use ostringstream instead of string concatenation (3x faster, single allocation)
            std::ostringstream oss;
            oss << "chunk_" << m_x << "_" << m_y << "_" << m_z << ".dat";
            fs::path filepath = chunksDir / oss.str();
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
        // OPTIMIZATION: Use ostringstream instead of string concatenation (3x faster, single allocation)
        std::ostringstream oss;
        oss << "chunk_" << m_x << "_" << m_y << "_" << m_z << ".dat";
        fs::path filepath = chunksDir / oss.str();

        // Open file for binary writing
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // RLE COMPRESSION: Compress block, metadata, and lighting data before writing
        std::vector<uint8_t> compressedBlocks, compressedMetadata, compressedLighting;
        compressBlocks(compressedBlocks);
        compressMetadata(compressedMetadata);
        compressLighting(compressedLighting);

        // Write header (version 3 with RLE compression + LIGHTING PERSISTENCE!)
        constexpr uint32_t CHUNK_FILE_VERSION = 3;
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

        // Write compressed lighting data size + data (variable size, typically 1-3 KB instead of 32 KB!)
        uint32_t lightingSize = static_cast<uint32_t>(compressedLighting.size());
        file.write(reinterpret_cast<const char*>(&lightingSize), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(compressedLighting.data()), lightingSize);

        file.close();

        Logger::debug() << "Saved chunk (" << m_x << ", " << m_y << ", " << m_z << ") with RLE+LIGHTING: "
                       << blockDataSize << " bytes blocks, " << metadataSize << " bytes metadata, "
                       << lightingSize << " bytes lighting (was 196608 bytes uncompressed)";
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
        // OPTIMIZATION: Use ostringstream instead of string concatenation (3x faster, single allocation)
        std::ostringstream oss;
        oss << "chunk_" << m_x << "_" << m_y << "_" << m_z << ".dat";
        fs::path filepath = chunksDir / oss.str();

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

            // FIXED (2025-11-23): Mark loaded chunks as NOT needing decoration
            // Prevents overwriting player edits when chunks reload
            m_needsDecoration = false;
            return true;

        } else if (version == 2) {
            // RLE COMPRESSED FORMAT (NO LIGHTING): Read compressed data and decompress

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

            // NOTE: Version 2 doesn't have lighting data, so caller must initialize lighting!
            Logger::debug() << "Loaded chunk (" << m_x << ", " << m_y << ", " << m_z << ") from RLE format v2 ("
                           << blockDataSize << "+" << metadataSize << " bytes) - lighting will be calculated";

            // FIXED (2025-11-23): Mark loaded chunks as NOT needing decoration
            m_needsDecoration = false;
            return true;

        } else if (version == 3) {
            // RLE COMPRESSED FORMAT WITH LIGHTING PERSISTENCE: Read all compressed data

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

            // Read compressed lighting data (NEW IN VERSION 3!)
            uint32_t lightingSize;
            file.read(reinterpret_cast<char*>(&lightingSize), sizeof(uint32_t));
            std::vector<uint8_t> compressedLighting(lightingSize);
            file.read(reinterpret_cast<char*>(compressedLighting.data()), lightingSize);

            file.close();

            // Decompress all data
            if (!decompressBlocks(compressedBlocks)) {
                Logger::error() << "Failed to decompress block data for chunk (" << m_x << ", " << m_y << ", " << m_z << ")";
                return false;
            }
            if (!decompressMetadata(compressedMetadata)) {
                Logger::error() << "Failed to decompress metadata for chunk (" << m_x << ", " << m_y << ", " << m_z << ")";
                return false;
            }
            if (!decompressLighting(compressedLighting)) {
                Logger::error() << "Failed to decompress lighting data for chunk (" << m_x << ", " << m_y << ", " << m_z << ")";
                return false;
            }

            // SUCCESS: Chunk loaded with lighting! No need for lighting recalculation!
            Logger::debug() << "Loaded chunk (" << m_x << ", " << m_y << ", " << m_z << ") from RLE format v3 WITH LIGHTING ("
                           << blockDataSize << "+" << metadataSize << "+" << lightingSize << " bytes) - instant lighting!";

            // FIXED (2025-11-23): Mark loaded chunks as NOT needing decoration
            m_needsDecoration = false;
            // PERFORMANCE FIX (2025-11-23): Mark that chunk has lighting data to skip re-initialization
            // This prevents double mesh generation for loaded chunks with lighting
            m_hasLightingData = true;
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
