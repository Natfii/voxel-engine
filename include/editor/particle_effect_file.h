/**
 * @file particle_effect_file.h
 * @brief Particle effect file persistence (YAML format)
 */

#pragma once

#include "particle/particle_effect.h"
#include <string>
#include <vector>

/**
 * @brief Particle effect file save/load operations
 */
class ParticleEffectFile {
public:
    /**
     * @brief Save a particle effect to YAML file
     * @param path File path to save to
     * @param effect Effect to save
     * @return True on success
     */
    static bool save(const std::string& path, const ParticleEffect& effect);

    /**
     * @brief Load a particle effect from YAML file
     * @param path File path to load from
     * @param effect Output effect
     * @return True on success
     */
    static bool load(const std::string& path, ParticleEffect& effect);

    /**
     * @brief Validate a particle effect file
     * @param path File path to validate
     * @param errors Output list of validation errors
     * @return True if valid
     */
    static bool validate(const std::string& path, std::vector<std::string>& errors);
};
