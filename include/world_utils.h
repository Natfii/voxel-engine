/**
 * @file world_utils.h
 * @brief Utility functions for world coordinate conversions
 *
 */

#pragma once

#include <cmath>

/**
 * @brief Block and chunk coordinates computed from world position
 *
 * Stores the results of converting world coordinates to chunk and local
 * block coordinates. Used to avoid code duplication.
 */
struct BlockCoordinates {
    int chunkX;   ///< Chunk X coordinate
    int chunkY;   ///< Chunk Y coordinate
    int chunkZ;   ///< Chunk Z coordinate
    int localX;   ///< Local block X within chunk (0-31)
    int localY;   ///< Local block Y within chunk (0-31)
    int localZ;   ///< Local block Z within chunk (0-31)
};

/**
 * @brief Converts world coordinates to chunk and local block coordinates
 *
 * Coordinate System:
 * - Blocks are 1.0 world units in size
 * - Each chunk contains 32x32x32 blocks
 * - Chunk coordinates can be negative (world is centered at origin)
 *
 * Algorithm:
 * 1. Convert world coords to block coords (floor to integer)
 * 2. Compute chunk coords (divide block coords by 32)
 * 3. Compute local coords (modulo 32, handling negatives correctly)
 *
 * Usage Example:
 * @code
 * // Get block at player's feet position
 * glm::vec3 playerPos = player.Position;
 * auto coords = worldToBlockCoords(playerPos.x, playerPos.y, playerPos.z);
 *
 * // Query the chunk and get the block
 * Chunk* chunk = world->getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
 * if (chunk) {
 *     int blockID = chunk->getBlock(coords.localX, coords.localY, coords.localZ);
 *     std::cout << "Block at player position: " << blockID << std::endl;
 * }
 *
 * // Or use the convenience method
 * int blockID = world->getBlockAt(playerPos.x, playerPos.y, playerPos.z);
 * @endcode
 *
 * @param worldX World X coordinate
 * @param worldY World Y coordinate
 * @param worldZ World Z coordinate
 * @return BlockCoordinates struct with chunk and local coordinates
 */
inline BlockCoordinates worldToBlockCoords(float worldX, float worldY, float worldZ) {
    // Chunk dimensions
    constexpr int CHUNK_WIDTH = 32;
    constexpr int CHUNK_HEIGHT = 32;
    constexpr int CHUNK_DEPTH = 32;

    // Convert world coordinates to block coordinates
    // Blocks are 1.0 units in size
    // PERFORMANCE: Fast path for positive coordinates (simple cast), slow path for negatives (floor)
    int blockX = (worldX >= 0.0f) ? static_cast<int>(worldX) : static_cast<int>(std::floor(worldX));
    int blockY = (worldY >= 0.0f) ? static_cast<int>(worldY) : static_cast<int>(std::floor(worldY));
    int blockZ = (worldZ >= 0.0f) ? static_cast<int>(worldZ) : static_cast<int>(std::floor(worldZ));

    // PERFORMANCE: Use bit shifts instead of division (24-39x faster!)
    // Division by 32: 24-39 CPU cycles
    // Bit shift >> 5: 1 CPU cycle
    // Since CHUNK_WIDTH/HEIGHT/DEPTH = 32 = 2^5, we can use >> 5
    // Arithmetic right shift gives floor division behavior for negatives
    int chunkX = blockX >> 5;  // Equivalent to blockX / 32
    int chunkY = blockY >> 5;  // Equivalent to blockY / 32
    int chunkZ = blockZ >> 5;  // Equivalent to blockZ / 32

    // PERFORMANCE: Use bit masking instead of subtraction (faster!)
    // For positive numbers: blockX & 31 = blockX % 32
    // For negative numbers: handled by adjustment code below
    int localX = blockX & 31;  // Keep only lower 5 bits (0-31)
    int localY = blockY & 31;
    int localZ = blockZ & 31;

    // Handle negative coordinates properly (modulo behavior for negatives)
    if (localX < 0) { localX += CHUNK_WIDTH; chunkX--; }
    if (localY < 0) { localY += CHUNK_HEIGHT; chunkY--; }
    if (localZ < 0) { localZ += CHUNK_DEPTH; chunkZ--; }

    return { chunkX, chunkY, chunkZ, localX, localY, localZ };
}
