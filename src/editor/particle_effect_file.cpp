/**
 * @file particle_effect_file.cpp
 * @brief Implementation of particle effect file persistence
 *
 * File format:
 * ```yaml
 * version: 1
 * name: "Fire Effect"
 * emitters:
 *   - name: "Fire"
 *     shape: cone
 *     render_shape: circle
 *     duration: 3.0
 *     loop: true
 *     rate: [30, 50]
 *     lifetime: [0.5, 1.5]
 *     speed: [2.0, 5.0]
 *     angle: [70, 110]
 *     gravity: [0.0, 2.0]
 *     drag: 0.0
 *     size_start: [1.5, 1.5]
 *     size_end: [0.2, 0.2]
 *     color_start: [1.0, 0.6, 0.1, 1.0]
 *     color_end: [1.0, 0.1, 0.0, 0.0]
 *     cone_angle: 25.0
 *     align_to_velocity: false
 *     blend_mode: alpha
 * ```
 */

#include "editor/particle_effect_file.h"
#include "logger.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// Helper to convert EmitterShape to string
static std::string shapeToString(EmitterShape shape) {
    switch (shape) {
        case EmitterShape::POINT: return "point";
        case EmitterShape::CONE: return "cone";
        case EmitterShape::BOX: return "box";
        case EmitterShape::CIRCLE: return "circle";
        default: return "point";
    }
}

static EmitterShape stringToShape(const std::string& str) {
    if (str == "cone") return EmitterShape::CONE;
    if (str == "box") return EmitterShape::BOX;
    if (str == "circle") return EmitterShape::CIRCLE;
    return EmitterShape::POINT;
}

static std::string renderShapeToString(ParticleRenderShape shape) {
    switch (shape) {
        case ParticleRenderShape::CIRCLE: return "circle";
        case ParticleRenderShape::SQUARE: return "square";
        case ParticleRenderShape::TRIANGLE: return "triangle";
        case ParticleRenderShape::STAR: return "star";
        case ParticleRenderShape::RING: return "ring";
        case ParticleRenderShape::SPARK: return "spark";
        default: return "circle";
    }
}

static ParticleRenderShape stringToRenderShape(const std::string& str) {
    if (str == "square") return ParticleRenderShape::SQUARE;
    if (str == "triangle") return ParticleRenderShape::TRIANGLE;
    if (str == "star") return ParticleRenderShape::STAR;
    if (str == "ring") return ParticleRenderShape::RING;
    if (str == "spark") return ParticleRenderShape::SPARK;
    return ParticleRenderShape::CIRCLE;
}

static std::string blendModeToString(ParticleBlendMode mode) {
    switch (mode) {
        case ParticleBlendMode::ALPHA: return "alpha";
        case ParticleBlendMode::ADDITIVE: return "additive";
        case ParticleBlendMode::PREMULTIPLIED: return "premultiplied";
        default: return "alpha";
    }
}

static ParticleBlendMode stringToBlendMode(const std::string& str) {
    if (str == "additive") return ParticleBlendMode::ADDITIVE;
    if (str == "premultiplied") return ParticleBlendMode::PREMULTIPLIED;
    return ParticleBlendMode::ALPHA;
}

bool ParticleEffectFile::save(const std::string& path, const ParticleEffect& effect) {
    try {
        // Create directory if it doesn't exist
        fs::path filePath(path);
        if (filePath.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(filePath.parent_path(), ec);
            if (ec) {
                Logger::error() << "Failed to create directory: " << filePath.parent_path();
                return false;
            }
        }

        // Build YAML document
        YAML::Emitter out;
        out << YAML::BeginMap;

        out << YAML::Key << "version" << YAML::Value << 1;
        out << YAML::Key << "name" << YAML::Value << effect.name;

        out << YAML::Key << "emitters" << YAML::Value << YAML::BeginSeq;

        for (const auto& emitter : effect.emitters) {
            out << YAML::BeginMap;

            out << YAML::Key << "name" << YAML::Value << emitter.name;
            out << YAML::Key << "shape" << YAML::Value << shapeToString(emitter.shape);
            out << YAML::Key << "render_shape" << YAML::Value << renderShapeToString(emitter.renderShape);

            out << YAML::Key << "duration" << YAML::Value << emitter.duration;
            out << YAML::Key << "loop" << YAML::Value << emitter.loop;

            out << YAML::Key << "rate" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.rate.min << emitter.rate.max << YAML::EndSeq;

            out << YAML::Key << "lifetime" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.lifetime.min << emitter.lifetime.max << YAML::EndSeq;

            out << YAML::Key << "speed" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.speed.min << emitter.speed.max << YAML::EndSeq;

            out << YAML::Key << "angle" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.angle.min << emitter.angle.max << YAML::EndSeq;

            out << YAML::Key << "gravity" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.gravity.x << emitter.gravity.y << YAML::EndSeq;

            out << YAML::Key << "drag" << YAML::Value << emitter.drag;

            out << YAML::Key << "size_start" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.sizeStart.x << emitter.sizeStart.y << YAML::EndSeq;

            out << YAML::Key << "size_end" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.sizeEnd.x << emitter.sizeEnd.y << YAML::EndSeq;

            out << YAML::Key << "color_start" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.colorStart.r << emitter.colorStart.g
                << emitter.colorStart.b << emitter.colorStart.a << YAML::EndSeq;

            out << YAML::Key << "color_end" << YAML::Value << YAML::Flow << YAML::BeginSeq
                << emitter.colorEnd.r << emitter.colorEnd.g
                << emitter.colorEnd.b << emitter.colorEnd.a << YAML::EndSeq;

            // Shape-specific settings
            if (emitter.shape == EmitterShape::CONE) {
                out << YAML::Key << "cone_angle" << YAML::Value << emitter.coneAngle;
            } else if (emitter.shape == EmitterShape::BOX) {
                out << YAML::Key << "box_size" << YAML::Value << YAML::Flow << YAML::BeginSeq
                    << emitter.boxSize.x << emitter.boxSize.y << emitter.boxSize.z << YAML::EndSeq;
            } else if (emitter.shape == EmitterShape::CIRCLE) {
                out << YAML::Key << "circle_radius" << YAML::Value << emitter.circleRadius;
            }

            out << YAML::Key << "align_to_velocity" << YAML::Value << emitter.alignToVelocity;
            out << YAML::Key << "blend_mode" << YAML::Value << blendModeToString(emitter.texture.blend);

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
        fout.close();

        Logger::info() << "Saved particle effect to: " << path;
        return true;

    } catch (const YAML::Exception& e) {
        Logger::error() << "YAML error while saving effect: " << e.what();
        return false;
    } catch (const std::exception& e) {
        Logger::error() << "Error while saving effect: " << e.what();
        return false;
    }
}

bool ParticleEffectFile::load(const std::string& path, ParticleEffect& effect) {
    try {
        if (!fs::exists(path)) {
            Logger::error() << "Particle effect file not found: " << path;
            return false;
        }

        YAML::Node root = YAML::LoadFile(path);

        // Check version
        if (!root["version"]) {
            Logger::error() << "Effect file missing 'version' field";
            return false;
        }
        int version = root["version"].as<int>();
        if (version != 1) {
            Logger::error() << "Unsupported effect version: " << version;
            return false;
        }

        // Clear effect
        effect = ParticleEffect();

        // Load name
        if (root["name"]) {
            effect.name = root["name"].as<std::string>();
        }

        // Load emitters
        if (!root["emitters"] || !root["emitters"].IsSequence()) {
            Logger::error() << "Effect file missing 'emitters' sequence";
            return false;
        }

        for (const auto& emitterNode : root["emitters"]) {
            EmitterConfig config;

            if (emitterNode["name"]) config.name = emitterNode["name"].as<std::string>();
            if (emitterNode["shape"]) config.shape = stringToShape(emitterNode["shape"].as<std::string>());
            if (emitterNode["render_shape"]) config.renderShape = stringToRenderShape(emitterNode["render_shape"].as<std::string>());
            if (emitterNode["duration"]) config.duration = emitterNode["duration"].as<float>();
            if (emitterNode["loop"]) config.loop = emitterNode["loop"].as<bool>();

            if (emitterNode["rate"] && emitterNode["rate"].IsSequence() && emitterNode["rate"].size() >= 2) {
                config.rate.min = emitterNode["rate"][0].as<float>();
                config.rate.max = emitterNode["rate"][1].as<float>();
            }

            if (emitterNode["lifetime"] && emitterNode["lifetime"].IsSequence() && emitterNode["lifetime"].size() >= 2) {
                config.lifetime.min = emitterNode["lifetime"][0].as<float>();
                config.lifetime.max = emitterNode["lifetime"][1].as<float>();
            }

            if (emitterNode["speed"] && emitterNode["speed"].IsSequence() && emitterNode["speed"].size() >= 2) {
                config.speed.min = emitterNode["speed"][0].as<float>();
                config.speed.max = emitterNode["speed"][1].as<float>();
            }

            if (emitterNode["angle"] && emitterNode["angle"].IsSequence() && emitterNode["angle"].size() >= 2) {
                config.angle.min = emitterNode["angle"][0].as<float>();
                config.angle.max = emitterNode["angle"][1].as<float>();
            }

            if (emitterNode["gravity"] && emitterNode["gravity"].IsSequence() && emitterNode["gravity"].size() >= 2) {
                config.gravity.x = emitterNode["gravity"][0].as<float>();
                config.gravity.y = emitterNode["gravity"][1].as<float>();
            }

            if (emitterNode["drag"]) config.drag = emitterNode["drag"].as<float>();

            if (emitterNode["size_start"] && emitterNode["size_start"].IsSequence() && emitterNode["size_start"].size() >= 2) {
                config.sizeStart.x = emitterNode["size_start"][0].as<float>();
                config.sizeStart.y = emitterNode["size_start"][1].as<float>();
            }

            if (emitterNode["size_end"] && emitterNode["size_end"].IsSequence() && emitterNode["size_end"].size() >= 2) {
                config.sizeEnd.x = emitterNode["size_end"][0].as<float>();
                config.sizeEnd.y = emitterNode["size_end"][1].as<float>();
            }

            if (emitterNode["color_start"] && emitterNode["color_start"].IsSequence() && emitterNode["color_start"].size() >= 4) {
                config.colorStart.r = emitterNode["color_start"][0].as<float>();
                config.colorStart.g = emitterNode["color_start"][1].as<float>();
                config.colorStart.b = emitterNode["color_start"][2].as<float>();
                config.colorStart.a = emitterNode["color_start"][3].as<float>();
            }

            if (emitterNode["color_end"] && emitterNode["color_end"].IsSequence() && emitterNode["color_end"].size() >= 4) {
                config.colorEnd.r = emitterNode["color_end"][0].as<float>();
                config.colorEnd.g = emitterNode["color_end"][1].as<float>();
                config.colorEnd.b = emitterNode["color_end"][2].as<float>();
                config.colorEnd.a = emitterNode["color_end"][3].as<float>();
            }

            // Shape-specific
            if (emitterNode["cone_angle"]) config.coneAngle = emitterNode["cone_angle"].as<float>();
            if (emitterNode["circle_radius"]) config.circleRadius = emitterNode["circle_radius"].as<float>();

            if (emitterNode["box_size"] && emitterNode["box_size"].IsSequence() && emitterNode["box_size"].size() >= 3) {
                config.boxSize.x = emitterNode["box_size"][0].as<float>();
                config.boxSize.y = emitterNode["box_size"][1].as<float>();
                config.boxSize.z = emitterNode["box_size"][2].as<float>();
            }

            if (emitterNode["align_to_velocity"]) config.alignToVelocity = emitterNode["align_to_velocity"].as<bool>();
            if (emitterNode["blend_mode"]) config.texture.blend = stringToBlendMode(emitterNode["blend_mode"].as<std::string>());

            effect.emitters.push_back(config);
        }

        Logger::info() << "Loaded particle effect from: " << path;
        return true;

    } catch (const YAML::Exception& e) {
        Logger::error() << "YAML error while loading effect: " << e.what();
        return false;
    } catch (const std::exception& e) {
        Logger::error() << "Error while loading effect: " << e.what();
        return false;
    }
}

bool ParticleEffectFile::validate(const std::string& path, std::vector<std::string>& errors) {
    errors.clear();

    try {
        if (!fs::exists(path)) {
            errors.push_back("File not found: " + path);
            return false;
        }

        YAML::Node root = YAML::LoadFile(path);

        if (!root["version"]) {
            errors.push_back("Missing 'version' field");
        }

        if (!root["emitters"]) {
            errors.push_back("Missing 'emitters' field");
        } else if (!root["emitters"].IsSequence()) {
            errors.push_back("'emitters' must be a sequence");
        }

        return errors.empty();

    } catch (const YAML::Exception& e) {
        errors.push_back("YAML parse error: " + std::string(e.what()));
        return false;
    }
}
