/**
 * @file rig_file.cpp
 * @brief Implementation of rig file persistence
 *
 * File format:
 * ```yaml
 * version: 1
 * model: assets/models/player.glb
 * has_tail: false
 * bones:
 *   - name: spine_root
 *     parent: ~
 *     position: [0.0, 0.5, 0.0]
 *   - name: spine_tip
 *     parent: spine_root
 *     position: [0.0, 1.2, 0.0]
 *   # etc...
 * ```
 */

#include "editor/rig_file.h"
#include "editor/skeleton_editor_state.h"
#include "logger.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * @brief Save a rig to YAML file
 */
bool RigFile::save(const std::string& path, const SkeletonEditorState& state) {
    try {
        // Validate state before saving
        std::vector<std::string> errors;
        if (!state.validate(errors)) {
            Logger::error() << "Cannot save rig - validation failed:";
            for (const auto& error : errors) {
                Logger::error() << "  " << error;
            }
            return false;
        }

        // Create directory if it doesn't exist
        fs::path filePath(path);
        if (filePath.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(filePath.parent_path(), ec);
            if (ec) {
                Logger::error() << "Failed to create directory: " << filePath.parent_path() << " - " << ec.message();
                return false;
            }
        }

        // Build YAML document
        YAML::Emitter out;
        out << YAML::BeginMap;

        // Version
        out << YAML::Key << "version" << YAML::Value << 1;

        // Model path
        out << YAML::Key << "model" << YAML::Value << state.getModelPath();

        // Tail flag
        out << YAML::Key << "has_tail" << YAML::Value << state.hasTail();

        // Bones
        out << YAML::Key << "bones" << YAML::Value << YAML::BeginSeq;

        for (const auto& bone : state.getBones()) {
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << bone.name;

            // Parent: use null (~) if empty
            out << YAML::Key << "parent";
            if (bone.parent.empty()) {
                out << YAML::Value << YAML::Null;
            } else {
                out << YAML::Value << bone.parent;
            }

            // Position as array
            out << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << bone.position.x << bone.position.y << bone.position.z
                << YAML::EndSeq;

            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        // Write to file
        std::ofstream fout(path);
        if (!fout.is_open()) {
            Logger::error() << "Failed to open file for writing: " << path;
            return false;
        }

        fout << out.c_str();

        // Check if write was successful before closing
        if (!fout.good()) {
            Logger::error() << "Failed to write to file: " << path;
            fout.close();
            return false;
        }

        fout.close();

        // Final check after close
        if (fout.fail()) {
            Logger::error() << "Error occurred while closing file: " << path;
            return false;
        }

        Logger::info() << "Saved rig to: " << path;
        return true;

    } catch (const YAML::Exception& e) {
        Logger::error() << "YAML error while saving rig: " << e.what();
        return false;
    } catch (const std::exception& e) {
        Logger::error() << "Error while saving rig: " << e.what();
        return false;
    }
}

/**
 * @brief Load a rig from YAML file
 */
bool RigFile::load(const std::string& path, SkeletonEditorState& state) {
    try {
        // Check file exists
        if (!fs::exists(path)) {
            Logger::error() << "Rig file not found: " << path;
            return false;
        }

        // Load YAML
        YAML::Node root = YAML::LoadFile(path);

        // Validate version
        if (!root["version"]) {
            Logger::error() << "Rig file missing 'version' field: " << path;
            return false;
        }

        int version = root["version"].as<int>();
        if (version != 1) {
            Logger::error() << "Unsupported rig file version: " << version;
            return false;
        }

        // Clear existing state
        state.clear();

        // Load model path
        if (root["model"]) {
            state.setModelPath(root["model"].as<std::string>());
        }

        // Load tail flag
        if (root["has_tail"]) {
            state.setHasTail(root["has_tail"].as<bool>());
        }

        // Load bones
        if (!root["bones"]) {
            Logger::error() << "Rig file missing 'bones' field: " << path;
            return false;
        }

        const YAML::Node& bonesNode = root["bones"];
        if (!bonesNode.IsSequence()) {
            Logger::error() << "'bones' field must be a sequence: " << path;
            return false;
        }

        for (size_t i = 0; i < bonesNode.size(); ++i) {
            const YAML::Node& boneNode = bonesNode[i];

            // Validate required fields
            if (!boneNode["name"]) {
                Logger::error() << "Bone " << i << " missing 'name' field";
                return false;
            }
            if (!boneNode["position"]) {
                Logger::error() << "Bone " << i << " missing 'position' field";
                return false;
            }

            // Read bone data
            Bone bone;
            bone.name = boneNode["name"].as<std::string>();

            // Parent (may be null)
            if (boneNode["parent"] && !boneNode["parent"].IsNull()) {
                bone.parent = boneNode["parent"].as<std::string>();
            }

            // Position
            const YAML::Node& posNode = boneNode["position"];
            if (!posNode.IsSequence() || posNode.size() != 3) {
                Logger::error() << "Bone '" << bone.name << "' position must be [x, y, z]";
                return false;
            }

            bone.position.x = posNode[0].as<float>();
            bone.position.y = posNode[1].as<float>();
            bone.position.z = posNode[2].as<float>();

            // Mark tail bones as optional
            bone.optional = (bone.name == "tail_base" || bone.name == "tail_tip");

            // Add bone using placeBone to maintain proper state
            state.placeBone(bone.name, bone.position);
        }

        // Validate loaded rig
        std::vector<std::string> errors;
        if (!state.validate(errors)) {
            Logger::error() << "Loaded rig failed validation:";
            for (const auto& error : errors) {
                Logger::error() << "  " << error;
            }
            return false;
        }

        Logger::info() << "Loaded rig from: " << path;
        return true;

    } catch (const YAML::Exception& e) {
        Logger::error() << "YAML error while loading rig: " << e.what();
        return false;
    } catch (const std::exception& e) {
        Logger::error() << "Error while loading rig: " << e.what();
        return false;
    }
}

/**
 * @brief Validate rig file structure without loading into state
 */
bool RigFile::validate(const std::string& path, std::vector<std::string>& errors) {
    errors.clear();

    try {
        // Check file exists
        if (!fs::exists(path)) {
            errors.push_back("File not found: " + path);
            return false;
        }

        // Load YAML
        YAML::Node root = YAML::LoadFile(path);

        // Check version
        if (!root["version"]) {
            errors.push_back("Missing 'version' field");
        } else {
            int version = root["version"].as<int>();
            if (version != 1) {
                errors.push_back("Unsupported version: " + std::to_string(version));
            }
        }

        // Check model path
        if (!root["model"]) {
            errors.push_back("Missing 'model' field");
        }

        // Check has_tail
        if (!root["has_tail"]) {
            errors.push_back("Missing 'has_tail' field");
        }

        // Check bones
        if (!root["bones"]) {
            errors.push_back("Missing 'bones' field");
        } else {
            const YAML::Node& bonesNode = root["bones"];
            if (!bonesNode.IsSequence()) {
                errors.push_back("'bones' must be a sequence");
            } else {
                // Validate each bone
                for (size_t i = 0; i < bonesNode.size(); ++i) {
                    const YAML::Node& bone = bonesNode[i];
                    std::string prefix = "Bone " + std::to_string(i) + ": ";

                    if (!bone["name"]) {
                        errors.push_back(prefix + "missing 'name'");
                    }

                    if (!bone["position"]) {
                        errors.push_back(prefix + "missing 'position'");
                    } else {
                        const YAML::Node& pos = bone["position"];
                        if (!pos.IsSequence() || pos.size() != 3) {
                            errors.push_back(prefix + "position must be [x, y, z]");
                        }
                    }
                }

                // Check for required bones
                std::vector<std::string> requiredBones = {
                    "spine_root", "spine_tip", "leg_L", "leg_R", "arm_L", "arm_R", "head"
                };

                for (const auto& required : requiredBones) {
                    bool found = false;
                    for (const auto& bone : bonesNode) {
                        if (bone["name"] && bone["name"].as<std::string>() == required) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        errors.push_back("Missing required bone: " + required);
                    }
                }
            }
        }

        return errors.empty();

    } catch (const YAML::Exception& e) {
        errors.push_back("YAML parse error: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        errors.push_back("Error: " + std::string(e.what()));
        return false;
    }
}
