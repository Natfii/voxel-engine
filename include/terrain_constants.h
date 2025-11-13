/**
 * @file terrain_constants.h
 * @brief Named constants for terrain generation and physics
 *
 * Eliminates magic numbers by providing named constants with clear documentation.
 */

#pragma once

namespace TerrainGeneration {
    // Terrain height generation
    constexpr int BASE_HEIGHT = 64;              ///< Base terrain height in blocks (Y coordinate)
    constexpr float HEIGHT_VARIATION = 12.0f;    ///< Max height variation above/below base (blocks)
    constexpr int TOPSOIL_DEPTH = 4;             ///< Depth of dirt layer below grass (blocks)

    // Block type IDs (must match block registry YAML files)
    constexpr int BLOCK_AIR = 0;                 ///< Air block ID
    constexpr int BLOCK_STONE = 1;               ///< Stone block ID (stone.yaml)
    constexpr int BLOCK_DIRT = 2;                ///< Dirt block ID (dirt.yaml)
    constexpr int BLOCK_GRASS = 3;               ///< Grass block ID (grass.yaml)
    constexpr int BLOCK_SAND = 4;                ///< Sand block ID (sand.yaml)
    constexpr int BLOCK_WATER = 5;               ///< Water block ID (water.yaml)
    constexpr int BLOCK_OAK_LOG = 6;             ///< Oak log block ID (oak log.yaml)
    constexpr int BLOCK_LEAVES = 7;              ///< Leaves block ID (leaves.yaml)

    // Water physics
    constexpr int WATER_LEVEL = 62;              ///< Sea level height in blocks (Y coordinate)
}

namespace PhysicsConstants {
    // Player physics thresholds
    constexpr float TERMINAL_VELOCITY = -40.0f;  ///< Maximum falling speed (world units/sec)
    constexpr float GROUND_CHECK_DISTANCE = 0.05f; ///< Distance below player to check for ground
    constexpr float STUCK_THRESHOLD = 0.02f;     ///< Minimum movement to not be considered stuck
    constexpr float STEP_HEIGHT = 0.3f;          ///< Maximum height player can step up (world units)
}
