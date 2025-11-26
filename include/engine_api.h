/**
 * @file engine_api.h
 * @brief Main API for voxel engine scripting and commands
 *
 * EngineAPI provides a high-level interface for interacting with the voxel engine.
 * This is the primary API used by console commands, scripts, and external tools.
 *
 * Features:
 * - Block manipulation (place, break, fill, replace)
 * - Terrain modification (brushes, sculpting)
 * - Structure spawning
 * - Entity/mesh management
 * - World queries (raycast, blocks in area)
 * - Player control
 * - Water physics
 *
 * Thread Safety:
 * - All methods are thread-safe and can be called from any thread
 * - Internal locking ensures consistent state
 * - Mesh regeneration is handled automatically
 */

#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <mutex>

// Forward declarations
class World;
class VulkanRenderer;
class Player;
class MeshRenderer;

/**
 * @brief Result of a block query operation
 */
struct BlockQueryResult {
    bool valid;              ///< True if the query succeeded
    int blockID;             ///< Block ID (0 = air)
    std::string blockName;   ///< Block name (e.g., "grass", "stone")
    glm::ivec3 position;     ///< Block position in world coordinates
};

/**
 * @brief Result of a raycast operation
 */
struct RaycastResult {
    bool hit;                ///< True if the ray hit a block
    glm::vec3 position;      ///< World position of the hit point
    glm::vec3 normal;        ///< Normal vector of the hit face
    glm::ivec3 blockPos;     ///< Block coordinates of the hit block
    int blockID;             ///< Block ID of the hit block
    float distance;          ///< Distance from ray origin to hit point
};

/**
 * @brief Information about a spawned entity
 */
struct SpawnedEntity {
    uint32_t entityID;       ///< Unique entity identifier
    glm::vec3 position;      ///< Entity position in world space
    std::string type;        ///< Entity type ("sphere", "cube", "cylinder", "mesh")
};

/**
 * @brief Brush settings for terrain painting operations
 */
struct BrushSettings {
    float radius = 5.0f;     ///< Brush radius in blocks
    float strength = 1.0f;   ///< Brush strength (0.0 to 1.0)
    float falloff = 0.5f;    ///< Edge falloff (0 = hard edge, 1 = smooth)
    bool affectWater = false; ///< If true, affects water blocks too
};

/**
 * @brief Main API for interacting with the voxel engine
 *
 * Singleton class that provides all high-level operations for modifying
 * and querying the voxel world. This is the primary interface used by
 * console commands, scripts, and external tools.
 *
 * Usage:
 * @code
 * auto& api = EngineAPI::instance();
 * api.initialize(world, renderer, player);
 * api.placeBlock(10, 20, 30, "grass");
 * auto result = api.raycast(playerPos, playerDir);
 * @endcode
 */
class EngineAPI {
public:
    /**
     * @brief Gets the singleton instance
     * @return Reference to the global EngineAPI instance
     */
    static EngineAPI& instance();

    /**
     * @brief Initialize the API with world, renderer, and player references
     *
     * Must be called before using any other API methods.
     *
     * @param world World instance
     * @param renderer Vulkan renderer for GPU operations
     * @param player Player instance
     */
    void initialize(World* world, VulkanRenderer* renderer, Player* player);

    /**
     * @brief Check if the API has been initialized
     * @return True if initialize() has been called
     */
    bool isInitialized() const;

    // ==================== BLOCK MANIPULATION ====================

    /**
     * @brief Place a block at the specified position
     *
     * @param x X coordinate in world space
     * @param y Y coordinate in world space
     * @param z Z coordinate in world space
     * @param blockID Block ID to place
     * @return True if successful, false if out of bounds
     */
    bool placeBlock(int x, int y, int z, int blockID);

    /**
     * @brief Place a block at the specified position (vector overload)
     */
    bool placeBlock(const glm::ivec3& pos, int blockID);

    /**
     * @brief Place a block by name
     *
     * @param pos Block position
     * @param blockName Block name (e.g., "grass", "stone")
     * @return True if successful, false if block name not found or out of bounds
     */
    bool placeBlock(const glm::ivec3& pos, const std::string& blockName);

    /**
     * @brief Break (remove) a block at the specified position
     *
     * @param x X coordinate in world space
     * @param y Y coordinate in world space
     * @param z Z coordinate in world space
     * @return True if successful, false if out of bounds
     */
    bool breakBlock(int x, int y, int z);

    /**
     * @brief Break (remove) a block at the specified position (vector overload)
     */
    bool breakBlock(const glm::ivec3& pos);

    /**
     * @brief Set block metadata
     *
     * @param pos Block position
     * @param metadata Metadata value (0-255)
     * @return True if successful, false if out of bounds
     */
    bool setBlockMetadata(const glm::ivec3& pos, uint8_t metadata);

    /**
     * @brief Get block metadata
     *
     * @param pos Block position
     * @return Metadata value (0-255), or 0 if out of bounds
     */
    uint8_t getBlockMetadata(const glm::ivec3& pos);

    /**
     * @brief Query block information at a position
     *
     * @param x X coordinate in world space
     * @param y Y coordinate in world space
     * @param z Z coordinate in world space
     * @return Block query result with ID, name, and position
     */
    BlockQueryResult getBlockAt(int x, int y, int z);

    /**
     * @brief Query block information at a position (vector overload)
     */
    BlockQueryResult getBlockAt(const glm::ivec3& pos);

    // ==================== AREA OPERATIONS ====================

    /**
     * @brief Fill an area with blocks
     *
     * Fills all blocks in the rectangular region from start to end (inclusive).
     *
     * @param start Start corner of the region
     * @param end End corner of the region
     * @param blockID Block ID to fill with
     * @return Number of blocks placed
     */
    int fillArea(const glm::ivec3& start, const glm::ivec3& end, int blockID);

    /**
     * @brief Fill an area with blocks by name
     */
    int fillArea(const glm::ivec3& start, const glm::ivec3& end, const std::string& blockName);

    /**
     * @brief Replace blocks in an area
     *
     * Replaces all blocks matching fromBlockID with toBlockID in the specified region.
     *
     * @param start Start corner of the region
     * @param end End corner of the region
     * @param fromBlockID Block ID to replace
     * @param toBlockID Block ID to replace with
     * @return Number of blocks replaced
     */
    int replaceBlocks(const glm::ivec3& start, const glm::ivec3& end,
                      int fromBlockID, int toBlockID);

    /**
     * @brief Replace blocks in an area by name
     */
    int replaceBlocks(const glm::ivec3& start, const glm::ivec3& end,
                      const std::string& fromName, const std::string& toName);

    // ==================== SPHERE OPERATIONS ====================

    /**
     * @brief Fill a spherical region with blocks
     *
     * @param center Center of the sphere
     * @param radius Sphere radius in blocks
     * @param blockID Block ID to fill with
     * @return Number of blocks placed
     */
    int fillSphere(const glm::vec3& center, float radius, int blockID);

    /**
     * @brief Create a hollow sphere
     *
     * @param center Center of the sphere
     * @param radius Sphere radius in blocks
     * @param blockID Block ID for the shell
     * @param thickness Shell thickness in blocks
     * @return Number of blocks placed
     */
    int hollowSphere(const glm::vec3& center, float radius, int blockID, float thickness = 1.0f);

    // ==================== TERRAIN MODIFICATION ====================

    /**
     * @brief Raise terrain in a circular area
     *
     * @param center Center of the brush
     * @param radius Brush radius
     * @param height Height to raise (in blocks)
     * @param brush Brush settings
     * @return Number of blocks modified
     */
    int raiseTerrain(const glm::vec3& center, float radius, float height,
                     const BrushSettings& brush = {});

    /**
     * @brief Lower terrain in a circular area
     *
     * @param center Center of the brush
     * @param radius Brush radius
     * @param depth Depth to lower (in blocks)
     * @param brush Brush settings
     * @return Number of blocks modified
     */
    int lowerTerrain(const glm::vec3& center, float radius, float depth,
                     const BrushSettings& brush = {});

    /**
     * @brief Smooth terrain in a circular area
     *
     * Averages block heights to create smooth transitions.
     *
     * @param center Center of the brush
     * @param radius Brush radius
     * @param brush Brush settings
     * @return Number of blocks modified
     */
    int smoothTerrain(const glm::vec3& center, float radius,
                      const BrushSettings& brush = {});

    /**
     * @brief Paint terrain with a specific block type
     *
     * @param center Center of the brush
     * @param radius Brush radius
     * @param blockID Block ID to paint with
     * @param brush Brush settings
     * @return Number of blocks modified
     */
    int paintTerrain(const glm::vec3& center, float radius, int blockID,
                     const BrushSettings& brush = {});

    /**
     * @brief Flatten terrain to a specific Y level
     *
     * @param center Center of the brush
     * @param radius Brush radius
     * @param targetY Target Y coordinate
     * @param brush Brush settings
     * @return Number of blocks modified
     */
    int flattenTerrain(const glm::vec3& center, float radius, int targetY,
                       const BrushSettings& brush = {});

    // ==================== STRUCTURE SPAWNING ====================

    /**
     * @brief Spawn a structure at a position
     *
     * @param name Structure name
     * @param position Center position for the structure
     * @return True if successful, false if structure not found
     */
    bool spawnStructure(const std::string& name, const glm::ivec3& position);

    /**
     * @brief Spawn a structure with rotation
     *
     * @param name Structure name
     * @param position Center position for the structure
     * @param rotation Rotation in degrees (0, 90, 180, 270)
     * @return True if successful, false if structure not found
     */
    bool spawnStructure(const std::string& name, const glm::ivec3& position,
                        int rotation);

    // ==================== ENTITY/MESH SPAWNING ====================

    /**
     * @brief Spawn a sphere entity
     *
     * @param position Sphere center position
     * @param radius Sphere radius
     * @param color RGBA color
     * @return Spawned entity information
     */
    SpawnedEntity spawnSphere(const glm::vec3& position, float radius,
                              const glm::vec4& color = glm::vec4(1.0f));

    /**
     * @brief Spawn a cube entity
     *
     * @param position Cube center position
     * @param size Cube size (edge length)
     * @param color RGBA color
     * @return Spawned entity information
     */
    SpawnedEntity spawnCube(const glm::vec3& position, float size,
                            const glm::vec4& color = glm::vec4(1.0f));

    /**
     * @brief Spawn a cylinder entity
     *
     * @param position Cylinder center position
     * @param radius Cylinder radius
     * @param height Cylinder height
     * @param color RGBA color
     * @return Spawned entity information
     */
    SpawnedEntity spawnCylinder(const glm::vec3& position, float radius, float height,
                                const glm::vec4& color = glm::vec4(1.0f));

    /**
     * @brief Spawn a custom mesh entity
     *
     * @param meshName Mesh file name (without extension)
     * @param position Mesh position
     * @param scale Mesh scale
     * @param rotation Mesh rotation (Euler angles in degrees)
     * @return Spawned entity information
     */
    SpawnedEntity spawnMesh(const std::string& meshName, const glm::vec3& position,
                            const glm::vec3& scale = glm::vec3(1.0f),
                            const glm::vec3& rotation = glm::vec3(0.0f));

    /**
     * @brief Remove a spawned entity
     *
     * @param entityID Entity ID to remove
     * @return True if successful, false if entity not found
     */
    bool removeEntity(uint32_t entityID);

    /**
     * @brief Set entity position
     *
     * @param entityID Entity ID
     * @param position New position
     * @return True if successful, false if entity not found
     */
    bool setEntityPosition(uint32_t entityID, const glm::vec3& position);

    /**
     * @brief Set entity scale
     *
     * @param entityID Entity ID
     * @param scale New scale
     * @return True if successful, false if entity not found
     */
    bool setEntityScale(uint32_t entityID, const glm::vec3& scale);

    /**
     * @brief Set entity color
     *
     * @param entityID Entity ID
     * @param color New RGBA color
     * @return True if successful, false if entity not found
     */
    bool setEntityColor(uint32_t entityID, const glm::vec4& color);

    /**
     * @brief Get all spawned entities
     * @return Vector of all spawned entities
     */
    std::vector<SpawnedEntity> getAllEntities();

    // ==================== WORLD QUERIES ====================

    /**
     * @brief Cast a ray into the world
     *
     * @param origin Ray origin
     * @param direction Ray direction (normalized)
     * @param maxDistance Maximum ray distance
     * @return Raycast result
     */
    RaycastResult raycast(const glm::vec3& origin, const glm::vec3& direction,
                          float maxDistance = 100.0f);

    /**
     * @brief Get all blocks within a radius
     *
     * @param center Center position
     * @param radius Search radius
     * @return Vector of block query results
     */
    std::vector<BlockQueryResult> getBlocksInRadius(const glm::vec3& center,
                                                    float radius);

    /**
     * @brief Get all blocks in an area
     *
     * @param start Start corner
     * @param end End corner
     * @return Vector of block query results
     */
    std::vector<BlockQueryResult> getBlocksInArea(const glm::ivec3& start,
                                                  const glm::ivec3& end);

    /**
     * @brief Get biome name at a position
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Biome name (e.g., "plains", "forest")
     */
    std::string getBiomeAt(float x, float z);

    /**
     * @brief Get terrain height at a position
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Terrain height (Y coordinate of the top block)
     */
    int getHeightAt(float x, float z);

    // ==================== PLAYER ====================

    /**
     * @brief Get player position
     * @return Player eye position in world space
     */
    glm::vec3 getPlayerPosition();

    /**
     * @brief Set player position
     * @param pos New eye position
     */
    void setPlayerPosition(const glm::vec3& pos);

    /**
     * @brief Get player look direction
     * @return Normalized look direction vector
     */
    glm::vec3 getPlayerLookDirection();

    /**
     * @brief Get the block the player is looking at
     *
     * @param maxDistance Maximum look distance
     * @return Raycast result for the targeted block
     */
    RaycastResult getPlayerTarget(float maxDistance = 5.0f);

    // ==================== WATER ====================

    /**
     * @brief Place water at a position
     *
     * @param pos Block position
     * @return True if successful, false if out of bounds
     */
    bool placeWater(const glm::ivec3& pos);

    /**
     * @brief Remove water at a position
     *
     * @param pos Block position
     * @return True if successful, false if out of bounds
     */
    bool removeWater(const glm::ivec3& pos);

    /**
     * @brief Flood fill an area with blocks
     *
     * Fills all connected air blocks starting from the specified position.
     *
     * @param start Starting position
     * @param blockID Block ID to fill with
     * @param maxBlocks Maximum number of blocks to fill
     * @return Number of blocks filled
     */
    int floodFill(const glm::ivec3& start, int blockID, int maxBlocks = 10000);

    // ==================== UTILITY ====================

    /**
     * @brief Get block ID from name
     *
     * @param blockName Block name (e.g., "grass", "stone")
     * @return Block ID, or -1 if not found
     */
    int getBlockID(const std::string& blockName);

    /**
     * @brief Get block name from ID
     *
     * @param blockID Block ID
     * @return Block name, or "Unknown" if invalid
     */
    std::string getBlockName(int blockID);

    /**
     * @brief Get all registered block names
     * @return Vector of all block names
     */
    std::vector<std::string> getAllBlockNames();

    /**
     * @brief Get all registered structure names
     * @return Vector of all structure names
     */
    std::vector<std::string> getAllStructureNames();

    /**
     * @brief Get all registered biome names
     * @return Vector of all biome names
     */
    std::vector<std::string> getAllBiomeNames();

    /**
     * @brief Get time of day
     *
     * @return Time value (0.0 = midnight, 0.5 = noon, 1.0 = next midnight)
     */
    float getTimeOfDay();

    /**
     * @brief Set time of day
     *
     * @param time Time value (0.0 = midnight, 0.5 = noon, 1.0 = next midnight)
     */
    void setTimeOfDay(float time);

private:
    // Private constructor (singleton)
    EngineAPI() = default;
    ~EngineAPI() = default;
    EngineAPI(const EngineAPI&) = delete;
    EngineAPI& operator=(const EngineAPI&) = delete;

    // Helper functions

    /**
     * @brief Calculate brush influence at a distance
     *
     * @param distance Distance from brush center
     * @param brush Brush settings
     * @return Influence value (0.0 to 1.0)
     */
    float calculateBrushInfluence(float distance, const BrushSettings& brush);

    /**
     * @brief Regenerate chunks affected by block changes
     *
     * @param start Start corner of affected region
     * @param end End corner of affected region
     */
    void regenerateAffectedChunks(const glm::ivec3& start, const glm::ivec3& end);

    // Core references
    World* m_world = nullptr;
    VulkanRenderer* m_renderer = nullptr;
    Player* m_player = nullptr;
    MeshRenderer* m_meshRenderer = nullptr;

    // Entity tracking
    std::vector<SpawnedEntity> m_spawnedEntities;
    uint32_t m_nextEntityID = 1;

    // Thread safety
    mutable std::mutex m_mutex;

    // Time of day (future feature)
    float m_timeOfDay = 0.0f;
};
