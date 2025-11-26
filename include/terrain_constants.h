/**
 * @file terrain_constants.h
 * @brief Named constants for terrain generation and physics
 *
 * Eliminates magic numbers by providing named constants with clear documentation.
 */

#pragma once

namespace TerrainGeneration {
    // World dimensions
    constexpr int WORLD_HEIGHT_CHUNKS = 512;     ///< World height in chunks (16384 blocks, near-infinite vertical)
    constexpr int WORLD_BOTTOM_Y = -128;         ///< Bottom of the world (Y coordinate in blocks)
    constexpr int BEDROCK_LAYER_Y = -120;        ///< Y level where bedrock layer begins (bottom 8 blocks)
    constexpr int UNDERGROUND_DEPTH_CHUNKS = 5;  ///< How many chunks deep to generate underground (5 chunks = 160 blocks)

    // Terrain height generation
    constexpr int BASE_HEIGHT = 64;              ///< Base terrain height in blocks (Y coordinate)
    constexpr float HEIGHT_VARIATION = 12.0f;    ///< Max height variation above/below base (blocks)
    constexpr int TOPSOIL_DEPTH = 5;             ///< Depth of dirt layer below grass (blocks)

    // Block type IDs (must match block registry YAML files)
    constexpr int BLOCK_AIR = 0;                 ///< Air block ID
    constexpr int BLOCK_STONE = 1;               ///< Stone block ID (stone.yaml)
    constexpr int BLOCK_DIRT = 2;                ///< Dirt block ID (dirt.yaml)
    constexpr int BLOCK_GRASS = 3;               ///< Grass block ID (grass.yaml)
    constexpr int BLOCK_SAND = 4;                ///< Sand block ID (sand.yaml)
    constexpr int BLOCK_WATER = 5;               ///< Water block ID (water.yaml)
    constexpr int BLOCK_OAK_LOG = 6;             ///< Oak log block ID (oak log.yaml)
    constexpr int BLOCK_LEAVES = 7;              ///< Leaves block ID (leaves.yaml)
    constexpr int BLOCK_SPRUCE_LOG = 8;          ///< Spruce log block ID (spruce log.yaml)
    constexpr int BLOCK_SPRUCE_LEAVES = 9;       ///< Spruce leaves block ID (spruce leaves.yaml)
    constexpr int BLOCK_SNOW = 10;               ///< Snow block ID (snow.yaml)
    constexpr int BLOCK_ICE = 11;                ///< Ice block ID (ice.yaml)
    constexpr int BLOCK_BEDROCK = 12;            ///< Bedrock block ID (bedrock.yaml)

    // Water physics
    constexpr int WATER_LEVEL = 62;              ///< Sea level height in blocks (Y coordinate)

    // Aquifer/underground water constants
    constexpr int AQUIFER_LEVEL = -30;           ///< Default water table level (Y coordinate)
    constexpr int AQUIFER_VARIATION = 15;        ///< Water table can vary ±15 blocks
    constexpr float AQUIFER_CHANCE = 0.25f;      ///< 25% of caves below water table have water

    // Snow line (2025-11-25): Y level above which snow appears on peaks
    constexpr int SNOW_LINE = 95;                ///< Y level above which snow appears
    constexpr int SNOW_TRANSITION = 5;           ///< Blocks of gradual snow transition
}

namespace PhysicsConstants {
    // Player physics thresholds
    constexpr float TERMINAL_VELOCITY = -40.0f;  ///< Maximum falling speed (world units/sec)
    constexpr float GROUND_CHECK_DISTANCE = 0.1f; ///< Distance below player to check for ground (increased for better precision)
    constexpr float STUCK_THRESHOLD = 0.02f;     ///< Minimum movement to not be considered stuck
    constexpr float STEP_HEIGHT = 0.3f;          ///< Maximum height player can step up (world units)
}

namespace BlockMetadataPacking {
    // METADATA PACKING: Pack multiple values into the existing uint8_t metadata field
    // This is more memory-efficient than adding separate arrays for each property
    //
    // Bit layout (8 bits total):
    // Bits 0-3: Water level (0-15, for fluid simulation)
    // Bits 4-5: Rotation (0-3, for logs/directional blocks: N/S/E/W or up/down)
    // Bits 6-7: Light level (0-3, simple ambient occlusion hint)
    //
    // Example: metadata = 0b11100101
    //   Light level = 3 (bits 6-7 = 11)
    //   Rotation = 2 (bits 4-5 = 10)
    //   Water level = 5 (bits 0-3 = 0101)

    constexpr uint8_t WATER_LEVEL_MASK = 0x0F;  ///< Bits 0-3
    constexpr uint8_t ROTATION_MASK = 0x30;     ///< Bits 4-5
    constexpr uint8_t LIGHT_LEVEL_MASK = 0xC0;  ///< Bits 6-7

    constexpr int WATER_LEVEL_SHIFT = 0;
    constexpr int ROTATION_SHIFT = 4;
    constexpr int LIGHT_LEVEL_SHIFT = 6;

    // Helper functions for metadata packing/unpacking
    inline uint8_t packMetadata(uint8_t waterLevel, uint8_t rotation, uint8_t lightLevel) {
        return ((waterLevel & 0x0F) << WATER_LEVEL_SHIFT) |
               ((rotation & 0x03) << ROTATION_SHIFT) |
               ((lightLevel & 0x03) << LIGHT_LEVEL_SHIFT);
    }

    inline uint8_t getWaterLevel(uint8_t metadata) {
        return (metadata & WATER_LEVEL_MASK) >> WATER_LEVEL_SHIFT;
    }

    inline uint8_t getRotation(uint8_t metadata) {
        return (metadata & ROTATION_MASK) >> ROTATION_SHIFT;
    }

    inline uint8_t getLightLevel(uint8_t metadata) {
        return (metadata & LIGHT_LEVEL_MASK) >> LIGHT_LEVEL_SHIFT;
    }

    inline void setWaterLevel(uint8_t& metadata, uint8_t waterLevel) {
        metadata = (metadata & ~WATER_LEVEL_MASK) | ((waterLevel & 0x0F) << WATER_LEVEL_SHIFT);
    }

    inline void setRotation(uint8_t& metadata, uint8_t rotation) {
        metadata = (metadata & ~ROTATION_MASK) | ((rotation & 0x03) << ROTATION_SHIFT);
    }

    inline void setLightLevel(uint8_t& metadata, uint8_t lightLevel) {
        metadata = (metadata & ~LIGHT_LEVEL_MASK) | ((lightLevel & 0x03) << LIGHT_LEVEL_SHIFT);
    }
}
