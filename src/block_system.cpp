/**
 * @file block_system.cpp
 * @brief Block registry and texture atlas generation system
 *
 * This file implements the BlockRegistry and BlockSystemManager which handle:
 * - Loading block definitions from YAML files (assets/blocks/)
 * - Texture loading and automatic downscaling to 64x64
 * - Cube-mapped texture atlas generation (NxN grid of block faces)
 * - Mipmap generation for smooth LOD transitions
 * - UV coordinate calculation for texture atlas indexing
 * - Block type registry (singleton pattern)
 *
 * The texture atlas packs all block textures into a single GPU texture
 * for efficient rendering without texture switching.
 *
 * Created by original author
 */

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

    // Collect all YAML files first
    std::vector<fs::path> yamlFiles;
    for (auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        auto ext = path.extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;
        yamlFiles.push_back(path);
    }

    // Structure to hold blocks without explicit IDs
    struct PendingBlock {
        fs::path path;
        YAML::Node doc;
        std::string name;
    };
    std::vector<PendingBlock> pendingBlocks;
    int highestExplicitID = 0;

    std::cout << "Loading blocks (Pass 1: Explicit IDs)..." << std::endl;

    // PASS 1: Load all blocks with explicit IDs
    for (const auto& path : yamlFiles) {
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

        // Check if block has explicit ID
        if (!doc["id"]) {
            // No ID specified - defer to pass 2
            pendingBlocks.push_back({path, doc, name});
            continue;
        }

        // Has explicit ID - load immediately
        BlockDefinition def;
        def.id = doc["id"].as<int>();
        def.name = name;
        def.sourceFile = path.string();  // Store the original YAML file path

        // Track highest explicit ID
        if (def.id > highestExplicitID) {
            highestExplicitID = def.id;
        }

        // Durability (required)
        if (doc["durability"]) {
            def.durability = doc["durability"].as<int>();
        }
        // Affected by gravity (required)
        if (doc["affected_by_gravity"]) {
            def.affectedByGravity = doc["affected_by_gravity"].as<bool>();
        }
        // Optional: flammability, transparency, redstone, liquid
        if (doc["flammability"]) {
            def.flammability = doc["flammability"].as<int>();
        }
        if (doc["transparency"]) {
            def.transparency = doc["transparency"].as<float>();
        }
        if (doc["liquid"]) {
            def.isLiquid = doc["liquid"].as<bool>();
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

        // Ensure m_defs is large enough to hold this block at its ID
        if (def.id >= (int)m_defs.size()) {
            m_defs.resize(def.id + 1);
        }

        // Add to registry at the specified ID position
        m_defs[def.id] = def;
        m_nameToID[def.name] = def.id;
        std::cout << "  Loaded block: " << def.name << " (ID " << def.id << ")" << std::endl;
    }

    // PASS 2: Load blocks without explicit IDs (auto-assign starting from highestExplicitID + 1)
    if (!pendingBlocks.empty()) {
        std::cout << "\nLoading blocks (Pass 2: Auto-assigned IDs)..." << std::endl;
        std::cout << "  Highest explicit ID: " << highestExplicitID << std::endl;
        std::cout << "  Auto-assigning " << pendingBlocks.size() << " blocks starting from ID " << (highestExplicitID + 1) << std::endl;

        int nextAutoID = highestExplicitID + 1;

        for (const auto& pending : pendingBlocks) {
            BlockDefinition def;
            def.id = nextAutoID++;
            def.name = pending.name;
            def.sourceFile = pending.path.string();

            YAML::Node doc = pending.doc;

            // Durability (required)
            if (doc["durability"]) {
                def.durability = doc["durability"].as<int>();
            }
            // Affected by gravity (required)
            if (doc["affected_by_gravity"]) {
                def.affectedByGravity = doc["affected_by_gravity"].as<bool>();
            }
            // Optional: flammability, transparency, redstone, liquid
            if (doc["flammability"]) {
                def.flammability = doc["flammability"].as<int>();
            }
            if (doc["transparency"]) {
                def.transparency = doc["transparency"].as<float>();
            }
            if (doc["liquid"]) {
                def.isLiquid = doc["liquid"].as<bool>();
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
                        std::cerr << "ERROR: Invalid color code for " << def.name << " - using error block" << std::endl;
                    }
                }
            }

            // Ensure m_defs is large enough
            if (def.id >= (int)m_defs.size()) {
                m_defs.resize(def.id + 1);
            }

            // Add to registry
            m_defs[def.id] = def;
            m_nameToID[def.name] = def.id;
            std::cout << "  Loaded block: " << def.name << " (ID " << def.id << ", auto-assigned)" << std::endl;
        }
    }

    // Build texture atlas after all blocks are loaded
    if (renderer) {
        buildTextureAtlas(renderer);
    }

    // Debug: Print final block registry state
    std::cout << "\nBlock Registry Summary:" << std::endl;
    for (size_t i = 0; i < m_defs.size(); i++) {
        const BlockDefinition& def = m_defs[i];
        if (def.name.empty()) {
            std::cout << "  ID " << i << ": [empty slot]" << std::endl;
        } else {
            std::cout << "  ID " << i << ": " << def.name;
            if (def.useCubeMap) {
                std::cout << " (cube map)";
            } else if (def.hasTexture) {
                std::cout << " (textured)";
            } else if (def.hasColor) {
                std::cout << " (colored)";
            }
            std::cout << std::endl;
        }
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

        // Skip empty slots (blocks loaded with explicit IDs may have gaps)
        if (def.name.empty()) {
            continue;
        }

        // Re-parse YAML to get texture information using stored source file
        if (def.sourceFile.empty()) {
            std::cerr << "Error: No source file stored for " << def.name << std::endl;
            continue;
        }

        YAML::Node doc;
        try {
            doc = YAML::LoadFile(def.sourceFile);
        } catch (const std::exception& e) {
            std::cerr << "Error re-parsing YAML for " << def.name << " from " << def.sourceFile << ": " << e.what() << std::endl;
            continue;
        }

        // Extract base filename (without extension) for default texture lookup
        std::filesystem::path sourcePath(def.sourceFile);
        std::string lowerName = sourcePath.stem().string();  // Get filename without extension
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                     [](unsigned char c){ return std::tolower(c); });

        if (doc["cube_map"]) {
            // Cube map mode
            def.useCubeMap = true;
            def.hasTexture = true;

            YAML::Node cubeMap = doc["cube_map"];

            // Helper to parse texture string with optional variation: "texture.png,1.5"
            auto parseTexture = [](const std::string& texString) -> std::pair<std::string, float> {
                size_t commaPos = texString.find(',');
                if (commaPos != std::string::npos) {
                    std::string filename = texString.substr(0, commaPos);
                    std::string variationStr = texString.substr(commaPos + 1);
                    try {
                        float variation = std::stof(variationStr);
                        return {filename, variation};
                    } catch (...) {
                        std::cerr << "Warning: Invalid variation value '" << variationStr << "', using 1.0" << std::endl;
                        return {filename, 1.0f};
                    }
                }
                return {texString, 1.0f};  // Default: no variation
            };

            // Collect all explicitly defined textures to detect single-texture mode
            std::vector<std::string> definedTextures;
            std::vector<std::string> faceNames = {"top", "bottom", "front", "back", "left", "right", "sides", "all"};
            for (const auto& faceName : faceNames) {
                if (cubeMap[faceName]) {
                    std::string texString = cubeMap[faceName].as<std::string>();
                    auto [filename, variation] = parseTexture(texString);
                    if (std::find(definedTextures.begin(), definedTextures.end(), texString) == definedTextures.end()) {
                        definedTextures.push_back(texString);
                    }
                }
            }

            // If only one texture is defined, use it as the universal default
            std::string defaultTexture;
            if (definedTextures.size() == 1) {
                defaultTexture = definedTextures[0];
                auto [filename, variation] = parseTexture(defaultTexture);
                std::cout << "  Single texture detected for " << def.name << ", using '" << filename << "' (variation=" << variation << ") for all faces" << std::endl;
            }

            // Helper to load a face texture with smart fallback logic
            auto loadFace = [&](const std::string& faceName, BlockDefinition::FaceTexture& face) {
                std::string texString;

                // Priority 1: Face-specific texture
                if (cubeMap[faceName]) {
                    texString = cubeMap[faceName].as<std::string>();
                }
                // Priority 2: "sides" for horizontal faces
                else if (cubeMap["sides"] && (faceName == "front" || faceName == "back" || faceName == "left" || faceName == "right")) {
                    texString = cubeMap["sides"].as<std::string>();
                }
                // Priority 3: "all" fallback
                else if (cubeMap["all"]) {
                    texString = cubeMap["all"].as<std::string>();
                }
                // Priority 4: Use the single defined texture if only one exists
                else if (!defaultTexture.empty()) {
                    texString = defaultTexture;
                }
                else {
                    return;
                }

                // Parse texture filename and variation
                auto [filename, variation] = parseTexture(texString);

                int atlasIndex = addTextureToAtlas(filename, def.name);
                if (atlasIndex >= 0) {
                    // Atlas coordinates will be calculated after we know the grid size
                    face.atlasX = atlasIndex;  // Temporarily store atlas index
                    face.atlasY = 0;
                    face.variation = variation;  // Store per-face variation
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
            std::string texString = doc["texture"].as<std::string>();

            // Skip hex colors
            if (!texString.empty() && texString[0] == '#') {
                continue;
            }

            // Parse texture filename and variation
            size_t commaPos = texString.find(',');
            std::string filename = texString;
            float variation = 1.0f;

            if (commaPos != std::string::npos) {
                filename = texString.substr(0, commaPos);
                std::string variationStr = texString.substr(commaPos + 1);
                try {
                    variation = std::stof(variationStr);
                } catch (...) {
                    std::cerr << "Warning: Invalid variation value '" << variationStr << "', using 1.0" << std::endl;
                }
            }

            int atlasIndex = addTextureToAtlas(filename, def.name);
            if (atlasIndex >= 0) {
                def.hasTexture = true;
                def.useCubeMap = false;
                // Store atlas index and variation temporarily
                def.all.atlasX = atlasIndex;
                def.all.atlasY = 0;
                def.all.variation = variation;
            }
        } else {
            // No texture or cube_map defined - try to load {blockname}.png automatically
            std::string defaultTexName = lowerName + ".png";
            std::cout << "  No texture defined for " << def.name << ", attempting to load " << defaultTexName << std::endl;

            int atlasIndex = addTextureToAtlas(defaultTexName, def.name);
            if (atlasIndex >= 0) {
                def.hasTexture = true;
                def.useCubeMap = false;
                def.all.atlasX = atlasIndex;
                def.all.atlasY = 0;
                def.all.variation = 1.0f;  // No variation by default
                std::cout << "  Auto-loaded default texture for " << def.name << std::endl;
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

        // Skip empty slots
        if (def.name.empty()) {
            continue;
        }

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

// ============================================================================
// BlockIconRenderer Implementation
// ============================================================================

VkDescriptorSet BlockIconRenderer::s_atlasDescriptorSet = VK_NULL_HANDLE;

void BlockIconRenderer::init(VkDescriptorSet atlasDescriptorSet) {
    s_atlasDescriptorSet = atlasDescriptorSet;
}

void BlockIconRenderer::drawBlockIcon(ImDrawList* drawList, const ImVec2& pos, float size, int blockID) {
    auto& registry = BlockRegistry::instance();
    if (blockID <= 0 || blockID >= registry.count()) {
        return;
    }

    const BlockDefinition& block = registry.get(blockID);

    // Use textured version if we have the atlas and the block has textures
    if (s_atlasDescriptorSet != VK_NULL_HANDLE && block.hasTexture) {
        drawIsometricCubeTextured(drawList, pos, size, blockID);
    } else {
        // Fallback to color version
        ImVec4 topColor = getBlockColor(blockID, 0);    // Top face
        ImVec4 leftColor = getBlockColor(blockID, 1);   // Left face
        ImVec4 rightColor = getBlockColor(blockID, 2);  // Right face
        drawIsometricCube(drawList, pos, size, topColor, leftColor, rightColor);
    }
}

void BlockIconRenderer::drawBlockPreview(ImDrawList* drawList, const ImVec2& pos, float size, int blockID) {
    // Same as icon but larger
    drawBlockIcon(drawList, pos, size, blockID);
}

ImVec4 BlockIconRenderer::getBlockColor(int blockID, int face) {
    auto& registry = BlockRegistry::instance();
    if (blockID <= 0 || blockID >= registry.count()) {
        return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    }

    const BlockDefinition& block = registry.get(blockID);

    // If block has a solid color, use it
    if (block.hasColor) {
        ImVec4 baseColor = ImVec4(block.color.r, block.color.g, block.color.b, 1.0f);

        // Apply shading based on face (isometric lighting)
        switch (face) {
            case 0: // Top - brightest
                return baseColor;
            case 1: // Left - medium
                return ImVec4(baseColor.x * 0.75f, baseColor.y * 0.75f, baseColor.z * 0.75f, 1.0f);
            case 2: // Right - darkest
                return ImVec4(baseColor.x * 0.55f, baseColor.y * 0.55f, baseColor.z * 0.55f, 1.0f);
        }
    }

    // For textured blocks, use a neutral gray with shading
    switch (face) {
        case 0: return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);  // Top
        case 1: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // Left
        case 2: return ImVec4(0.45f, 0.45f, 0.45f, 1.0f); // Right
    }

    return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
}

void BlockIconRenderer::getTextureUVs(int blockID, int face, ImVec2& uv0, ImVec2& uv1) {
    // NOTE: This function works with dynamically loaded blocks at runtime
    // Users can add new block YAML files and textures to assets/blocks/
    // and they will automatically appear with correct icons in the inventory
    auto& registry = BlockRegistry::instance();
    if (blockID <= 0 || blockID >= registry.count()) {
        uv0 = ImVec2(0, 0);
        uv1 = ImVec2(1, 1);
        return;
    }

    const BlockDefinition& block = registry.get(blockID);
    // Query atlas grid size dynamically - this adjusts based on how many blocks are loaded
    int gridSize = registry.getAtlasGridSize();
    if (gridSize == 0) gridSize = 1;

    // Get the appropriate face texture coordinates
    const BlockDefinition::FaceTexture* faceTexture = nullptr;

    if (block.useCubeMap) {
        switch (face) {
            case 0: faceTexture = &block.top; break;    // Top
            case 1: faceTexture = &block.left; break;   // Left
            case 2: faceTexture = &block.right; break;  // Right
            default: faceTexture = &block.all; break;
        }
    } else {
        faceTexture = &block.all;
    }

    // Calculate UV coordinates from atlas grid position
    float cellSize = 1.0f / gridSize;
    uv0 = ImVec2(faceTexture->atlasX * cellSize, faceTexture->atlasY * cellSize);
    uv1 = ImVec2((faceTexture->atlasX + 1) * cellSize, (faceTexture->atlasY + 1) * cellSize);
}

void BlockIconRenderer::drawIsometricCubeTextured(ImDrawList* drawList, const ImVec2& center, float size, int blockID) {
    // Isometric projection constants
    const float isoAngle = 0.5f;
    const float halfSize = size * 0.5f;

    // Calculate face vertices
    ImVec2 topFace[4] = {
        ImVec2(center.x, center.y - size * 0.35f),                    // Top point
        ImVec2(center.x + size * 0.45f, center.y - size * 0.1f),      // Right point
        ImVec2(center.x, center.y + size * 0.15f),                    // Bottom point
        ImVec2(center.x - size * 0.45f, center.y - size * 0.1f)       // Left point
    };

    ImVec2 leftFace[4] = {
        ImVec2(center.x - size * 0.45f, center.y - size * 0.1f),      // Top-left
        ImVec2(center.x, center.y + size * 0.15f),                    // Top-right
        ImVec2(center.x, center.y + size * 0.5f),                     // Bottom-right
        ImVec2(center.x - size * 0.45f, center.y + size * 0.25f)      // Bottom-left
    };

    ImVec2 rightFace[4] = {
        ImVec2(center.x + size * 0.45f, center.y - size * 0.1f),      // Top-right
        ImVec2(center.x, center.y + size * 0.15f),                    // Top-left
        ImVec2(center.x, center.y + size * 0.5f),                     // Bottom-left
        ImVec2(center.x + size * 0.45f, center.y + size * 0.25f)      // Bottom-right
    };

    // Get UV coordinates for each face
    ImVec2 uvTop0, uvTop1, uvLeft0, uvLeft1, uvRight0, uvRight1;
    getTextureUVs(blockID, 0, uvTop0, uvTop1);     // Top
    getTextureUVs(blockID, 1, uvLeft0, uvLeft1);   // Left
    getTextureUVs(blockID, 2, uvRight0, uvRight1); // Right

    ImTextureID texID = (ImTextureID)s_atlasDescriptorSet;

    // Draw faces in back-to-front order with textures
    // Right face (darkened)
    drawList->AddImageQuad(texID,
        rightFace[0], rightFace[1], rightFace[2], rightFace[3],
        ImVec2(uvRight0.x, uvRight0.y), ImVec2(uvRight1.x, uvRight0.y),
        ImVec2(uvRight1.x, uvRight1.y), ImVec2(uvRight0.x, uvRight1.y),
        IM_COL32(140, 140, 140, 255)); // Darken right face
    drawList->AddQuad(rightFace[0], rightFace[1], rightFace[2], rightFace[3],
        IM_COL32(0, 0, 0, 100), 1.0f);

    // Left face (medium shade)
    drawList->AddImageQuad(texID,
        leftFace[0], leftFace[1], leftFace[2], leftFace[3],
        ImVec2(uvLeft0.x, uvLeft0.y), ImVec2(uvLeft1.x, uvLeft0.y),
        ImVec2(uvLeft1.x, uvLeft1.y), ImVec2(uvLeft0.x, uvLeft1.y),
        IM_COL32(190, 190, 190, 255)); // Medium shade left face
    drawList->AddQuad(leftFace[0], leftFace[1], leftFace[2], leftFace[3],
        IM_COL32(0, 0, 0, 100), 1.0f);

    // Top face (brightest)
    drawList->AddImageQuad(texID,
        topFace[0], topFace[1], topFace[2], topFace[3],
        ImVec2(uvTop0.x, uvTop0.y), ImVec2(uvTop1.x, uvTop0.y),
        ImVec2(uvTop1.x, uvTop1.y), ImVec2(uvTop0.x, uvTop1.y),
        IM_COL32(255, 255, 255, 255)); // Full bright top face
    drawList->AddQuad(topFace[0], topFace[1], topFace[2], topFace[3],
        IM_COL32(0, 0, 0, 120), 1.5f);
}

void BlockIconRenderer::drawIsometricCube(ImDrawList* drawList, const ImVec2& center, float size,
                                          const ImVec4& topColor, const ImVec4& leftColor, const ImVec4& rightColor) {
    // Isometric projection constants
    // Standard isometric: 30 degree angle
    const float isoAngle = 0.5f; // tan(30°) ≈ 0.577, using 0.5 for simplicity
    const float halfSize = size * 0.5f;

    // Calculate the 7 vertices of the isometric cube projection
    // Center the cube at 'center'
    ImVec2 top = ImVec2(center.x, center.y - halfSize);                           // Top vertex
    ImVec2 left = ImVec2(center.x - halfSize, center.y);                          // Left vertex
    ImVec2 right = ImVec2(center.x + halfSize, center.y);                         // Right vertex
    ImVec2 bottom = ImVec2(center.x, center.y + halfSize);                        // Bottom vertex (center of base)
    ImVec2 topLeft = ImVec2(center.x - halfSize * isoAngle, center.y - halfSize * isoAngle);
    ImVec2 topRight = ImVec2(center.x + halfSize * isoAngle, center.y - halfSize * isoAngle);
    ImVec2 bottomLeft = ImVec2(center.x - halfSize * isoAngle, center.y + halfSize * isoAngle);
    ImVec2 bottomRight = ImVec2(center.x + halfSize * isoAngle, center.y + halfSize * isoAngle);

    // Simplified isometric cube using diamonds/rhombus shapes
    // We'll draw three visible faces: top, left, right

    // Top face (brightest) - diamond shape
    ImVec2 topFace[4] = {
        ImVec2(center.x, center.y - size * 0.35f),                    // Top point
        ImVec2(center.x + size * 0.45f, center.y - size * 0.1f),      // Right point
        ImVec2(center.x, center.y + size * 0.15f),                    // Bottom point
        ImVec2(center.x - size * 0.45f, center.y - size * 0.1f)       // Left point
    };

    // Left face (medium shade)
    ImVec2 leftFace[4] = {
        ImVec2(center.x - size * 0.45f, center.y - size * 0.1f),      // Top-left
        ImVec2(center.x, center.y + size * 0.15f),                    // Top-right
        ImVec2(center.x, center.y + size * 0.5f),                     // Bottom-right
        ImVec2(center.x - size * 0.45f, center.y + size * 0.25f)      // Bottom-left
    };

    // Right face (darkest)
    ImVec2 rightFace[4] = {
        ImVec2(center.x + size * 0.45f, center.y - size * 0.1f),      // Top-right
        ImVec2(center.x, center.y + size * 0.15f),                    // Top-left
        ImVec2(center.x, center.y + size * 0.5f),                     // Bottom-left
        ImVec2(center.x + size * 0.45f, center.y + size * 0.25f)      // Bottom-right
    };

    // Draw faces in back-to-front order for proper occlusion
    // Right face (back-right)
    drawList->AddQuadFilled(rightFace[0], rightFace[1], rightFace[2], rightFace[3],
                            ImGui::ColorConvertFloat4ToU32(rightColor));
    drawList->AddQuad(rightFace[0], rightFace[1], rightFace[2], rightFace[3],
                      IM_COL32(0, 0, 0, 100), 1.0f);

    // Left face (back-left)
    drawList->AddQuadFilled(leftFace[0], leftFace[1], leftFace[2], leftFace[3],
                            ImGui::ColorConvertFloat4ToU32(leftColor));
    drawList->AddQuad(leftFace[0], leftFace[1], leftFace[2], leftFace[3],
                      IM_COL32(0, 0, 0, 100), 1.0f);

    // Top face (front)
    drawList->AddQuadFilled(topFace[0], topFace[1], topFace[2], topFace[3],
                            ImGui::ColorConvertFloat4ToU32(topColor));
    drawList->AddQuad(topFace[0], topFace[1], topFace[2], topFace[3],
                      IM_COL32(0, 0, 0, 120), 1.5f);
}
