/**
 * @file chunk_face_config.h
 * @brief Data-driven face configuration for chunk mesh generation
 *
 * This header provides configuration data for the 6 cube faces used in
 * greedy meshing. It replaces 6 nearly-identical code blocks (~360 lines)
 * with a single parameterized implementation.
 *
 * Usage:
 *   for (const auto& face : FACE_CONFIGS) {
 *       processFace(x, y, z, face, blockId, def);
 *   }
 *
 * Future refactoring: The chunk mesh generation in chunk.cpp can be updated
 * to use these configurations to reduce code duplication.
 */

#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

/**
 * @brief Face direction enumeration matching CompressedVertex normal indices
 */
enum class FaceDirection : uint8_t {
    PosX = 0,  // Right face (+X)
    NegX = 1,  // Left face (-X)
    PosY = 2,  // Top face (+Y)
    NegY = 3,  // Bottom face (-Y)
    PosZ = 4,  // Back face (+Z)
    NegZ = 5   // Front face (-Z)
};

/**
 * @brief Configuration for a single cube face
 *
 * Contains all the parameters needed to process a face during mesh generation:
 * - Normal direction for face culling and lighting
 * - Axes for greedy meshing extension
 * - Vertex data offsets
 * - Processed bitmask for tracking
 */
struct FaceConfig {
    // Face identification
    FaceDirection direction;        ///< Which face this is
    uint8_t normalIndex;            ///< Index for CompressedVertex (0-5)

    // Normal vector for neighbor block queries
    glm::ivec3 normal;              ///< Direction to check for neighbor block

    // Greedy meshing extension axes
    // For each face, we extend in two perpendicular directions
    glm::ivec3 extendAxis1;         ///< First axis to extend along
    glm::ivec3 extendAxis2;         ///< Second axis to extend along

    // Vertex data offsets into cube/UV arrays
    int cubeVertexOffset;           ///< Offset into cube vertex array (0, 12, 24, ...)
    int uvOffset;                   ///< Offset into UV array (0, 8, 16, ...)

    // Bitmask index for tracking processed faces
    // Each face uses a separate bitmask to track which blocks have been processed
    uint8_t maskIndex;              ///< 0-5, corresponding to face direction

    // Whether this is a side face (needs V-flip for UVs)
    bool isSideFace;                ///< true for +-X, +-Z faces

    // Corner indices for vertex generation (order matters for winding)
    std::array<uint8_t, 4> cornerOrder;  ///< Order of corners for this face
};

/**
 * @brief Static configuration for all 6 cube faces
 *
 * Order matches CompressedVertex normal indices:
 * 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
 */
static constexpr std::array<FaceConfig, 6> FACE_CONFIGS = {{
    // Right face (+X direction)
    {
        FaceDirection::PosX,
        0,                          // normalIndex
        {+1, 0, 0},                 // normal
        {0, 0, 1},                  // extendAxis1 (+Z)
        {0, 1, 0},                  // extendAxis2 (+Y)
        36,                         // cubeVertexOffset (Right face in cube array)
        24,                         // uvOffset
        0,                          // maskIndex
        true,                       // isSideFace
        {2, 3, 0, 1}               // cornerOrder (HEIGHT, BOTH, ORIGIN, WIDTH for V-flip)
    },
    // Left face (-X direction)
    {
        FaceDirection::NegX,
        1,                          // normalIndex
        {-1, 0, 0},                 // normal
        {0, 0, 1},                  // extendAxis1 (+Z)
        {0, 1, 0},                  // extendAxis2 (+Y)
        24,                         // cubeVertexOffset (Left face in cube array)
        16,                         // uvOffset
        1,                          // maskIndex
        true,                       // isSideFace
        {2, 3, 0, 1}               // cornerOrder
    },
    // Top face (+Y direction)
    {
        FaceDirection::PosY,
        2,                          // normalIndex
        {0, +1, 0},                 // normal
        {1, 0, 0},                  // extendAxis1 (+X)
        {0, 0, 1},                  // extendAxis2 (+Z)
        48,                         // cubeVertexOffset (Top face in cube array)
        32,                         // uvOffset
        2,                          // maskIndex
        false,                      // isSideFace (top/bottom use standard UVs)
        {0, 1, 3, 2}               // cornerOrder (ORIGIN, WIDTH, HEIGHT, BOTH)
    },
    // Bottom face (-Y direction)
    {
        FaceDirection::NegY,
        3,                          // normalIndex
        {0, -1, 0},                 // normal
        {1, 0, 0},                  // extendAxis1 (+X)
        {0, 0, 1},                  // extendAxis2 (+Z)
        60,                         // cubeVertexOffset (Bottom face in cube array)
        40,                         // uvOffset
        3,                          // maskIndex
        false,                      // isSideFace
        {0, 1, 3, 2}               // cornerOrder
    },
    // Back face (+Z direction)
    {
        FaceDirection::PosZ,
        4,                          // normalIndex
        {0, 0, +1},                 // normal
        {1, 0, 0},                  // extendAxis1 (+X)
        {0, 1, 0},                  // extendAxis2 (+Y)
        12,                         // cubeVertexOffset (Back face in cube array)
        8,                          // uvOffset
        4,                          // maskIndex
        true,                       // isSideFace
        {2, 3, 0, 1}               // cornerOrder
    },
    // Front face (-Z direction)
    {
        FaceDirection::NegZ,
        5,                          // normalIndex
        {0, 0, -1},                 // normal
        {1, 0, 0},                  // extendAxis1 (+X)
        {0, 1, 0},                  // extendAxis2 (+Y)
        0,                          // cubeVertexOffset (Front face in cube array)
        0,                          // uvOffset
        5,                          // maskIndex
        true,                       // isSideFace
        {2, 3, 0, 1}               // cornerOrder
    }
}};

/**
 * @brief Get face configuration by direction
 * @param dir Face direction
 * @return Reference to face configuration
 */
inline const FaceConfig& getFaceConfig(FaceDirection dir) {
    return FACE_CONFIGS[static_cast<size_t>(dir)];
}

/**
 * @brief Get face configuration by normal index (0-5)
 * @param normalIndex Normal index (same as CompressedVertex)
 * @return Reference to face configuration
 */
inline const FaceConfig& getFaceConfigByNormal(uint8_t normalIndex) {
    return FACE_CONFIGS[normalIndex];
}

/**
 * @brief Check if a face should render based on neighbor block
 *
 * Encapsulates the logic for determining if a face should be rendered:
 * - Solid blocks: render if neighbor is not solid
 * - Liquid blocks: render only if neighbor is air
 * - Transparent blocks: render if neighbor is different block type
 *
 * @param isCurrentLiquid Whether current block is liquid
 * @param isCurrentTransparent Whether current block has transparency
 * @param currentBlockId Current block ID
 * @param neighborBlockId Neighbor block ID
 * @param neighborIsSolid Whether neighbor is solid
 * @return True if face should be rendered
 */
inline bool shouldRenderFace(bool isCurrentLiquid, bool isCurrentTransparent,
                              int currentBlockId, int neighborBlockId, bool neighborIsSolid) {
    if (isCurrentLiquid) {
        // Water: only render against air
        return (neighborBlockId == 0);
    } else if (isCurrentTransparent) {
        // Transparent blocks: render unless neighbor is same type
        return (neighborBlockId != currentBlockId) && (neighborBlockId != 0);
    } else {
        // Solid opaque: render against non-solid
        return !neighborIsSolid;
    }
}

/**
 * @brief Calculate greedy mesh extents for a face
 *
 * Extends the face in two perpendicular directions to merge adjacent
 * identical faces. This is the core of greedy meshing.
 *
 * @param startX Starting X coordinate
 * @param startY Starting Y coordinate
 * @param startZ Starting Z coordinate
 * @param face Face configuration
 * @param maxWidth Maximum width to extend
 * @param maxHeight Maximum height to extend
 * @param blockId Block ID to match
 * @param isProcessedFunc Function to check if position is processed
 * @param canExtendFunc Function to check if can extend to position
 * @return Pair of (width, height) for the merged quad
 */
template<typename IsProcessedFn, typename CanExtendFn>
inline std::pair<int, int> calculateGreedyExtents(
    int startX, int startY, int startZ,
    const FaceConfig& face,
    int maxWidth, int maxHeight,
    int blockId,
    IsProcessedFn isProcessedFunc,
    CanExtendFn canExtendFunc)
{
    int width = 1;
    int height = 1;

    const auto& axis1 = face.extendAxis1;
    const auto& axis2 = face.extendAxis2;

    // Extend in first axis direction
    while (width < maxWidth) {
        int nextX = startX + axis1.x * width;
        int nextY = startY + axis1.y * width;
        int nextZ = startZ + axis1.z * width;

        if (isProcessedFunc(nextX, nextY, nextZ, face.maskIndex)) break;
        if (!canExtendFunc(nextX, nextY, nextZ, blockId, face)) break;
        width++;
    }

    // Extend in second axis direction
    bool canExtendHeight = true;
    while (height < maxHeight && canExtendHeight) {
        for (int w = 0; w < width; w++) {
            int checkX = startX + axis1.x * w + axis2.x * height;
            int checkY = startY + axis1.y * w + axis2.y * height;
            int checkZ = startZ + axis1.z * w + axis2.z * height;

            if (isProcessedFunc(checkX, checkY, checkZ, face.maskIndex) ||
                !canExtendFunc(checkX, checkY, checkZ, blockId, face)) {
                canExtendHeight = false;
                break;
            }
        }
        if (canExtendHeight) height++;
    }

    return {width, height};
}
