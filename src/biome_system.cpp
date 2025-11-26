#include "biome_system.h"
#include "tree_generator.h"
#include "logger.h"
#include "script_action.h"
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace fs = std::filesystem;

// ==================== BiomeRegistry Singleton ====================

BiomeRegistry& BiomeRegistry::getInstance() {
    static BiomeRegistry instance;
    return instance;
}

// ==================== Loading Functions ====================

bool BiomeRegistry::loadBiomes(const std::string& directory) {
    Logger::info() << "Loading biomes from: " << directory;

    if (!fs::exists(directory)) {
        Logger::error() << "Biome directory does not exist: " << directory;
        return false;
    }

    int loadedCount = 0;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            const auto& path = entry.path();
            if (path.extension() == ".yaml" || path.extension() == ".yml") {
                if (loadBiomeFromFile(path.string())) {
                    loadedCount++;
                } else {
                    Logger::error() << "Failed to load biome from: " << path.string();
                }
            }
        }
    }

    Logger::info() << "Successfully loaded " << loadedCount << " biome(s)";
    return loadedCount > 0;
}

void BiomeRegistry::generateTreeTemplates(TreeGenerator* treeGenerator) {
    if (!treeGenerator) {
        Logger::error() << "Cannot generate tree templates: TreeGenerator is null";
        return;
    }

    Logger::info() << "Generating tree templates for " << m_biomes.size() << " biome(s)...";

    for (auto& biome : m_biomes) {
        if (biome->trees_spawn) {
            treeGenerator->generateTreeTemplatesForBiome(biome.get());
            Logger::debug() << "  Generated 10 tree templates for biome: " << biome->name;
        }
    }

    Logger::info() << "Tree template generation complete";
}

bool BiomeRegistry::loadBiomeFromFile(const std::string& filepath) {
    try {
        YAML::Node doc = YAML::LoadFile(filepath);

        auto biome = std::make_unique<Biome>();

        // ===== REQUIRED FIELDS =====
        if (!doc["name"]) {
            Logger::error() << "Missing required field 'name' in: " << filepath;
            return false;
        }
        biome->name = normalizeName(doc["name"].as<std::string>());

        if (!doc["temperature"]) {
            Logger::error() << "Missing required field 'temperature' in: " << filepath;
            return false;
        }
        biome->temperature = doc["temperature"].as<int>();

        if (!doc["moisture"]) {
            Logger::error() << "Missing required field 'moisture' in: " << filepath;
            return false;
        }
        biome->moisture = doc["moisture"].as<int>();

        if (!doc["age"]) {
            Logger::error() << "Missing required field 'age' in: " << filepath;
            return false;
        }
        biome->age = doc["age"].as<int>();

        if (!doc["activity"]) {
            Logger::error() << "Missing required field 'activity' in: " << filepath;
            return false;
        }
        biome->activity = doc["activity"].as<int>();

        // Validate ranges
        if (biome->temperature < 0 || biome->temperature > 100) {
            Logger::error() << "Temperature must be 0-100 in: " << filepath;
            return false;
        }
        if (biome->moisture < 0 || biome->moisture > 100) {
            Logger::error() << "Moisture must be 0-100 in: " << filepath;
            return false;
        }
        if (biome->age < 0 || biome->age > 100) {
            Logger::error() << "Age must be 0-100 in: " << filepath;
            return false;
        }
        if (biome->activity < 0 || biome->activity > 100) {
            Logger::error() << "Activity must be 0-100 in: " << filepath;
            return false;
        }

        // ===== OPTIONAL FIELDS =====

        // Spawning and Generation
        if (doc["spawn_location"]) {
            biome->spawn_location = parseSpawnLocation(doc["spawn_location"].as<std::string>());
        }
        if (doc["lowest_y"]) {
            biome->lowest_y = doc["lowest_y"].as<int>();
        }
        if (doc["underwater_biome"]) {
            biome->underwater_biome = doc["underwater_biome"].as<bool>();
        }
        if (doc["river_compatible"]) {
            biome->river_compatible = doc["river_compatible"].as<bool>();
        }
        if (doc["biome_rarity_weight"]) {
            biome->biome_rarity_weight = doc["biome_rarity_weight"].as<int>();
            if (biome->biome_rarity_weight < 1) biome->biome_rarity_weight = 1;
            if (biome->biome_rarity_weight > 100) biome->biome_rarity_weight = 100;
        }
        if (doc["parent_biome"]) {
            biome->parent_biome = normalizeName(doc["parent_biome"].as<std::string>());
        }
        if (doc["height_multiplier"]) {
            biome->height_multiplier = doc["height_multiplier"].as<float>();
        }

        // Vegetation
        if (doc["trees_spawn"]) {
            biome->trees_spawn = doc["trees_spawn"].as<bool>();
        }
        if (doc["tree_density"]) {
            biome->tree_density = doc["tree_density"].as<int>();
            if (biome->tree_density < 0) biome->tree_density = 0;
            if (biome->tree_density > 100) biome->tree_density = 100;
        }
        if (doc["vegetation_density"]) {
            biome->vegetation_density = doc["vegetation_density"].as<int>();
            if (biome->vegetation_density < 0) biome->vegetation_density = 0;
            if (biome->vegetation_density > 100) biome->vegetation_density = 100;
        }

        // Block Lists
        if (doc["required_blocks"]) {
            biome->required_blocks = parseIntList(doc["required_blocks"].as<std::string>());
        }
        if (doc["blacklisted_blocks"]) {
            biome->blacklisted_blocks = parseIntList(doc["blacklisted_blocks"].as<std::string>());
        }

        // Structure Lists
        if (doc["required_structures"]) {
            biome->required_structures = parseStringList(doc["required_structures"].as<std::string>());
        }
        if (doc["blacklisted_structures"]) {
            biome->blacklisted_structures = parseStringList(doc["blacklisted_structures"].as<std::string>());
        }

        // Creature Control
        if (doc["blacklisted_creatures"]) {
            biome->blacklisted_creatures = parseStringList(doc["blacklisted_creatures"].as<std::string>());
        }
        if (doc["hostile_spawn"]) {
            biome->hostile_spawn = doc["hostile_spawn"].as<bool>();
        }

        // Primary Blocks
        if (doc["primary_surface_block"]) {
            biome->primary_surface_block = doc["primary_surface_block"].as<int>();
        }
        if (doc["primary_stone_block"]) {
            biome->primary_stone_block = doc["primary_stone_block"].as<int>();
        }
        if (doc["primary_log_block"]) {
            biome->primary_log_block = doc["primary_log_block"].as<int>();
        }
        if (doc["primary_leave_block"]) {
            biome->primary_leave_block = doc["primary_leave_block"].as<int>();
        }

        // Weather and Atmosphere
        if (doc["primary_weather"]) {
            biome->primary_weather = doc["primary_weather"].as<std::string>();
        }
        if (doc["blacklisted_weather"]) {
            biome->blacklisted_weather = parseStringList(doc["blacklisted_weather"].as<std::string>());
        }
        if (doc["fog_color"]) {
            biome->fog_color = parseColor(doc["fog_color"].as<std::string>());
            biome->has_custom_fog = true;
        }

        // Ore Distribution
        if (doc["ore_spawn_rates"]) {
            biome->ore_spawn_rates = parseOreSpawnRates(doc["ore_spawn_rates"].as<std::string>());
        }

        // Track temperature and moisture ranges for noise scaling
        m_minTemperature = std::min(m_minTemperature, biome->temperature);
        m_maxTemperature = std::max(m_maxTemperature, biome->temperature);
        m_minMoisture = std::min(m_minMoisture, biome->moisture);
        m_maxMoisture = std::max(m_maxMoisture, biome->moisture);

        // Parse and register event handlers (if any)
        if (doc["events"]) {
            std::vector<ScriptEventHandler> handlers;

            // Iterate through all event types in the "events" section
            for (auto it = doc["events"].begin(); it != doc["events"].end(); ++it) {
                std::string eventType = it->first.as<std::string>();

                try {
                    ScriptEventHandler handler = ScriptEventHandler::fromYAML(eventType, it->second);
                    handlers.push_back(handler);
                    Logger::debug() << "    Parsed event handler: " << eventType
                                   << " with " << handler.actions.size() << " actions";
                } catch (const std::exception& e) {
                    Logger::warning() << "Error parsing event handler '" << eventType
                                     << "' for biome " << biome->name << ": " << e.what();
                }
            }

            // Register the handlers
            if (!handlers.empty()) {
                registerBiomeEventHandlers(biome->name, handlers);
            }
        }

        // Add to registry
        int index = static_cast<int>(m_biomes.size());
        m_biomeNameToIndex[biome->name] = index;
        m_biomes.push_back(std::move(biome));

        Logger::debug() << "  Loaded biome: " << m_biomes.back()->name;
        return true;

    } catch (const YAML::Exception& e) {
        Logger::error() << "YAML parsing error in " << filepath << ": " << e.what();
        return false;
    } catch (const std::exception& e) {
        Logger::error() << "Error loading biome from " << filepath << ": " << e.what();
        return false;
    }
}

// ==================== Query Functions ====================

const Biome* BiomeRegistry::getBiome(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    std::string normalized = normalizeName(name);
    auto it = m_biomeNameToIndex.find(normalized);
    if (it != m_biomeNameToIndex.end()) {
        return m_biomes[it->second].get();
    }
    return nullptr;
}

const Biome* BiomeRegistry::getBiomeByIndex(int index) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    if (index >= 0 && index < static_cast<int>(m_biomes.size())) {
        return m_biomes[index].get();
    }
    return nullptr;
}

std::vector<const Biome*> BiomeRegistry::getBiomesInRange(int temp_min, int temp_max,
                                                            int moisture_min, int moisture_max) const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    std::vector<const Biome*> result;
    for (const auto& biome : m_biomes) {
        if (biome->temperature >= temp_min && biome->temperature <= temp_max &&
            biome->moisture >= moisture_min && biome->moisture <= moisture_max) {
            result.push_back(biome.get());
        }
    }
    return result;
}

void BiomeRegistry::clear() {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_biomes.clear();
    m_biomeNameToIndex.clear();
}

// ==================== Parsing Helper Functions ====================

BiomeSpawnLocation BiomeRegistry::parseSpawnLocation(const std::string& location_str) {
    std::string lower = location_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "underground") {
        return BiomeSpawnLocation::Underground;
    } else if (lower == "aboveground" || lower == "above_ground") {
        return BiomeSpawnLocation::AboveGround;
    } else if (lower == "both") {
        return BiomeSpawnLocation::Both;
    }

    Logger::warning() << "Unknown spawn location '" << location_str << "', defaulting to AboveGround";
    return BiomeSpawnLocation::AboveGround;
}

std::vector<int> BiomeRegistry::parseIntList(const std::string& str) {
    std::vector<int> result;
    if (str.empty()) return result;

    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);

        if (!token.empty()) {
            try {
                result.push_back(std::stoi(token));
            } catch (const std::exception&) {
                Logger::warning() << "Invalid integer '" << token << "' in list";
            }
        }
    }
    return result;
}

std::vector<std::string> BiomeRegistry::parseStringList(const std::string& str) {
    std::vector<std::string> result;
    if (str.empty()) return result;

    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);

        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

std::vector<OreSpawnRate> BiomeRegistry::parseOreSpawnRates(const std::string& str) {
    std::vector<OreSpawnRate> result;
    if (str.empty()) return result;

    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);

        if (!token.empty()) {
            // Parse "ore_name:multiplier" format
            size_t colonPos = token.find(':');
            if (colonPos != std::string::npos) {
                std::string ore_name = token.substr(0, colonPos);
                std::string multiplier_str = token.substr(colonPos + 1);

                // Trim both parts
                ore_name.erase(0, ore_name.find_first_not_of(" \t\n\r"));
                ore_name.erase(ore_name.find_last_not_of(" \t\n\r") + 1);
                multiplier_str.erase(0, multiplier_str.find_first_not_of(" \t\n\r"));
                multiplier_str.erase(multiplier_str.find_last_not_of(" \t\n\r") + 1);

                try {
                    float multiplier = std::stof(multiplier_str);
                    result.push_back({ore_name, multiplier});
                } catch (const std::exception&) {
                    Logger::warning() << "Invalid ore spawn rate '" << token << "'";
                }
            } else {
                Logger::warning() << "Invalid ore spawn rate format '" << token << "' (expected 'name:multiplier')";
            }
        }
    }
    return result;
}

glm::vec3 BiomeRegistry::parseColor(const std::string& str) {
    std::vector<int> components;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);

        if (!token.empty()) {
            try {
                components.push_back(std::stoi(token));
            } catch (const std::exception&) {
                Logger::warning() << "Invalid color component '" << token << "'";
            }
        }
    }

    if (components.size() == 3) {
        // Convert from 0-255 to 0.0-1.0
        return glm::vec3(
            components[0] / 255.0f,
            components[1] / 255.0f,
            components[2] / 255.0f
        );
    }

    Logger::warning() << "Invalid color format '" << str << "', using default";
    return glm::vec3(0.5f, 0.7f, 0.9f);  // Default sky-ish color
}

std::string BiomeRegistry::normalizeName(const std::string& name) const {
    std::string result = name;

    // Convert to lowercase
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);

    // Trim whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);

    // Replace spaces with underscores
    std::replace(result.begin(), result.end(), ' ', '_');

    return result;
}
