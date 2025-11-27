#pragma once

#include <string>
#include <vector>

// Forward declarations
class SkeletonEditorState;

/**
 * @brief Handles saving and loading rig files
 *
 * Rig files are YAML format stored in assets/rigs/
 */
class RigFile {
public:
    /**
     * @brief Save a rig to file
     * @param path File path (e.g., "assets/rigs/player.yaml")
     * @param state The skeleton editor state to save
     * @return True if save succeeded
     */
    static bool save(const std::string& path, const SkeletonEditorState& state);

    /**
     * @brief Load a rig from file
     * @param path File path
     * @param state Output skeleton editor state
     * @return True if load succeeded
     */
    static bool load(const std::string& path, SkeletonEditorState& state);

    /**
     * @brief Validate rig file structure
     * @param path File path
     * @param errors Output error messages
     * @return True if file is valid
     */
    static bool validate(const std::string& path, std::vector<std::string>& errors);

    /**
     * @brief Get default rig directory
     */
    static std::string getDefaultDirectory() { return "assets/rigs"; }
};
