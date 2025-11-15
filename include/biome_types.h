#pragma once

/**
 * Biome Types and Constants
 *
 * This file provides constants and type definitions for the biome system.
 * Biomes are defined in YAML files in assets/biomes/ and loaded at runtime.
 *
 * The biome system uses a temperature-moisture matrix to select appropriate biomes:
 * - Temperature: 0 (coldest) to 100 (warmest)
 * - Moisture: 0 (driest) to 100 (wettest)
 *
 * Temperature Zones:
 *   0-20:   Arctic/Alpine (ice, tundra, high mountains)
 *   20-40:  Cold (taiga, winter forest, cold mountains)
 *   40-60:  Temperate (plains, forests, hills)
 *   60-80:  Warm (savanna, warm forests)
 *   80-100: Hot (desert, tropical rainforest, jungle)
 *
 * Moisture Zones:
 *   0-20:   Arid (desert, barren)
 *   20-40:  Dry (savanna, dry grassland)
 *   40-60:  Moderate (plains, forests)
 *   60-80:  Humid (rainforest, swamp)
 *   80-100: Saturated (ocean, swamp, rainforest)
 */

namespace BiomeTypes {
    /**
     * Standard biome name constants
     * These match the names defined in YAML files (normalized to lowercase)
     */
    namespace Names {
        // Surface Biomes - Cold
        constexpr const char* ICE_TUNDRA = "ice_tundra";
        constexpr const char* WINTER_FOREST = "winter_forest";
        constexpr const char* TAIGA = "taiga";

        // Surface Biomes - Temperate
        constexpr const char* PLAINS = "plains";
        constexpr const char* FOREST = "forest";
        constexpr const char* MOUNTAIN = "mountain";
        constexpr const char* SWAMP = "swamp";

        // Surface Biomes - Warm/Hot
        constexpr const char* SAVANNA = "savanna";
        constexpr const char* DESERT = "desert";
        constexpr const char* TROPICAL_RAINFOREST = "tropical_rainforest";

        // Water Biomes
        constexpr const char* OCEAN = "ocean";

        // Underground Biomes
        constexpr const char* MUSHROOM_CAVE = "mushroom_cave";
        constexpr const char* CRYSTAL_CAVE = "crystal_cave";
        constexpr const char* DEEP_DARK = "deep_dark";
    }

    /**
     * Temperature ranges (0-100 scale)
     */
    namespace Temperature {
        constexpr int ARCTIC_MIN = 0;
        constexpr int ARCTIC_MAX = 20;

        constexpr int COLD_MIN = 20;
        constexpr int COLD_MAX = 40;

        constexpr int TEMPERATE_MIN = 40;
        constexpr int TEMPERATE_MAX = 60;

        constexpr int WARM_MIN = 60;
        constexpr int WARM_MAX = 80;

        constexpr int HOT_MIN = 80;
        constexpr int HOT_MAX = 100;
    }

    /**
     * Moisture ranges (0-100 scale)
     */
    namespace Moisture {
        constexpr int ARID_MIN = 0;
        constexpr int ARID_MAX = 20;

        constexpr int DRY_MIN = 20;
        constexpr int DRY_MAX = 40;

        constexpr int MODERATE_MIN = 40;
        constexpr int MODERATE_MAX = 60;

        constexpr int HUMID_MIN = 60;
        constexpr int HUMID_MAX = 80;

        constexpr int SATURATED_MIN = 80;
        constexpr int SATURATED_MAX = 100;
    }

    /**
     * Age ranges (terrain roughness)
     * Age 0 = young, rough, mountainous terrain
     * Age 100 = old, flat, plains-like terrain
     */
    namespace Age {
        constexpr int YOUNG_MIN = 0;       // Very rough, high mountains
        constexpr int YOUNG_MAX = 30;

        constexpr int MATURE_MIN = 30;     // Moderate hills and valleys
        constexpr int MATURE_MAX = 70;

        constexpr int OLD_MIN = 70;        // Very flat, plains
        constexpr int OLD_MAX = 100;
    }

    /**
     * Activity ranges (structure/settlement spawn rate)
     * Activity 0 = no structures
     * Activity 100 = maximum structure density
     */
    namespace Activity {
        constexpr int NONE = 0;
        constexpr int LOW_MIN = 1;
        constexpr int LOW_MAX = 30;

        constexpr int MODERATE_MIN = 30;
        constexpr int MODERATE_MAX = 70;

        constexpr int HIGH_MIN = 70;
        constexpr int HIGH_MAX = 100;
    }

    /**
     * Rarity weights (for biome selection)
     * Higher weight = more common
     */
    namespace Rarity {
        constexpr int VERY_RARE = 10;      // Deep Dark, Crystal Cave
        constexpr int RARE = 20;           // Mushroom Cave, special variants
        constexpr int UNCOMMON = 35;       // Mountain, Swamp, Ice Tundra
        constexpr int COMMON = 50;         // Forest, Taiga, Savanna
        constexpr int VERY_COMMON = 70;    // Plains, Ocean
    }
}

/**
 * Biome Property Defaults
 * Used when creating new biomes
 */
namespace BiomeDefaults {
    constexpr int TREE_DENSITY = 50;           // Default tree spawn density
    constexpr int VEGETATION_DENSITY = 50;     // Default vegetation density
    constexpr float HEIGHT_MULTIPLIER = 1.0f;  // Default terrain height multiplier
    constexpr int LOWEST_Y = 0;                // Default minimum Y level
    constexpr int BIOME_RARITY_WEIGHT = 50;    // Default rarity weight
}
