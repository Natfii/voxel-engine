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
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <mutex>

// Static member initialization
std::unique_ptr<FastNoiseLite> Chunk::s_noise = nullptr;
static std::mutex s_noiseMutex;  // Protect noise access for thread safety

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
    using namespace TerrainGeneration;

    if (s_noise == nullptr) {
        return BASE_HEIGHT; // Fallback to flat terrain if noise not initialized
    }

    // Thread-safe noise sampling (FastNoiseLite may not be thread-safe)
    float noise;
    {
        std::lock_guard<std::mutex> lock(s_noiseMutex);
        noise = s_noise->GetNoise(worldX, worldZ);
    }

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
            // Convert local coords to world coords (blocks are 0.5 units)
            float worldX = (m_x * WIDTH + x) * 0.5f;
            float worldZ = (m_z * DEPTH + z) * 0.5f;

            // Get biome at this position
            const Biome* biome = biomeMap->getBiomeAt(worldX, worldZ);
            if (!biome) {
                // Fallback if no biome available
                continue;
            }

            // Get terrain height from biome map
            int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);

            // Fill column
            for (int y = 0; y < HEIGHT; y++) {
                int worldY = m_y * HEIGHT + y;
                float worldYf = worldY * 0.5f;

                // Check if this is inside a cave
                float caveDensity = biomeMap->getCaveDensityAt(worldX, worldYf, worldZ);
                bool isCave = caveDensity < 0.45f;  // Threshold for cave air

                // Check if inside underground biome chamber
                bool isUndergroundChamber = biomeMap->isUndergroundBiomeAt(worldX, worldYf, worldZ);

                // Determine block placement
                if (worldY < terrainHeight) {
                    // Below surface

                    // If in cave, create air pocket (unless very shallow)
                    if (isCave && worldY < terrainHeight - 5) {
                        m_blocks[x][y][z] = BLOCK_AIR;
                        continue;
                    }

                    // If in underground chamber, create large open space
                    if (isUndergroundChamber && worldY < 40) {
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
                    m_blocks[x][y][z] = BLOCK_WATER;
                    m_blockMetadata[x][y][z] = 0;  // Source block
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
    /**
     * Cube Vertex Layout (indexed rendering):
     * ========================================
     *
     * Each face is a quad defined by 4 vertices (counter-clockwise winding order).
     * Blocks are 0.5 world units in size (1 block = 0.5 units).
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
     *   12-23: Back face (z=0.5, facing +Z)
     *   24-35: Left face (x=0, facing -X)
     *   36-47: Right face (x=0.5, facing +X)
     *   48-59: Top face (y=0.5, facing +Y)
     *   60-71: Bottom face (y=0, facing -Y)
     *
     * Each face offset contains 12 floats (4 vertices × 3 components).
     */
    static constexpr std::array<float, 72> cube = {{
        // Front face (z = 0, facing -Z) - vertices: BL, BR, TR, TL
        0,0,0,  0.5f,0,0,  0.5f,0.5f,0,  0,0.5f,0,
        // Back face (z = 0.5, facing +Z) - vertices: BR, BL, TL, TR (reversed for correct winding)
        0.5f,0,0.5f,  0,0,0.5f,  0,0.5f,0.5f,  0.5f,0.5f,0.5f,
        // Left face (x = 0, facing -X) - vertices: BL, BR, TR, TL
        0,0,0.5f,  0,0,0,  0,0.5f,0,  0,0.5f,0.5f,
        // Right face (x = 0.5, facing +X) - vertices: BL, BR, TR, TL
        0.5f,0,0,  0.5f,0,0.5f,  0.5f,0.5f,0.5f,  0.5f,0.5f,0,
        // Top face (y = 0.5, facing +Y) - vertices: BL, BR, TR, TL
        0,0.5f,0,  0.5f,0.5f,0,  0.5f,0.5f,0.5f,  0,0.5f,0.5f,
        // Bottom face (y = 0, facing -Y) - vertices: BL, BR, TR, TL
        0,0,0.5f,  0.5f,0,0.5f,  0.5f,0,0,  0,0,0
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

    std::vector<Vertex> verts;          // Opaque geometry
    std::vector<uint32_t> indices;
    std::vector<Vertex> transparentVerts;  // Transparent geometry (water, glass, etc.)
    std::vector<uint32_t> transparentIndices;

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
        return glm::vec3(worldBlockX * 0.5f, worldBlockY * 0.5f, worldBlockZ * 0.5f);
    };

    // Helper lambda to check if a block is solid (non-air)
    // THIS VERSION CHECKS NEIGHBORING CHUNKS via World
    auto isSolid = [this, world, &registry, &localToWorldPos](int x, int y, int z) -> bool {
        int blockID;
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            // Inside this chunk
            blockID = m_blocks[x][y][z];
        } else {
            // Out of bounds - check neighboring chunk via World
            glm::vec3 worldPos = localToWorldPos(x, y, z);
            blockID = world->getBlockAt(worldPos.x, worldPos.y, worldPos.z);
        }
        if (blockID == 0) return false;
        return !registry.get(blockID).isLiquid;  // Solid = not air and not liquid
    };

    // Helper lambda to check if a block is liquid
    auto isLiquid = [this, world, &registry, &localToWorldPos](int x, int y, int z) -> bool {
        int blockID;
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
            blockID = m_blocks[x][y][z];
        } else {
            // Out of bounds - check world
            glm::vec3 worldPos = localToWorldPos(x, y, z);
            blockID = world->getBlockAt(worldPos.x, worldPos.y, worldPos.z);
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

                // Water level height adjustment (Minecraft-style flowing water)
                // Level 0 = source (full height), Level 7 = edge (very low)
                float waterHeightAdjust = 0.0f;
                if (def.isLiquid) {
                    uint8_t waterLevel = m_blockMetadata[X][Y][Z];
                    // Each level reduces height by 1/8th of a block (0.0625 world units)
                    waterHeightAdjust = -waterLevel * (0.5f / 8.0f);
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
    }  // End of transparent buffer creation
}

void Chunk::destroyBuffers(VulkanRenderer* renderer) {
    VkDevice device = renderer->getDevice();

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
