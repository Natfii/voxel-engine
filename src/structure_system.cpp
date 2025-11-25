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
#include "logger.h"
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
        Logger::info() << "StructureRegistry: Creating directory: " << directory;
        fs::create_directories(dirPath);
        return true;  // Not an error - just no structures yet
    }

    if (!fs::is_directory(dirPath)) {
        Logger::error() << "StructureRegistry: Not a directory: " << directory;
        return false;
    }

    Logger::info() << "Loading structures from " << directory << "...";

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
        Logger::info() << "No structure files found in " << directory;
        return true;  // Not an error
    }

    // Load each structure file
    for (const auto& path : yamlFiles) {
        try {
            YAML::Node doc = YAML::LoadFile(path.string());

            if (!doc["name"]) {
                Logger::error() << "Structure missing 'name' in " << path.string();
                continue;
            }

            StructureDefinition structDef;
            structDef.name = doc["name"].as<std::string>();

            if (!doc["variations"]) {
                Logger::error() << "Structure missing 'variations' in " << path.string();
                continue;
            }

            // Parse all variations
            YAML::Node variations = doc["variations"];
            if (!variations.IsSequence()) {
                Logger::error() << "Structure 'variations' must be a list in " << path.string();
                continue;
            }

            int totalChance = 0;

            for (size_t i = 0; i < variations.size(); i++) {
                YAML::Node varNode = variations[i];
                StructureVariation var;

                // Parse dimensions
                if (!varNode["length"] || !varNode["width"] || !varNode["height"]) {
                    Logger::error() << "Variation " << i << " missing dimensions in " << path.string();
                    continue;
                }

                var.length = varNode["length"].as<int>();
                var.width = varNode["width"].as<int>();
                var.height = varNode["height"].as<int>();
                var.depth = varNode["depth"] ? varNode["depth"].as<int>() : 0;
                var.chance = varNode["chance"] ? varNode["chance"].as<int>() : 100;

                // Validate odd dimensions
                if (var.length % 2 == 0 || var.width % 2 == 0) {
                    Logger::error() << "Variation " << i << " in " << path.string() << " has even dimensions! "
                                   << "Length and width must be odd. Got length=" << var.length
                                   << ", width=" << var.width;
                    continue;
                }

                totalChance += var.chance;

                // Parse structure data
                if (!varNode["structure"]) {
                    Logger::error() << "Variation " << i << " missing 'structure' in " << path.string();
                    continue;
                }

                YAML::Node structureNode = varNode["structure"];
                if (!structureNode.IsSequence()) {
                    Logger::error() << "Variation " << i << " 'structure' must be a list of layers in " << path.string();
                    continue;
                }

                // Parse each layer (Y level)
                for (size_t layerIdx = 0; layerIdx < structureNode.size(); layerIdx++) {
                    YAML::Node layerNode = structureNode[layerIdx];
                    if (!layerNode.IsSequence()) {
                        Logger::error() << "Layer " << layerIdx << " must be a 2D array in " << path.string();
                        continue;
                    }

                    std::vector<std::vector<int>> layer;

                    // Parse each row (Z axis)
                    for (size_t rowIdx = 0; rowIdx < layerNode.size(); rowIdx++) {
                        YAML::Node rowNode = layerNode[rowIdx];
                        if (!rowNode.IsSequence()) {
                            Logger::error() << "Row " << rowIdx << " in layer " << layerIdx
                                           << " must be an array in " << path.string();
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
                    Logger::warning() << "Variation " << i << " in " << path.string()
                                     << " has height=" << var.height
                                     << " but structure has " << var.structure.size() << " layers";
                }

                structDef.variations.push_back(var);
                Logger::debug() << "  Loaded variation " << i << " for '" << structDef.name
                               << "' (" << var.length << "x" << var.width << "x" << var.height
                               << ", chance=" << var.chance << "%)";
            }

            // Validate total chance
            if (totalChance != 100) {
                Logger::warning() << "Total chance for '" << structDef.name
                                 << "' is " << totalChance << "%, expected 100%";
            }

            // Add structure to registry
            if (!structDef.variations.empty()) {
                m_structures[structDef.name] = structDef;
                Logger::debug() << "Loaded structure: " << structDef.name
                               << " with " << structDef.variations.size() << " variation(s)";
            }

        } catch (const std::exception& e) {
            Logger::error() << "Error loading structure from " << path.string() << ": " << e.what();
            continue;
        }
    }

    Logger::info() << "StructureRegistry: Loaded " << m_structures.size() << " structure(s)";
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
        Logger::error() << "StructureRegistry::spawnStructure: World is null";
        return false;
    }

    const StructureDefinition* structDef = get(name);
    if (!structDef) {
        Logger::error() << "StructureRegistry::spawnStructure: Structure '" << name << "' not found";
        return false;
    }

    const StructureVariation* var = selectVariation(*structDef);
    if (!var) {
        Logger::error() << "StructureRegistry::spawnStructure: No valid variation for '" << name << "'";
        return false;
    }

    Logger::info() << "Spawning structure '" << name << "' at ("
                  << centerPos.x << ", " << centerPos.y << ", " << centerPos.z << ")";

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
    const int structureHeight = static_cast<int>(var->structure.size());
    for (int y = 0; y < structureHeight; y++) {
        const auto& layer = var->structure[y];
        const int layerDepth = static_cast<int>(layer.size());

        for (int z = 0; z < layerDepth; z++) {
            const auto& row = layer[z];
            const int rowWidth = static_cast<int>(row.size());

            for (int x = 0; x < rowWidth; x++) {
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
        Logger::debug() << "Updating " << affectedChunks.size() << " affected chunk(s)...";
        for (Chunk* chunk : affectedChunks) {
            chunk->generateMesh(world);

            // Upload to GPU (async to prevent frame stalls)
            renderer->beginAsyncChunkUpload();
            chunk->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(chunk);
        }
    }

    Logger::info() << "Structure '" << name << "' spawned successfully!";
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
