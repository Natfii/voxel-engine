/**
 * @file structure_system.cpp
 * @brief Structure registry and spawning system implementation
 *
 * This file implements the StructureRegistry which handles:
 * - Loading structure definitions from YAML files
 * - Validating structure dimensions and chances
 * - Random variation selection
 * - Structure spawning in the world
 */

#include "structure_system.h"
#include "block_system.h"
#include "world.h"
#include "chunk.h"
#include "vulkan_renderer.h"
#include <iostream>
#include <filesystem>
#include <set>
#include <yaml-cpp/yaml.h>

StructureRegistry& StructureRegistry::instance() {
    static StructureRegistry registry;
    return registry;
}

StructureRegistry::StructureRegistry() {
    // Initialize random number generator with random seed
    std::random_device rd;
    m_rng = std::mt19937(rd());
    m_dist = std::uniform_int_distribution<int>(0, 99);  // 0-99 for percentage
}

bool StructureRegistry::loadStructures(const std::string& directory) {
    namespace fs = std::filesystem;
    fs::path dirPath(directory);

    if (!fs::exists(dirPath)) {
        std::cout << "StructureRegistry: Creating directory: " << directory << std::endl;
        fs::create_directories(dirPath);
        return true;  // Not an error - just no structures yet
    }

    if (!fs::is_directory(dirPath)) {
        std::cerr << "StructureRegistry: Not a directory: " << directory << std::endl;
        return false;
    }

    std::cout << "Loading structures from " << directory << "..." << std::endl;

    // Collect all YAML files
    std::vector<fs::path> yamlFiles;
    for (auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        auto ext = path.extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;
        yamlFiles.push_back(path);
    }

    if (yamlFiles.empty()) {
        std::cout << "No structure files found in " << directory << std::endl;
        return true;  // Not an error
    }

    // Load each structure file
    for (const auto& path : yamlFiles) {
        try {
            YAML::Node doc = YAML::LoadFile(path.string());

            if (!doc["name"]) {
                std::cerr << "Structure missing 'name' in " << path << std::endl;
                continue;
            }

            StructureDefinition structDef;
            structDef.name = doc["name"].as<std::string>();

            if (!doc["variations"]) {
                std::cerr << "Structure missing 'variations' in " << path << std::endl;
                continue;
            }

            // Parse all variations
            YAML::Node variations = doc["variations"];
            if (!variations.IsSequence()) {
                std::cerr << "Structure 'variations' must be a list in " << path << std::endl;
                continue;
            }

            int totalChance = 0;

            for (size_t i = 0; i < variations.size(); i++) {
                YAML::Node varNode = variations[i];
                StructureVariation var;

                // Parse dimensions
                if (!varNode["length"] || !varNode["width"] || !varNode["height"]) {
                    std::cerr << "Variation " << i << " missing dimensions in " << path << std::endl;
                    continue;
                }

                var.length = varNode["length"].as<int>();
                var.width = varNode["width"].as<int>();
                var.height = varNode["height"].as<int>();
                var.depth = varNode["depth"] ? varNode["depth"].as<int>() : 0;
                var.chance = varNode["chance"] ? varNode["chance"].as<int>() : 100;

                // Validate odd dimensions
                if (var.length % 2 == 0 || var.width % 2 == 0) {
                    std::cerr << "Variation " << i << " in " << path << " has even dimensions! "
                              << "Length and width must be odd. Got length=" << var.length
                              << ", width=" << var.width << std::endl;
                    continue;
                }

                totalChance += var.chance;

                // Parse structure data
                if (!varNode["structure"]) {
                    std::cerr << "Variation " << i << " missing 'structure' in " << path << std::endl;
                    continue;
                }

                YAML::Node structureNode = varNode["structure"];
                if (!structureNode.IsSequence()) {
                    std::cerr << "Variation " << i << " 'structure' must be a list of layers in " << path << std::endl;
                    continue;
                }

                // Parse each layer (Y level)
                for (size_t layerIdx = 0; layerIdx < structureNode.size(); layerIdx++) {
                    YAML::Node layerNode = structureNode[layerIdx];
                    if (!layerNode.IsSequence()) {
                        std::cerr << "Layer " << layerIdx << " must be a 2D array in " << path << std::endl;
                        continue;
                    }

                    std::vector<std::vector<int>> layer;

                    // Parse each row (Z axis)
                    for (size_t rowIdx = 0; rowIdx < layerNode.size(); rowIdx++) {
                        YAML::Node rowNode = layerNode[rowIdx];
                        if (!rowNode.IsSequence()) {
                            std::cerr << "Row " << rowIdx << " in layer " << layerIdx
                                      << " must be an array in " << path << std::endl;
                            continue;
                        }

                        std::vector<int> row;

                        // Parse each block (X axis)
                        for (size_t colIdx = 0; colIdx < rowNode.size(); colIdx++) {
                            int blockID = rowNode[colIdx].as<int>();
                            row.push_back(blockID);
                        }

                        layer.push_back(row);
                    }

                    var.structure.push_back(layer);
                }

                // Validate structure dimensions match declared dimensions
                if ((int)var.structure.size() != var.height) {
                    std::cerr << "Warning: Variation " << i << " in " << path
                              << " has height=" << var.height
                              << " but structure has " << var.structure.size() << " layers" << std::endl;
                }

                structDef.variations.push_back(var);
                std::cout << "  Loaded variation " << i << " for '" << structDef.name
                          << "' (" << var.length << "x" << var.width << "x" << var.height
                          << ", chance=" << var.chance << "%)" << std::endl;
            }

            // Validate total chance
            if (totalChance != 100) {
                std::cerr << "Warning: Total chance for '" << structDef.name
                          << "' is " << totalChance << "%, expected 100%" << std::endl;
            }

            // Add structure to registry
            if (!structDef.variations.empty()) {
                m_structures[structDef.name] = structDef;
                std::cout << "Loaded structure: " << structDef.name
                          << " with " << structDef.variations.size() << " variation(s)" << std::endl;
            }

        } catch (const std::exception& e) {
            std::cerr << "Error loading structure from " << path << ": " << e.what() << std::endl;
            continue;
        }
    }

    std::cout << "StructureRegistry: Loaded " << m_structures.size() << " structure(s)" << std::endl;
    return true;
}

const StructureDefinition* StructureRegistry::get(const std::string& name) const {
    auto it = m_structures.find(name);
    if (it == m_structures.end()) {
        return nullptr;
    }
    return &it->second;
}

const StructureVariation* StructureRegistry::selectVariation(const StructureDefinition& def) {
    if (def.variations.empty()) {
        return nullptr;
    }

    // If only one variation, return it
    if (def.variations.size() == 1) {
        return &def.variations[0];
    }

    // Random selection based on weighted chances
    int roll = m_dist(m_rng);  // 0-99
    int cumulative = 0;

    for (const auto& var : def.variations) {
        cumulative += var.chance;
        if (roll < cumulative) {
            return &var;
        }
    }

    // Fallback to first variation if something went wrong
    return &def.variations[0];
}

bool StructureRegistry::spawnStructure(const std::string& name, World* world, const glm::ivec3& centerPos, VulkanRenderer* renderer) {
    if (!world) {
        std::cerr << "StructureRegistry::spawnStructure: World is null" << std::endl;
        return false;
    }

    const StructureDefinition* structDef = get(name);
    if (!structDef) {
        std::cerr << "StructureRegistry::spawnStructure: Structure '" << name << "' not found" << std::endl;
        return false;
    }

    const StructureVariation* var = selectVariation(*structDef);
    if (!var) {
        std::cerr << "StructureRegistry::spawnStructure: No valid variation for '" << name << "'" << std::endl;
        return false;
    }

    std::cout << "Spawning structure '" << name << "' at ("
              << centerPos.x << ", " << centerPos.y << ", " << centerPos.z << ")" << std::endl;

    // Calculate offsets to center the structure
    int halfLength = var->length / 2;  // Integer division (e.g., 5/2 = 2)
    int halfWidth = var->width / 2;

    // Start position (bottom-left-back corner)
    glm::ivec3 startPos = centerPos;
    startPos.x -= halfLength;
    startPos.z -= halfWidth;
    startPos.y -= var->depth;  // Go down by depth amount

    // Track all affected chunks for mesh regeneration
    std::set<Chunk*> affectedChunks;

    // Place blocks layer by layer
    for (int y = 0; y < (int)var->structure.size(); y++) {
        const auto& layer = var->structure[y];

        for (int z = 0; z < (int)layer.size(); z++) {
            const auto& row = layer[z];

            for (int x = 0; x < (int)row.size(); x++) {
                int blockID = row[x];

                // Skip air blocks (ID 0)
                if (blockID == 0) {
                    continue;
                }

                glm::ivec3 blockPos = startPos + glm::ivec3(x, y, z);

                // Place the block in the world
                // Convert block coordinates to world coordinates (blocks are 1.0 units)
                float worldX = static_cast<float>(blockPos.x);
                float worldY = static_cast<float>(blockPos.y);
                float worldZ = static_cast<float>(blockPos.z);

                world->setBlockAt(worldX, worldY, worldZ, blockID);

                // Track the affected chunk
                Chunk* chunk = world->getChunkAtWorldPos(worldX, worldY, worldZ);
                if (chunk) {
                    affectedChunks.insert(chunk);
                }
            }
        }
    }

    // Regenerate meshes and GPU buffers for all affected chunks
    if (renderer) {
        std::cout << "Updating " << affectedChunks.size() << " affected chunk(s)..." << std::endl;
        for (Chunk* chunk : affectedChunks) {
            chunk->generateMesh(world);
            chunk->createVertexBuffer(renderer);
        }
    }

    std::cout << "Structure '" << name << "' spawned successfully!" << std::endl;
    return true;
}

std::vector<std::string> StructureRegistry::getAllStructureNames() const {
    std::vector<std::string> names;
    names.reserve(m_structures.size());

    for (const auto& pair : m_structures) {
        names.push_back(pair.first);
    }

    return names;
}
