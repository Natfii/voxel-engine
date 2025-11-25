/**
 * @file map_preview.h
 * @brief Live map preview for loading screen
 *
 * Generates a real-time minimap showing terrain/biomes as chunks are generated.
 * Used during world loading to give visual feedback on generation progress.
 * Features animated reveal of chunks with configurable delay.
 *
 * Created: 2025-11-25
 */

#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <chrono>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// Forward declarations
class BiomeMap;
class VulkanRenderer;

/**
 * @brief Live map preview generator for loading screen
 *
 * Creates a small texture (MAP_SIZE x MAP_SIZE) that represents the world
 * from a top-down view. Colors are based on biome/terrain type.
 * Updates in real-time as chunks are generated.
 */
class MapPreview {
public:
    static constexpr int MAP_SIZE = 128;  ///< Pixels per side (128x128 texture)
    static constexpr int BLOCKS_PER_PIXEL = 4;  ///< Each pixel represents 4x4 blocks

    MapPreview();
    ~MapPreview();

    /**
     * @brief Initialize the map preview with biome data source
     * @param biomeMap Pointer to biome map for sampling
     * @param renderer Vulkan renderer for texture creation
     * @param centerX World X coordinate at center of map
     * @param centerZ World Z coordinate at center of map
     * @return True if initialization succeeded
     */
    bool initialize(BiomeMap* biomeMap, VulkanRenderer* renderer, int centerX, int centerZ);

    /**
     * @brief Generate the full map preview (blocking)
     * Called once to create initial preview
     */
    void generateFullPreview();

    /**
     * @brief Mark a chunk as generated (queues for animated reveal)
     * @param chunkX Chunk X coordinate
     * @param chunkZ Chunk Z coordinate
     */
    void markChunkGenerated(int chunkX, int chunkZ);

    /**
     * @brief Process pending chunk animations and upload texture
     *
     * Call this each frame during loading. Processes queued chunks
     * with staggered delays for smooth animation effect.
     */
    void updateTexture();

    /**
     * @brief Set animation speed (chunks revealed per second)
     * @param chunksPerSecond How many chunks to reveal per second (default: 30)
     */
    void setAnimationSpeed(float chunksPerSecond) { m_chunksPerSecond = chunksPerSecond; }

    /**
     * @brief Get ImGui texture descriptor for rendering
     * @return ImGui-compatible texture handle
     */
    VkDescriptorSet getImGuiTexture() const { return m_imguiDescriptor; }

    /**
     * @brief Check if preview is ready for display
     */
    bool isReady() const { return m_initialized; }

    /**
     * @brief Cleanup GPU resources
     */
    void cleanup();

private:
    /**
     * @brief Actually reveal a chunk on the map (called from animation loop)
     */
    void revealChunk(int chunkX, int chunkZ);

    /**
     * @brief Sample terrain at a world position and return a color
     * @param worldX World X coordinate
     * @param worldZ World Z coordinate
     * @return RGBA color (packed uint32_t)
     */
    uint32_t sampleTerrainColor(float worldX, float worldZ);

    /**
     * @brief Convert biome temperature/moisture to a display color
     */
    uint32_t biomeToColor(int temperature, int moisture, float height);

    BiomeMap* m_biomeMap = nullptr;
    VulkanRenderer* m_renderer = nullptr;

    int m_centerX = 0;
    int m_centerZ = 0;

    // Pixel data (RGBA, 4 bytes per pixel)
    std::vector<uint32_t> m_pixels;
    std::mutex m_pixelMutex;

    // Vulkan resources
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSet m_imguiDescriptor = VK_NULL_HANDLE;

    // Staging buffer for CPU->GPU transfer
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;

    bool m_initialized = false;
    bool m_needsUpdate = false;

    // Animation queue for staggered chunk reveals
    struct PendingChunk {
        int chunkX, chunkZ;
        std::chrono::steady_clock::time_point queueTime;
    };
    std::queue<PendingChunk> m_pendingChunks;
    std::mutex m_pendingMutex;
    float m_chunksPerSecond = 50.0f;  // Animation speed
    std::chrono::steady_clock::time_point m_lastRevealTime;
};
