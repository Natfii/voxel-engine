// block_system.cpp
#include "block_system.h"
#include "vulkan_renderer.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>

// stb_image - for texture loading and resizing
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

// Helper structure to hold loaded texture data before atlas building
struct LoadedTexture {
    unsigned char* pixels = nullptr;  // RGBA data (64x64)
    int width = 64;
    int height = 64;
    std::string name;
};

// Helper function to load and downscale a texture to 64x64
static LoadedTexture loadAndResizeTexture(const std::string& texturePath, const std::string& name) {
    LoadedTexture tex;
    tex.name = name;

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        return tex;  // pixels = nullptr indicates failure
    }

    // Auto-downscale to 64x64 if needed
    if (texWidth != 64 || texHeight != 64) {
        std::cout << "Resizing texture " << texturePath << " from " << texWidth << "x" << texHeight << " to 64x64" << std::endl;

        unsigned char* resizedPixels = (unsigned char*)malloc(64 * 64 * 4);  // 64x64 RGBA
        stbir_resize_uint8_linear(pixels, texWidth, texHeight, 0,
                                  resizedPixels, 64, 64, 0,
                                  STBIR_RGBA);

        stbi_image_free(pixels);
        tex.pixels = resizedPixels;
    } else {
        tex.pixels = pixels;
    }

    return tex;
}

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
    air.hasTexture = false;
    m_defs.push_back(air);
    m_nameToID[air.name] = 0;
}

bool BlockRegistry::loadBlocks(const std::string& directory, VulkanRenderer* renderer) {
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
        // Texture variation (zoom factor for random sampling)
        if (doc["texture_variation"]) {
            def.textureVariation = doc["texture_variation"].as<float>();
        }
        // Store metadata node if exists
        if (doc["metadata"]) {
            def.metadata = doc["metadata"];
        }
        
        // Parse texture field (either PNG filename, hex color, or cube_map)
        if (doc["cube_map"]) {
            // Cube map mode: different textures for different faces
            def.useCubeMap = true;
            // Textures will be loaded during atlas building
        } else if (doc["texture"]) {
            std::string tex = doc["texture"].as<std::string>();

            // Parse hex color as fallback
            if (!tex.empty() && tex[0] == '#') {
                std::string hex = tex.substr(1);
                if (hex.size() == 6) {
                    int rgb = std::stoi(hex, nullptr, 16);
                    float r = ((rgb >> 16) & 0xFF) / 255.0f;
                    float g = ((rgb >> 8) & 0xFF) / 255.0f;
                    float b = (rgb & 0xFF) / 255.0f;
                    def.hasColor = true;
                    def.color = glm::vec3(r, g, b);
                } else {
                    // Invalid color code - use error color (semi-transparent red)
                    def.hasColor = true;
                    def.color = glm::vec3(1.0f, 0.0f, 0.0f);
                    def.transparency = 0.5f;
                    std::cerr << "ERROR: Invalid color code for " << name << " - using error block" << std::endl;
                }
            }
        }
        
        // Add to registry
        m_nameToID[def.name] = def.id;
        m_defs.push_back(def);
    }

    // Build texture atlas after all blocks are loaded
    if (renderer) {
        buildTextureAtlas(renderer);
    }

    return true;
}

void BlockRegistry::buildTextureAtlas(VulkanRenderer* renderer) {
    std::cout << "Building texture atlas with cube map support..." << std::endl;

    // Map texture names to atlas indices to avoid duplicates
    std::unordered_map<std::string, int> textureNameToAtlasIndex;
    std::vector<LoadedTexture> textures;

    // Helper to add a texture to the atlas if not already present
    auto addTextureToAtlas = [&](const std::string& textureName, const std::string& blockName) -> int {
        // Check if already loaded
        auto it = textureNameToAtlasIndex.find(textureName);
        if (it != textureNameToAtlasIndex.end()) {
            return it->second;
        }

        // Load the texture
        std::string texturePath = "assets/blocks/" + textureName;
        LoadedTexture tex = loadAndResizeTexture(texturePath, blockName + ":" + textureName);

        if (!tex.pixels) {
            std::cerr << "  Failed to load texture: " << textureName << std::endl;
            return -1;
        }

        int atlasIndex = (int)textures.size();
        textures.push_back(tex);
        textureNameToAtlasIndex[textureName] = atlasIndex;
        std::cout << "  Loaded texture: " << textureName << " (atlas index " << atlasIndex << ")" << std::endl;
        return atlasIndex;
    };

    // First pass: Load all textures from YAML definitions
    for (size_t i = 1; i < m_defs.size(); i++) {  // Skip air (index 0)
        BlockDefinition& def = m_defs[i];

        // Re-parse YAML to get texture information
        std::string lowerName = def.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                     [](unsigned char c){ return std::tolower(c); });

        std::string yamlPath = "assets/blocks/" + lowerName + ".yaml";
        YAML::Node doc;
        try {
            doc = YAML::LoadFile(yamlPath);
        } catch (const std::exception& e) {
            std::cerr << "Error re-parsing YAML for " << def.name << ": " << e.what() << std::endl;
            continue;
        }

        if (doc["cube_map"]) {
            // Cube map mode
            def.useCubeMap = true;
            def.hasTexture = true;

            YAML::Node cubeMap = doc["cube_map"];

            // Helper to load a face texture, with fallback to "sides" or "all"
            auto loadFace = [&](const std::string& faceName, BlockDefinition::FaceTexture& face) {
                std::string texName;
                if (cubeMap[faceName]) {
                    texName = cubeMap[faceName].as<std::string>();
                } else if (cubeMap["sides"] && (faceName == "front" || faceName == "back" || faceName == "left" || faceName == "right")) {
                    texName = cubeMap["sides"].as<std::string>();
                } else if (cubeMap["all"]) {
                    texName = cubeMap["all"].as<std::string>();
                } else {
                    return;
                }

                int atlasIndex = addTextureToAtlas(texName, def.name);
                if (atlasIndex >= 0) {
                    // Atlas coordinates will be calculated after we know the grid size
                    face.atlasX = atlasIndex;  // Temporarily store atlas index
                    face.atlasY = 0;
                }
            };

            loadFace("top", def.top);
            loadFace("bottom", def.bottom);
            loadFace("front", def.front);
            loadFace("back", def.back);
            loadFace("left", def.left);
            loadFace("right", def.right);

            std::cout << "  Loaded cube map for " << def.name << std::endl;
        } else if (doc["texture"]) {
            // Simple texture mode (backwards compatibility)
            std::string texName = doc["texture"].as<std::string>();

            // Skip hex colors
            if (!texName.empty() && texName[0] == '#') {
                continue;
            }

            int atlasIndex = addTextureToAtlas(texName, def.name);
            if (atlasIndex >= 0) {
                def.hasTexture = true;
                def.useCubeMap = false;
                // Store atlas index temporarily
                def.all.atlasX = atlasIndex;
                def.all.atlasY = 0;
            }
        }
    }

    if (textures.empty()) {
        std::cout << "No textures loaded, skipping atlas creation" << std::endl;
        return;
    }

    // Calculate atlas grid size (round up to next power of 2)
    int numTextures = (int)textures.size();
    m_atlasGridSize = 1;
    while (m_atlasGridSize * m_atlasGridSize < numTextures) {
        m_atlasGridSize *= 2;
    }

    int atlasSize = m_atlasGridSize * 64;  // Each slot is 64x64
    std::cout << "Atlas grid: " << m_atlasGridSize << "x" << m_atlasGridSize << " (" << atlasSize << "x" << atlasSize << " pixels, " << numTextures << " textures)" << std::endl;

    // Create atlas pixel data (RGBA)
    size_t atlasDataSize = atlasSize * atlasSize * 4;
    unsigned char* atlasPixels = (unsigned char*)calloc(atlasDataSize, 1);  // Initialize to zeros

    // Copy textures into atlas
    for (int atlasIndex = 0; atlasIndex < (int)textures.size(); atlasIndex++) {
        LoadedTexture& tex = textures[atlasIndex];

        int atlasX = atlasIndex % m_atlasGridSize;
        int atlasY = atlasIndex / m_atlasGridSize;

        // Copy texture pixels into atlas
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int srcIdx = (y * 64 + x) * 4;
                int dstX = atlasX * 64 + x;
                int dstY = atlasY * 64 + y;
                int dstIdx = (dstY * atlasSize + dstX) * 4;

                atlasPixels[dstIdx + 0] = tex.pixels[srcIdx + 0];  // R
                atlasPixels[dstIdx + 1] = tex.pixels[srcIdx + 1];  // G
                atlasPixels[dstIdx + 2] = tex.pixels[srcIdx + 2];  // B
                atlasPixels[dstIdx + 3] = tex.pixels[srcIdx + 3];  // A
            }
        }
    }

    // Second pass: Convert atlas indices to atlas grid coordinates
    for (size_t i = 1; i < m_defs.size(); i++) {
        BlockDefinition& def = m_defs[i];
        if (!def.hasTexture) continue;

        auto convertFace = [&](BlockDefinition::FaceTexture& face) {
            int atlasIndex = face.atlasX;  // We stored the index here temporarily
            face.atlasX = atlasIndex % m_atlasGridSize;
            face.atlasY = atlasIndex / m_atlasGridSize;
        };

        if (def.useCubeMap) {
            convertFace(def.top);
            convertFace(def.bottom);
            convertFace(def.front);
            convertFace(def.back);
            convertFace(def.left);
            convertFace(def.right);
        } else {
            convertFace(def.all);
        }
    }

    // Free individual texture memory
    for (auto& tex : textures) {
        if (tex.pixels) {
            stbi_image_free(tex.pixels);
        }
    }

    // Upload atlas to GPU
    VkDeviceSize imageSize = atlasDataSize;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    renderer->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(renderer->getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, atlasPixels, atlasDataSize);
    vkUnmapMemory(renderer->getDevice(), stagingBufferMemory);

    free(atlasPixels);  // Free CPU atlas memory

    // Create Vulkan image
    renderer->createImage(atlasSize, atlasSize, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         m_atlasImage, m_atlasMemory);

    // Transition image layout and copy
    renderer->transitionImageLayout(m_atlasImage, VK_FORMAT_R8G8B8A8_SRGB,
                                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    renderer->copyBufferToImage(stagingBuffer, m_atlasImage, atlasSize, atlasSize);
    renderer->transitionImageLayout(m_atlasImage, VK_FORMAT_R8G8B8A8_SRGB,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Cleanup staging buffer
    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);

    // Create image view and sampler
    m_atlasImageView = renderer->createImageView(m_atlasImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    m_atlasSampler = renderer->createTextureSampler();

    std::cout << "Texture atlas created successfully!" << std::endl;
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

std::string BlockRegistry::getBlockName(int blockID) const {
    if (blockID < 0 || blockID >= (int)m_defs.size()) {
        return "Unknown";
    }
    return m_defs[blockID].name;
}

std::string BlockRegistry::getBlockType(int blockID) const {
    if (blockID < 0 || blockID >= (int)m_defs.size()) {
        return "unknown";
    }

    const BlockDefinition& def = m_defs[blockID];

    // Classify block type based on properties
    if (blockID == BlockID::AIR) {
        return "air";
    }
    if (def.transparency > 0.5f) {
        return "transparent";
    }
    // Future: Add liquid detection when liquids are implemented
    // if (def.isLiquid) return "liquid";

    return "solid";
}

bool BlockRegistry::isBreakable(int blockID) const {
    if (blockID < 0 || blockID >= (int)m_defs.size()) {
        return false;
    }

    // Air is not breakable
    if (blockID == BlockID::AIR) {
        return false;
    }

    // All non-air blocks are breakable for now
    // Future: Check durability or special flags
    return true;
}
