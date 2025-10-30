#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>        // for glm::vec3
#include <yaml-cpp/yaml.h>   // for YAML::Node

// A definition of a block, parsed from a YAML file.
struct BlockDefinition {
    int id = -1;
    std::string name;
    // Texture or solid color:
    // If textureID != 0, we have a loaded texture (TODO: migrate to Vulkan VkImage)
    // Else if hasColor is true, use color.
    bool hasColor = false;
    glm::vec3 color = glm::vec3(0.0f);
    uint32_t textureID = 0;  // TODO: Change to VkImage when textures are migrated
    
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
    // Returns true on success.
    bool loadBlocks(const std::string& directory = "assets/blocks");

    // Get a block definition by numeric ID. Throws if invalid.
    const BlockDefinition& get(int id) const;
    // Get a block definition by block name. Throws if not found.
    const BlockDefinition& get(const std::string& name) const;
    
    // Get numeric ID for a given block name, or -1 if not found.
    int getID(const std::string& name) const;

    // Total number of block definitions (including "Air")
    int count() const { return (int)m_defs.size(); }

private:
    BlockRegistry();                        // private constructor (singleton)
    BlockRegistry(const BlockRegistry&) = delete;
    BlockRegistry& operator=(const BlockRegistry&) = delete;
    
    std::vector<BlockDefinition> m_defs;    // indexed by ID
    std::unordered_map<std::string,int> m_nameToID;
};
