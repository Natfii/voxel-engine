#ifndef LIGHTING_SYSTEM_H
#define LIGHTING_SYSTEM_H

#include <deque>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>
#include "block_light.h"
#include "frustum.h"

// Forward declarations
class World;
class Chunk;

/**
 * @brief Global lighting system for voxel world
 *
 * Implements industry-standard flood-fill BFS lighting with two separate channels:
 * - Sky Light: Sunlight from above (0-15), propagates down without attenuation
 * - Block Light: Emissive light from torches/lava (0-15), spherical propagation
 *
 * Features:
 * - Incremental updates: Processes lighting over multiple frames (maintains 60 FPS)
 * - Two-queue removal: Handles "ghost lighting" from overlapping sources
 * - Chunk boundary handling: Automatically marks neighbor chunks dirty
 * - Thread-safe: Uses World's existing mutex for concurrent access
 *
 * Performance:
 * - Sub-millisecond lighting updates
 * - Max 500 light additions per frame
 * - Max 300 light removals per frame (higher priority)
 * - Max 10 chunk mesh regenerations per frame
 *
 * Based on research from:
 * - Minecraft (Mojang)
 * - Seeds of Andromeda (fast flood-fill lighting)
 * - 0fps.net (voxel lighting algorithms)
 */
class LightingSystem {
public:
    /**
     * @brief Constructs a lighting system for the specified world
     *
     * @param world Pointer to the world (not owned, must outlive LightingSystem)
     */
    explicit LightingSystem(World* world);

    /**
     * @brief Destroys the lighting system
     */
    ~LightingSystem();

    // ========== Initialization ==========

    /**
     * @brief Initializes lighting for the entire world
     *
     * Generates sunlight for all existing chunks:
     * 1. For each column (x, z), find highest solid block
     * 2. Set skyLight = 15 for all air above
     * 3. Propagate sunlight downward through transparent blocks
     * 4. Spread sunlight horizontally with decay
     *
     * Should be called after world generation but before rendering.
     *
     * @note This is a blocking operation - may take several seconds for large worlds
     */
    void initializeWorldLighting();

    // ========== Update ==========

    /**
     * @brief Updates lighting incrementally (call every frame)
     *
     * Processes queued light additions and removals in batches:
     * - Max 500 light additions per frame
     * - Max 300 light removals per frame (higher priority)
     * - Max 10 chunk mesh regenerations per frame
     *
     * This prevents lighting updates from freezing the game.
     *
     * @param deltaTime Time elapsed since last frame (seconds)
     * @param renderer Vulkan renderer for mesh buffer updates (optional, but required for visual updates)
     */
    void update(float deltaTime, class VulkanRenderer* renderer = nullptr);

    // ========== Viewport-Based Lighting (Dynamic Time-of-Day Updates) ==========

    /**
     * @brief Recalculate sky lighting for chunks visible in frustum
     *
     * This is the core of the viewport-based lighting system. When the sun/moon
     * position changes significantly, this method recalculates lighting ONLY for
     * chunks currently visible to the player.
     *
     * Performance: ~60-100 chunks recalculated (vs 400+ for full world update)
     *
     * @param frustum Camera frustum for visibility culling
     * @param playerPos Player position (for distance sorting/prioritization)
     */
    void recalculateViewportLighting(const Frustum& frustum, const glm::vec3& playerPos);

    /**
     * @brief Get chunks that are visible in the frustum
     *
     * Helper method that filters all world chunks to find those within
     * the camera's view frustum. Used by viewport lighting system.
     *
     * @param frustum Camera frustum for visibility testing
     * @return Vector of chunk pointers that intersect the frustum
     */
    std::vector<Chunk*> getVisibleChunks(const Frustum& frustum) const;

    // ========== Light Source Management ==========

    /**
     * @brief Adds a block light source (torch, lava, etc.)
     *
     * Queues the light source for BFS propagation. The light will spread
     * over multiple frames via the update() method.
     *
     * @param worldPos World position of light source
     * @param lightLevel Light emission level (0-15, typically 14 for torch)
     */
    void addLightSource(const glm::vec3& worldPos, uint8_t lightLevel);

    /**
     * @brief Adds a sky light source (sunlight from above)
     *
     * Used when initializing chunk lighting to queue blocks with sky light
     * for horizontal BFS propagation. This is critical for ensuring leaves
     * and other transparent blocks get properly lit.
     *
     * @param worldPos World position with sky light
     * @param lightLevel Sky light level (0-15, typically 15 for full sunlight)
     */
    void addSkyLightSource(const glm::vec3& worldPos, uint8_t lightLevel);

    /**
     * @brief Removes a light source
     *
     * Uses two-queue removal algorithm to handle overlapping light sources:
     * 1. Phase 1: Clear affected area
     * 2. Phase 2: Re-propagate from remaining sources
     *
     * @param worldPos World position of light source to remove
     */
    void removeLightSource(const glm::vec3& worldPos);

    // ========== Block Change Integration ==========

    /**
     * @brief Called when a chunk is about to be unloaded
     *
     * Removes the chunk from dirty chunks tracking to prevent dangling pointers.
     * CRITICAL: Must be called before the chunk is destroyed!
     *
     * @param chunk Pointer to chunk being removed
     */
    void notifyChunkUnload(Chunk* chunk);

    /**
     * @brief Called when a block changes (placed/broken)
     *
     * Handles lighting updates when blocks are added or removed:
     * - Breaking opaque block: Sunlight floods down
     * - Placing opaque block: Sunlight blocked, area darkens
     * - Breaking emissive: Remove light source
     *
     * @param worldPos Position of changed block
     * @param wasOpaque True if old block was opaque
     * @param isOpaque True if new block is opaque
     */
    void onBlockChanged(const glm::ivec3& worldPos, bool wasOpaque, bool isOpaque);

    // ========== Light Queries ==========

    /**
     * @brief Gets sky light level at world position
     *
     * @param worldPos World position to query
     * @return Sky light level (0-15), or 0 if out of bounds
     */
    uint8_t getSkyLight(const glm::ivec3& worldPos) const;

    /**
     * @brief Gets block light level at world position
     *
     * @param worldPos World position to query
     * @return Block light level (0-15), or 0 if out of bounds
     */
    uint8_t getBlockLight(const glm::ivec3& worldPos) const;

    /**
     * @brief Gets combined light level (max of sky and block)
     *
     * @param worldPos World position to query
     * @return Combined light level (0-15)
     */
    uint8_t getCombinedLight(const glm::ivec3& worldPos) const;

    // ========== Status Queries ==========

    /**
     * @brief Checks if lighting queues are empty
     *
     * @return True if all updates are complete
     */
    bool queuesEmpty() const {
        return m_lightAddQueue.empty() && m_lightRemoveQueue.empty();
    }

    /**
     * @brief Gets the number of pending light additions
     *
     * @return Number of queued additions
     */
    size_t getPendingAdditions() const { return m_lightAddQueue.size(); }

    /**
     * @brief Gets the number of pending light removals
     *
     * @return Number of queued removals
     */
    size_t getPendingRemovals() const { return m_lightRemoveQueue.size(); }

    /**
     * @brief Regenerates meshes for all dirty chunks (blocking)
     *
     * Used during world loading to ensure all spawn chunks have final lighting.
     * Pass maxChunks=10000 to process all dirty chunks immediately.
     *
     * @param maxChunks Maximum chunks to regenerate (use high value for blocking operation)
     * @param renderer Vulkan renderer for GPU upload (nullptr to skip upload)
     */
    void regenerateAllDirtyChunks(int maxChunks, class VulkanRenderer* renderer) {
        regenerateDirtyChunks(maxChunks, renderer);
    }

private:
    // ========== Internal Data Structures ==========

    /**
     * @brief Light node for BFS propagation queue
     */
    struct LightNode {
        glm::ivec3 position;    ///< World position of light
        uint8_t lightLevel;     ///< Light level (0-15)
        bool isSkyLight;        ///< True = sky light, false = block light

        LightNode(const glm::ivec3& pos, uint8_t level, bool isSky)
            : position(pos), lightLevel(level), isSkyLight(isSky) {}
    };

    // ========== Internal Methods ==========

    /**
     * @brief Propagates light from a single node (one BFS step)
     *
     * @param node Light node to propagate from
     */
    void propagateLightStep(const LightNode& node);

    /**
     * @brief Removes light from a single node (one removal step)
     *
     * @param node Light node to remove
     */
    void removeLightStep(const LightNode& node);

    /**
     * @brief Sets sky light at world position
     *
     * @param worldPos World position
     * @param value Sky light level (0-15)
     */
    void setSkyLight(const glm::ivec3& worldPos, uint8_t value);

    /**
     * @brief Sets block light at world position
     *
     * @param worldPos World position
     * @param value Block light level (0-15)
     */
    void setBlockLight(const glm::ivec3& worldPos, uint8_t value);

    /**
     * @brief Marks neighboring chunks as dirty if block is on chunk boundary
     *
     * @param chunk Chunk containing the modified block
     * @param localX Local X coordinate within chunk (0-31)
     * @param localY Local Y coordinate within chunk (0-31)
     * @param localZ Local Z coordinate within chunk (0-31)
     */
    void markNeighborChunksDirty(Chunk* chunk, int localX, int localY, int localZ);

    /**
     * @brief Regenerates meshes for dirty chunks (batched)
     *
     * @param maxPerFrame Maximum chunks to regenerate this frame
     * @param renderer Vulkan renderer for vertex buffer updates
     */
    void regenerateDirtyChunks(int maxPerFrame, class VulkanRenderer* renderer);

    /**
     * @brief Checks if a block is transparent (allows light to pass)
     *
     * @param worldPos World position to check
     * @return True if block is air or transparent
     */
    bool isTransparent(const glm::ivec3& worldPos) const;

    /**
     * @brief Generates initial sunlight for a chunk column
     *
     * @param chunkX Chunk X coordinate
     * @param chunkZ Chunk Z coordinate
     */
    void generateSunlightColumn(int chunkX, int chunkZ);

    // ========== Member Variables ==========

    World* m_world;  ///< World reference (not owned)

    std::deque<LightNode> m_lightAddQueue;     ///< Queue for light additions (BFS)
    std::deque<LightNode> m_lightRemoveQueue;  ///< Queue for light removals (two-queue algorithm)
    std::unordered_set<Chunk*> m_dirtyChunks;  ///< Chunks that need mesh regeneration

    // Performance tuning constants
    static constexpr int MAX_LIGHT_ADDS_PER_FRAME = 500;    ///< Max additions per frame
    static constexpr int MAX_LIGHT_REMOVES_PER_FRAME = 300; ///< Max removals per frame
    static constexpr int MAX_MESH_REGEN_PER_FRAME = 10;     ///< Max mesh regenerations per frame
};

#endif // LIGHTING_SYSTEM_H
