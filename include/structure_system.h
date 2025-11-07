/**
 * @file structure_system.h
 * @brief Structure loading and spawning system with YAML-based definitions
 *
 * This system handles:
 * - Loading structure definitions from YAML files (assets/structures/)
 * - Multiple variations with weighted random selection
 * - Structure spawning at world positions
 * - Integration with block system for runtime block lookup
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <glm/glm.hpp>

// Forward declarations
class World;
class VulkanRenderer;

/**
 * @brief A single variation of a structure with weighted chance
 */
struct StructureVariation {
    int length = 0;           ///< X dimension (must be odd)
    int width = 0;            ///< Z dimension (must be odd)
    int height = 0;           ///< Y dimension (number of layers)
    int depth = 0;            ///< How many blocks spawn below ground
    int chance = 0;           ///< Percentage chance (0-100)

    /// 3D array of block IDs [y][z][x]
    /// First dimension is height (layers from bottom to top)
    /// Each layer is a 2D array of block IDs
    std::vector<std::vector<std::vector<int>>> structure;
};

/**
 * @brief Definition of a structure loaded from YAML
 *
 * A structure can have multiple variations that are randomly selected
 * based on weighted chances. Each variation defines a 3D grid of blocks.
 */
struct StructureDefinition {
    std::string name;                           ///< Structure name (e.g., "Oak Tree")
    std::vector<StructureVariation> variations; ///< All variations with chances
};

/**
 * @brief Singleton registry for all structures
 *
 * The StructureRegistry loads structure definitions from YAML files
 * and provides methods to spawn them in the world.
 *
 * Features:
 * - YAML-based structure loading
 * - Multiple variations with weighted random selection
 * - Odd dimension validation (for center-based spawning)
 * - Integration with block system for runtime block lookup
 *
 * Usage:
 * @code
 * auto& registry = StructureRegistry::instance();
 * registry.loadStructures("assets/structures");
 * registry.spawnStructure("Oak Tree", world, glm::ivec3(100, 64, 100));
 * @endcode
 */
class StructureRegistry {
public:
    /**
     * @brief Gets the singleton instance
     * @return Reference to the global structure registry
     */
    static StructureRegistry& instance();

    /**
     * @brief Loads all structure definitions from YAML files
     *
     * Scans the directory for .yaml/.yml files and parses structure definitions.
     * Validates that dimensions are odd numbers and chances sum to 100%.
     *
     * Expected file structure:
     * - assets/structures/oaktree.yaml
     * - assets/structures/rock.yaml
     *
     * @param directory Directory containing structure YAML files (default: "assets/structures")
     * @return True if loading succeeded, false on error
     */
    bool loadStructures(const std::string& directory = "assets/structures");

    /**
     * @brief Gets a structure definition by name
     *
     * @param name Structure name (e.g., "Oak Tree")
     * @return Pointer to structure definition, or nullptr if not found
     */
    const StructureDefinition* get(const std::string& name) const;

    /**
     * @brief Spawns a structure at the specified world position
     *
     * Randomly selects a variation based on weighted chances and places
     * blocks in the world. The position represents the center (middle block)
     * of the structure at ground level. Updates chunk meshes on GPU.
     *
     * @param name Structure name
     * @param world World instance to spawn into
     * @param centerPos Center position (middle block at ground level)
     * @param renderer Vulkan renderer for GPU mesh updates (optional, can be null)
     * @return True if spawned successfully, false if structure not found
     */
    bool spawnStructure(const std::string& name, World* world, const glm::ivec3& centerPos, VulkanRenderer* renderer = nullptr);

    /**
     * @brief Gets all loaded structure names
     * @return Vector of structure names
     */
    std::vector<std::string> getAllStructureNames() const;

    /**
     * @brief Gets the total number of loaded structures
     * @return Structure count
     */
    int count() const { return (int)m_structures.size(); }

private:
    StructureRegistry();
    StructureRegistry(const StructureRegistry&) = delete;
    StructureRegistry& operator=(const StructureRegistry&) = delete;

    /**
     * @brief Selects a random variation based on weighted chances
     *
     * @param def Structure definition with variations
     * @return Pointer to selected variation, or nullptr if no valid variation
     */
    const StructureVariation* selectVariation(const StructureDefinition& def);

    // Structure storage
    std::unordered_map<std::string, StructureDefinition> m_structures;

    // Random number generation
    std::mt19937 m_rng;
    std::uniform_int_distribution<int> m_dist;
};
