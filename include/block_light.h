#ifndef BLOCK_LIGHT_H
#define BLOCK_LIGHT_H

#include <cstdint>

/**
 * Compact lighting data structure for voxel blocks.
 *
 * Stores two 4-bit light channels in a single byte:
 * - Sky Light: Sunlight from above (0-15)
 * - Block Light: Emissive light from torches, lava, etc. (0-15)
 *
 * Storage efficiency: 1 byte per block
 * Per 32x32x32 chunk: 32,768 blocks × 1 byte = 32 KB
 */
struct BlockLight {
    uint8_t skyLight   : 4;  // 0-15 sunlight level
    uint8_t blockLight : 4;  // 0-15 torch/emissive light level

    // Default constructor: complete darkness
    constexpr BlockLight() : skyLight(0), blockLight(0) {}

    // Constructor with values
    constexpr BlockLight(uint8_t sky, uint8_t block)
        : skyLight(sky), blockLight(block) {}

    // Get the maximum light value from both channels
    inline uint8_t getMaxLight() const {
        return (skyLight > blockLight) ? skyLight : blockLight;
    }

    // Get combined light as a normalized float (0.0 - 1.0)
    inline float getMaxLightNormalized() const {
        return getMaxLight() / 15.0f;
    }
};

// Ensure the struct is exactly 1 byte for memory efficiency
static_assert(sizeof(BlockLight) == 1, "BlockLight must be exactly 1 byte");

#endif // BLOCK_LIGHT_H
