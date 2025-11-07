/**
 * @file world_utils.h
 * @brief Utility functions for world coordinate conversions
 *
 * Created by Claude (Anthropic AI Assistant) for code cleanup
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
 * - Blocks are 0.5 world units in size
 * - Each chunk contains 32x32x32 blocks
 * - Chunk coordinates can be negative (world is centered at origin)
 *
 * Algorithm:
 * 1. Convert world coords to block coords (divide by 0.5)
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
    // Blocks are 0.5 units in size
    int blockX = static_cast<int>(std::floor(worldX / 0.5f));
    int blockY = static_cast<int>(std::floor(worldY / 0.5f));
    int blockZ = static_cast<int>(std::floor(worldZ / 0.5f));

    // Compute chunk coordinates
    int chunkX = blockX / CHUNK_WIDTH;
    int chunkY = blockY / CHUNK_HEIGHT;
    int chunkZ = blockZ / CHUNK_DEPTH;

    // Compute local coordinates within chunk
    int localX = blockX - (chunkX * CHUNK_WIDTH);
    int localY = blockY - (chunkY * CHUNK_HEIGHT);
    int localZ = blockZ - (chunkZ * CHUNK_DEPTH);

    // Handle negative coordinates properly (modulo behavior for negatives)
    if (localX < 0) { localX += CHUNK_WIDTH; chunkX--; }
    if (localY < 0) { localY += CHUNK_HEIGHT; chunkY--; }
    if (localZ < 0) { localZ += CHUNK_DEPTH; chunkZ--; }

    return { chunkX, chunkY, chunkZ, localX, localY, localZ };
}
