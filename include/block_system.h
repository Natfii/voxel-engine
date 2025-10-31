#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>        // for glm::vec3
#include <yaml-cpp/yaml.h>   // for YAML::Node

// Block ID constants
namespace BlockID {
    constexpr int AIR = 0;
}

// Helper functions for block properties
inline bool isAir(int blockID) { return blockID == BlockID::AIR; }
inline bool isSolid(int blockID) { return blockID > BlockID::AIR; }

// Forward declarations
#include <vulkan/vulkan.h>
class VulkanRenderer;

// A definition of a block, parsed from a YAML file.
struct BlockDefinition {
    int id = -1;
    std::string name;

    // Rendering properties
    bool hasTexture = false;        // True if texture loaded successfully
    bool hasColor = false;          // True if using solid color
    glm::vec3 color = glm::vec3(0.0f);
    float textureVariation = 1.0f;  // Zoom factor for random texture sampling (1.0 = no variation)

    // Texture atlas position (coordinates in the atlas grid)
    int atlasX = 0;  // X position in atlas grid (0, 1, 2, ...)
    int atlasY = 0;  // Y position in atlas grid (0, 1, 2, ...)

    // Block properties
    int durability = 0;
    bool affectedByGravity = false;
    int flammability = 0;
    float transparency = 0.0f;
    bool redstone = false;

    // Additional custom data (raw YAML node)
    YAML::Node metadata;
};

// Singleton registry of all block definitions loaded from YAML.
// Provides fast lookup by numeric ID or block name.
class BlockRegistry {
public:
    // Get the singleton instance
    static BlockRegistry& instance();

    // Load all block YAML files from the given directory.
    // Expected file extension: .yaml or .yml.
    // Textures are loaded from assets/blocks/{block_name}.png (auto-resized to 64x64)
    // Returns true on success.
    bool loadBlocks(const std::string& directory = "assets/blocks", VulkanRenderer* renderer = nullptr);

    // Get a block definition by numeric ID. Throws if invalid.
    const BlockDefinition& get(int id) const;
    // Get a block definition by block name. Throws if not found.
    const BlockDefinition& get(const std::string& name) const;
    
    // Get numeric ID for a given block name, or -1 if not found.
    int getID(const std::string& name) const;

    // Total number of block definitions (including "Air")
    int count() const { return (int)m_defs.size(); }

    // Texture atlas access
    VkImageView getAtlasImageView() const { return m_atlasImageView; }
    VkSampler getAtlasSampler() const { return m_atlasSampler; }
    int getAtlasGridSize() const { return m_atlasGridSize; }

private:
    BlockRegistry();                        // private constructor (singleton)
    BlockRegistry(const BlockRegistry&) = delete;
    BlockRegistry& operator=(const BlockRegistry&) = delete;

    void buildTextureAtlas(VulkanRenderer* renderer);

    std::vector<BlockDefinition> m_defs;    // indexed by ID
    std::unordered_map<std::string,int> m_nameToID;

    // Texture atlas (single combined texture for all blocks)
    VkImage m_atlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_atlasMemory = VK_NULL_HANDLE;
    VkImageView m_atlasImageView = VK_NULL_HANDLE;
    VkSampler m_atlasSampler = VK_NULL_HANDLE;
    int m_atlasGridSize = 0;  // Grid size (e.g., 4 = 4×4 grid, 16 blocks)
};
