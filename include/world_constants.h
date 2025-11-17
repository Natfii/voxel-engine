/**
 * @file world_constants.h
 * @brief Constants for world rendering and culling
 */

#pragma once

namespace WorldConstants {
    // ========== Debug Output ==========
    /// Frames between debug output (60 frames ≈ 1 second at 60 FPS)
    constexpr int DEBUG_OUTPUT_INTERVAL = 60;

    // ========== Chunk Culling Constants ==========
    /// Fragment shader discard distance multiplier
    /// The fragment shader discards fragments beyond renderDistance * this value
    constexpr float FRAGMENT_DISCARD_MARGIN = 1.05f;

    /// Half-diagonal distance of a chunk (world units)
    /// Chunks are 32x32x32 blocks = 32x32x32 world units
    /// Distance from center to farthest corner = sqrt(16² + 16² + 16²) ≈ 27.71
    constexpr float CHUNK_HALF_DIAGONAL = 27.71f;

    /// Additional frustum culling margin (world units)
    /// Padding to prevent edge-case chunk popping
    constexpr float FRUSTUM_CULLING_PADDING = 2.0f;
}

namespace PlayerConstants {
    // ========== Debug Output Intervals ==========
    /// Frames between collision debug output
    constexpr int COLLISION_DEBUG_INTERVAL = 60;

    /// Frames between position debug output (less frequent)
    constexpr int POSITION_DEBUG_INTERVAL = 120;
}
