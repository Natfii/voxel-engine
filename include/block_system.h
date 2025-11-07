/**
 * @file block_system.h
 * @brief Extensible block system with YAML-based definitions and texture atlas
 *
 * Enhanced API documentation by Claude (Anthropic AI Assistant)
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>        // for glm::vec3
#include <yaml-cpp/yaml.h>   // for YAML::Node

// Block ID constants
namespace BlockID {
    constexpr int AIR = 0;  ///< Air (empty space, non-solid)
}

// Helper functions for block properties
inline bool isAir(int blockID) { return blockID == BlockID::AIR; }
inline bool isSolid(int blockID) { return blockID > BlockID::AIR; }

// Forward declarations
#include <vulkan/vulkan.h>
class VulkanRenderer;

/**
 * @brief Definition of a block type loaded from YAML configuration
 *
 * Contains all properties and rendering data for a block type.
 * Blocks can use either solid colors or cube-mapped textures.
 *
 * Example YAML:
 * @code
 * id: 2
 * name: "grass"
 * durability: 3
 * texture:
 *   all: {x: 0, y: 0}      # Default texture for all faces
 *   top: {x: 1, y: 0}      # Override for top face
 * @endcode
 */
struct BlockDefinition {
    int id = -1;                       ///< Unique block ID
    std::string name;                  ///< Block name (e.g., "grass", "stone")

    // ========== Rendering Properties ==========
    bool hasTexture = false;           ///< True if texture was loaded successfully
    bool hasColor = false;             ///< True if using solid color fallback
    glm::vec3 color = glm::vec3(0.0f); ///< Solid color (RGB, 0-1 range)

    /**
     * @brief Texture coordinates for a single block face
     *
     * Specifies position in texture atlas and optional variation.
     */
    struct FaceTexture {
        int atlasX = 0;                ///< X position in atlas grid (0, 1, 2, ...)
        int atlasY = 0;                ///< Y position in atlas grid (0, 1, 2, ...)
        float variation = 1.0f;        ///< Texture variation (1.0 = no variation, >1.0 = zoomed)
    };

    // ========== Cube Map Textures ==========
    // Each face can have different textures (e.g., grass has different top/bottom)
    FaceTexture all;                   ///< Default texture for all faces
    FaceTexture top;                   ///< +Y face (top)
    FaceTexture bottom;                ///< -Y face (bottom)
    FaceTexture front;                 ///< -Z face (front)
    FaceTexture back;                  ///< +Z face (back)
    FaceTexture left;                  ///< -X face (left)
    FaceTexture right;                 ///< +X face (right)

    bool useCubeMap = false;           ///< True if using per-face textures

    // ========== Gameplay Properties ==========
    int durability = 0;                ///< How hard the block is to break
    bool affectedByGravity = false;    ///< If true, block falls (like sand)
    int flammability = 0;              ///< How easily block catches fire
    float transparency = 0.0f;         ///< Transparency (0=opaque, 1=fully transparent)
    bool redstone = false;             ///< If true, conducts redstone signal
    bool isLiquid = false;             ///< If true, no outline when targeting

    // ========== Custom Data ==========
    YAML::Node metadata;               ///< Raw YAML node for custom properties
};

/**
 * @brief Singleton registry of all block types with texture atlas management
 *
 * The BlockRegistry loads block definitions from YAML files and manages
 * a texture atlas for efficient GPU rendering.
 *
 * Features:
 * - YAML-based block definition loading
 * - Automatic texture atlas generation
 * - Fast ID and name-based lookups
 * - Support for cube-mapped textures (per-face textures)
 *
 * Texture Atlas System:
 * - All block textures are packed into a single GPU texture
 * - Each texture is resized to 64x64 pixels
 * - Atlas is organized as an NxN grid
 * - UV coordinates are computed based on atlas position
 *
 * Usage:
 * @code
 * auto& registry = BlockRegistry::instance();
 * registry.loadBlocks("assets/blocks", renderer);
 * const auto& grass = registry.get("grass");
 * @endcode
 */
class BlockRegistry {
public:
    /**
     * @brief Gets the singleton instance of the registry
     * @return Reference to the global block registry
     */
    static BlockRegistry& instance();

    /**
     * @brief Loads all block definitions from YAML files and builds texture atlas
     *
     * Scans the directory for .yaml/.yml files, parses block definitions,
     * loads textures from matching .png files, and creates a texture atlas.
     *
     * Expected file structure:
     * - assets/blocks/grass.yaml (block definition)
     * - assets/blocks/grass.png (texture, auto-resized to 64x64)
     *
     * @param directory Directory containing YAML files (default: "assets/blocks")
     * @param renderer Vulkan renderer for atlas creation (can be null for no textures)
     * @return True if loading succeeded, false on error
     */
    bool loadBlocks(const std::string& directory = "assets/blocks", VulkanRenderer* renderer = nullptr);

    /**
     * @brief Gets a block definition by numeric ID
     *
     * @param id Block ID
     * @return Reference to block definition
     * @throws std::out_of_range if ID is invalid
     */
    const BlockDefinition& get(int id) const;

    /**
     * @brief Gets a block definition by name
     *
     * @param name Block name (e.g., "grass", "stone")
     * @return Reference to block definition
     * @throws std::runtime_error if name not found
     */
    const BlockDefinition& get(const std::string& name) const;

    /**
     * @brief Gets the numeric ID for a block name
     *
     * @param name Block name
     * @return Block ID, or -1 if not found
     */
    int getID(const std::string& name) const;

    /**
     * @brief Gets the total number of registered blocks
     * @return Block count (including Air at ID 0)
     */
    int count() const { return (int)m_defs.size(); }

    // ========== Texture Atlas Access ==========

    /**
     * @brief Gets the texture atlas image view for rendering
     * @return Vulkan image view of the atlas
     */
    VkImageView getAtlasImageView() const { return m_atlasImageView; }

    /**
     * @brief Gets the texture atlas sampler
     * @return Vulkan sampler for the atlas
     */
    VkSampler getAtlasSampler() const { return m_atlasSampler; }

    /**
     * @brief Gets the atlas grid size
     * @return Grid dimension (e.g., 4 for 4x4 = 16 textures)
     */
    int getAtlasGridSize() const { return m_atlasGridSize; }

    // ========== Query Methods ==========
    // Used by targeting system and UI

    /**
     * @brief Gets the display name of a block
     * @param blockID Block ID
     * @return Block name, or "Unknown" if invalid
     */
    std::string getBlockName(int blockID) const;

    /**
     * @brief Gets the type string for a block
     * @param blockID Block ID
     * @return Type description (e.g., "solid", "liquid")
     */
    std::string getBlockType(int blockID) const;

    /**
     * @brief Checks if a block can be broken by the player
     * @param blockID Block ID
     * @return True if breakable, false if indestructible (or Air)
     */
    bool isBreakable(int blockID) const;

private:
    /**
     * @brief Private constructor (singleton pattern)
     */
    BlockRegistry();
    BlockRegistry(const BlockRegistry&) = delete;
    BlockRegistry& operator=(const BlockRegistry&) = delete;

    /**
     * @brief Builds the texture atlas from loaded block textures
     *
     * Packs all block textures into a single GPU texture for efficient rendering.
     *
     * @param renderer Vulkan renderer for atlas creation
     */
    void buildTextureAtlas(VulkanRenderer* renderer);

    // ========== Data Storage ==========
    std::vector<BlockDefinition> m_defs;           ///< Block definitions indexed by ID
    std::unordered_map<std::string,int> m_nameToID; ///< Name to ID lookup

    // ========== Texture Atlas ==========
    VkImage m_atlasImage = VK_NULL_HANDLE;         ///< Combined texture atlas
    VkDeviceMemory m_atlasMemory = VK_NULL_HANDLE; ///< Atlas GPU memory
    VkImageView m_atlasImageView = VK_NULL_HANDLE; ///< Atlas image view
    VkSampler m_atlasSampler = VK_NULL_HANDLE;     ///< Atlas sampler
    int m_atlasGridSize = 0;                       ///< Grid size (NxN)
};
