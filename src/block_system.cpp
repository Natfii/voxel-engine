// block_system.cpp
#include "block_system.h"
#include <iostream>
#include <filesystem>

// stb_image - for texture loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

BlockRegistry& BlockRegistry::instance() {
    static BlockRegistry registry;
    return registry;
}

BlockRegistry::BlockRegistry() {
    // Create a default "Air" block with ID 0.
    BlockDefinition air;
    air.id = 0;
    air.name = "Air";
    air.durability = 0;
    air.affectedByGravity = false;
    air.flammability = 0;
    air.transparency = 1.0f;
    air.redstone = false;
    air.hasColor = false;
    air.textureID = 0;
    m_defs.push_back(air);
    m_nameToID[air.name] = 0;
}

bool BlockRegistry::loadBlocks(const std::string& directory) {
    namespace fs = std::filesystem;
    fs::path dirPath(directory);
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        std::cerr << "BlockRegistry: Directory not found: " << directory << std::endl;
        return false;
    }
    for (auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        auto ext = path.extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;
        // Parse the YAML file
        YAML::Node doc;
        try {
            doc = YAML::LoadFile(path.string());
        } catch (const std::exception& e) {
            std::cerr << "Error parsing YAML file " << path << ": " << e.what() << std::endl;
            continue;
        }
        if (!doc["name"]) {
            std::cerr << "YAML missing 'name' in " << path << std::endl;
            continue;
        }
        std::string name = doc["name"].as<std::string>();
        // Skip if name already exists
        if (m_nameToID.count(name)) {
            std::cerr << "Duplicate block name '" << name << "' in " << path << "; skipping." << std::endl;
            continue;
        }
        BlockDefinition def;
        def.id = (int)m_defs.size();
        def.name = name;
        
        // Durability (required)
        if (doc["durability"]) {
            def.durability = doc["durability"].as<int>();
        }
        // Affected by gravity (required)
        if (doc["affected_by_gravity"]) {
            def.affectedByGravity = doc["affected_by_gravity"].as<bool>();
        }
        // Optional: flammability, transparency, redstone
        if (doc["flammability"]) {
            def.flammability = doc["flammability"].as<int>();
        }
        if (doc["transparency"]) {
            def.transparency = doc["transparency"].as<float>();
        }
        // Store metadata node if exists
        if (doc["metadata"]) {
            def.metadata = doc["metadata"];
        }
        
        // Texture: either file path or color code
        if (doc["texture"]) {
            std::string tex = doc["texture"].as<std::string>();
            if (!tex.empty() && tex[0] == '#') {
                // Parse color hex code "#RRGGBB"
                std::string hex = tex.substr(1);
                if (hex.size() == 6) {
                    int rgb = std::stoi(hex, nullptr, 16);
                    float r = ((rgb >> 16) & 0xFF) / 255.0f;
                    float g = ((rgb >> 8) & 0xFF) / 255.0f;
                    float b = (rgb & 0xFF) / 255.0f;
                    def.hasColor = true;
                    def.color = glm::vec3(r, g, b);
                } else {
                    std::cerr << "Invalid color code in " << path << ": " << tex << std::endl;
                }
            } else {
                // TODO: Migrate texture loading to Vulkan VkImage
                // For now, texture loading is disabled - blocks will use their color or white
                std::cerr << "NOTE: Texture loading not yet implemented for Vulkan. Block "
                          << name << " will use solid color." << std::endl;

                // Temporarily set to white if no color defined
                if (!def.hasColor) {
                    def.hasColor = true;
                    def.color = glm::vec3(1.0f, 1.0f, 1.0f);
                }

                /* OpenGL texture loading (disabled for Vulkan migration):
                int width, height, channels;
                stbi_uc* data = stbi_load(tex.c_str(), &width, &height, &channels, 0);
                if (data) {
                    glGenTextures(1, &def.textureID);
                    glBindTexture(GL_TEXTURE_2D, def.textureID);
                    GLenum format = (channels == 4 ? GL_RGBA : GL_RGB);
                    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                    glGenerateMipmap(GL_TEXTURE_2D);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    stbi_image_free(data);
                    glBindTexture(GL_TEXTURE_2D, 0);
                } else {
                    std::cerr << "Failed to load texture " << tex << " for block " << name << std::endl;
                }
                */
            }
        }
        
        // Add to registry
        m_nameToID[def.name] = def.id;
        m_defs.push_back(def);
    }
    return true;
}

const BlockDefinition& BlockRegistry::get(int id) const {
    if (id < 0 || id >= (int)m_defs.size()) {
        throw std::out_of_range("BlockRegistry: ID out of range");
    }
    return m_defs[id];
}

const BlockDefinition& BlockRegistry::get(const std::string& name) const {
    auto it = m_nameToID.find(name);
    if (it == m_nameToID.end()) {
        throw std::out_of_range("BlockRegistry: Unknown block name: " + name);
    }
    return m_defs[it->second];
}

int BlockRegistry::getID(const std::string& name) const {
    auto it = m_nameToID.find(name);
    return (it == m_nameToID.end() ? -1 : it->second);
}
