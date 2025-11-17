#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <glm/glm.hpp>
#include "biome_falloff.h"
#include "tree_generator.h"

// Forward declarations
class VulkanRenderer;

/**
 * Represents the location where a biome can spawn
 */
enum class BiomeSpawnLocation {
    Underground = 1,      // Only underground
    AboveGround = 2,      // Only above ground
    Both = 3              // Can spawn in both (Underground | AboveGround)
};

/**
 * Represents ore spawn multipliers for a biome
 */
struct OreSpawnRate {
    std::string ore_name;   // Name or ID of the ore block
    float multiplier;       // Spawn rate multiplier (1.0 = normal, 2.0 = double, 0.5 = half)
};

/**
 * Represents a single biome with all its properties
 */
struct Biome {
    // === REQUIRED PROPERTIES ===
    std::string name;           // Biome name (lowercase, spaces as '_')
    int temperature;            // 0 (coldest) to 100 (warmest) - center point for backward compatibility
    int moisture;               // 0 (driest) to 100 (wettest)
    int age;                    // 0 (rough terrain) to 100 (flat/plains)
    int activity;               // 0-100: spawn rate for structures/dens/towns

    // Temperature range for biome selection (preferred over single temperature value)
    int temperature_min;        // Minimum temperature where biome can spawn (-1 = use temperature-10)
    int temperature_max;        // Maximum temperature where biome can spawn (-1 = use temperature+10)

    // === OPTIONAL PROPERTIES ===

    // Spawning and Generation
    BiomeSpawnLocation spawn_location = BiomeSpawnLocation::AboveGround;
    int lowest_y = 0;                    // Lowest Y level where biome can spawn
    bool underwater_biome = false;       // Can spawn as ocean floor biome
    bool river_compatible = true;        // Can rivers cut through this biome
    int biome_rarity_weight = 50;        // 1-100: how common the biome is (higher = more common)
    std::string parent_biome;            // Parent biome name (for variants based on age/activity)
    float height_multiplier = 1.0f;      // Terrain height multiplier (1.0 = normal, 2.0 = double height)

    // === PER-BIOME HEIGHT VARIATION PARAMETERS ===
    int base_height_offset = 0;          // Vertical offset for entire biome (-50 to +50 blocks)
    float height_variation_min = 5.0f;   // Minimum terrain variation in blocks
    float height_variation_max = 30.0f;  // Maximum terrain variation in blocks
    float height_noise_frequency = 0.015f; // Noise frequency for height (higher = rougher terrain)
    int valley_depth = 0;                // Extra depth for valleys (negative values create deep valleys)
    int peak_height = 0;                 // Extra height for peaks (positive values create tall mountains)

    // === TERRAIN ROUGHNESS CONTROL (Noise Detail Parameters) ===
    int terrain_octaves = 5;             // Number of noise octaves (more = more detail, 3-8 recommended)
    float terrain_lacunarity = 2.0f;     // Lacunarity for noise (spacing between octaves, 2.0 = standard)
    float terrain_gain = 0.5f;           // Gain/persistence for noise (amplitude decay per octave, 0.5 = standard)
    int terrain_roughness = -1;          // 0-100: Overall roughness override (0=smooth, 100=very rough, -1=use age)

    // Vegetation
    bool trees_spawn = true;             // Can trees spawn in this biome
    int tree_density = 50;               // 0-100: tree spawn density
    int vegetation_density = 50;         // 0-100: grass/flowers/mushrooms spawn rate

    // Block Lists
    std::vector<int> required_blocks;    // Block IDs that must spawn
    std::vector<int> blacklisted_blocks; // Block IDs that cannot spawn naturally

    // Structure Lists
    std::vector<std::string> required_structures;    // Structures that must spawn at least once
    std::vector<std::string> blacklisted_structures; // Structures that cannot spawn

    // Creature Control
    std::vector<std::string> blacklisted_creatures;  // Creatures that cannot spawn
    bool hostile_spawn = true;                       // Can hostile creatures spawn naturally

    // Primary Blocks (defaults)
    int primary_surface_block = 3;       // Default: grass (block ID 3)
    int primary_stone_block = 1;         // Default: stone (block ID 1)
    int primary_log_block = -1;          // Default: oak log (-1 = use default)
    int primary_leave_block = -1;        // Default: normal leaves (-1 = use default)

    // Weather and Atmosphere
    std::string primary_weather;         // Primary weather type
    std::vector<std::string> blacklisted_weather;  // Weather that cannot occur
    glm::vec3 fog_color = glm::vec3(0.5f, 0.7f, 0.9f);  // Fog color override
    bool has_custom_fog = false;         // Whether to use custom fog color

    // Ore Distribution
    std::vector<OreSpawnRate> ore_spawn_rates;  // Ore spawn multipliers

    // Tree Generation (per-biome tree templates for unique tree styles)
    std::vector<TreeTemplate> tree_templates;  // 10 unique tree templates for this biome

    // === BIOME INFLUENCE FALLOFF CUSTOMIZATION (Agent 23) ===
    // Per-biome falloff configuration for fine-grained blending control
    // Allows individual biomes to have custom transition behavior
    BiomeFalloff::BiomeFalloffConfig falloffConfig;

    // Constructor
    Biome() : temperature_min(-1), temperature_max(-1) {}

    // Helper function to get effective temperature range
    int getEffectiveMinTemp() const {
        return (temperature_min >= 0) ? temperature_min : std::max(0, temperature - 10);
    }
    int getEffectiveMaxTemp() const {
        return (temperature_max >= 0) ? temperature_max : std::min(100, temperature + 10);
    }
};

/**
 * Singleton class that manages all biomes
 * Similar to BlockRegistry, loads YAML files from assets/biomes/
 */
class BiomeRegistry {
public:
    // Get the singleton instance
    static BiomeRegistry& getInstance();

    // Delete copy constructor and assignment operator
    BiomeRegistry(const BiomeRegistry&) = delete;
    BiomeRegistry& operator=(const BiomeRegistry&) = delete;

    /**
     * Load all biome definition files from the specified directory
     * @param directory Path to the biomes directory (e.g., "assets/biomes/")
     * @return true if loading was successful, false otherwise
     */
    bool loadBiomes(const std::string& directory);

    /**
     * Generate tree templates for all loaded biomes
     * Must be called after loadBiomes() but before world generation
     * @param treeGenerator Tree generator to use for template generation
     */
    void generateTreeTemplates(class TreeGenerator* treeGenerator);

    /**
     * Get a biome by name (case-insensitive)
     * @param name The biome name
     * @return Pointer to the biome, or nullptr if not found
     */
    const Biome* getBiome(const std::string& name) const;

    /**
     * Get a biome by index
     * @param index The biome index
     * @return Pointer to the biome, or nullptr if index is out of range
     */
    const Biome* getBiomeByIndex(int index) const;

    /**
     * Get the number of registered biomes
     */
    int getBiomeCount() const { return static_cast<int>(m_biomes.size()); }

    /**
     * Get all biomes
     */
    const std::vector<std::unique_ptr<Biome>>& getAllBiomes() const { return m_biomes; }

    /**
     * Clear all loaded biomes
     */
    void clear();

    /**
     * Get biomes suitable for a given temperature and moisture range
     * Used during world generation to find neighboring biomes
     */
    std::vector<const Biome*> getBiomesInRange(int temp_min, int temp_max,
                                                 int moisture_min, int moisture_max) const;

private:
    BiomeRegistry() = default;

    // Load a single biome from a YAML file
    bool loadBiomeFromFile(const std::string& filepath);

    // Parse biome spawn location from string
    BiomeSpawnLocation parseSpawnLocation(const std::string& location_str);

    // Parse comma-separated list of integers
    std::vector<int> parseIntList(const std::string& str);

    // Parse comma-separated list of strings
    std::vector<std::string> parseStringList(const std::string& str);

    // Parse ore spawn rates (format: "coal:1.5,iron:2.0,gold:0.5")
    std::vector<OreSpawnRate> parseOreSpawnRates(const std::string& str);

    // Parse RGB color (format: "R,G,B" where each is 0-255)
    glm::vec3 parseColor(const std::string& str);

    // Normalize biome name (lowercase, trim whitespace)
    std::string normalizeName(const std::string& name) const;

private:
    std::vector<std::unique_ptr<Biome>> m_biomes;
    std::unordered_map<std::string, int> m_biomeNameToIndex;  // name -> index lookup
    mutable std::mutex m_registryMutex;  // Protects access to biome data during multi-threaded chunk generation
};
