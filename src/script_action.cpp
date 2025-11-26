/**
 * @file script_action.cpp
 * @brief Implementation of YAML-based scripting system
 */

#include "script_action.h"
#include "event_dispatcher.h"
#include "event_types.h"
#include "block_system.h"
#include "structure_system.h"
#include "engine_api.h"
#include "command_registry.h"
#include "logger.h"
#include <unordered_map>
#include <random>
#include <algorithm>
#include <cctype>

// Forward declarations
class World; // We'll need this for executing actions

// Global storage for block event handlers
// Maps blockID -> list of listener handles for cleanup
static std::unordered_map<int, std::vector<ListenerHandle>> g_blockEventHandles;

// Global storage for biome event handlers
// Maps biomeName -> list of listener handles for cleanup
static std::unordered_map<std::string, std::vector<ListenerHandle>> g_biomeEventHandles;

// Random number generator for probability checks
static std::random_device g_rd;
static std::mt19937 g_rng(g_rd());

/**
 * @brief Helper function to normalize string (lowercase, trim whitespace)
 */
static std::string normalizeString(const std::string& str) {
    std::string result = str;

    // Trim whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);

    // Convert to lowercase
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return result;
}

/**
 * @brief Roll a probability check
 * @param probability Percentage chance (0-100)
 * @return true if check passes, false otherwise
 */
static bool rollProbability(int probability) {
    if (probability >= 100) return true;
    if (probability <= 0) return false;

    std::uniform_int_distribution<int> dist(1, 100);
    return dist(g_rng) <= probability;
}

/**
 * @brief Parse glm::ivec3 from YAML sequence [x, y, z]
 */
static glm::ivec3 parseOffset(const YAML::Node& node) {
    if (!node.IsSequence() || node.size() != 3) {
        throw std::runtime_error("Offset must be a sequence of 3 integers [x, y, z]");
    }

    return glm::ivec3(
        node[0].as<int>(),
        node[1].as<int>(),
        node[2].as<int>()
    );
}

/**
 * @brief Parse ConditionType from string
 */
static ConditionType parseConditionType(const std::string& condStr) {
    std::string normalized = normalizeString(condStr);

    if (normalized == "block_is") return ConditionType::BLOCK_IS;
    if (normalized == "block_is_not") return ConditionType::BLOCK_IS_NOT;
    if (normalized == "random_chance") return ConditionType::RANDOM_CHANCE;
    if (normalized == "time_is_day") return ConditionType::TIME_IS_DAY;
    if (normalized == "time_is_night") return ConditionType::TIME_IS_NIGHT;

    throw std::runtime_error("Unknown condition type: " + condStr);
}

// ============================================================================
// ScriptVariableRegistry Implementation
// ============================================================================

ScriptVariableRegistry& ScriptVariableRegistry::instance() {
    static ScriptVariableRegistry instance;
    return instance;
}

void ScriptVariableRegistry::setVariable(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_variables[name] = value;
}

std::string ScriptVariableRegistry::getVariable(const std::string& name, const std::string& defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_variables.find(name);
    if (it != m_variables.end()) {
        return it->second;
    }
    return defaultValue;
}

void ScriptVariableRegistry::setNumeric(const std::string& name, int value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_variables[name] = std::to_string(value);
}

int ScriptVariableRegistry::getNumeric(const std::string& name, int defaultValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_variables.find(name);
    if (it != m_variables.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            return defaultValue;
        }
    }
    return defaultValue;
}

void ScriptVariableRegistry::increment(const std::string& name, int amount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int currentValue = 0;
    auto it = m_variables.find(name);
    if (it != m_variables.end()) {
        try {
            currentValue = std::stoi(it->second);
        } catch (const std::exception&) {
            // If parsing fails, treat as 0
            currentValue = 0;
        }
    }
    m_variables[name] = std::to_string(currentValue + amount);
}

void ScriptVariableRegistry::decrement(const std::string& name, int amount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int currentValue = 0;
    auto it = m_variables.find(name);
    if (it != m_variables.end()) {
        try {
            currentValue = std::stoi(it->second);
        } catch (const std::exception&) {
            // If parsing fails, treat as 0
            currentValue = 0;
        }
    }
    m_variables[name] = std::to_string(currentValue - amount);
}

void ScriptVariableRegistry::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_variables.clear();
}

// ============================================================================
// ActionType Parsing
// ============================================================================

ActionType parseActionType(const std::string& typeStr) {
    std::string normalized = normalizeString(typeStr);

    if (normalized == "place_block") return ActionType::PLACE_BLOCK;
    if (normalized == "break_block") return ActionType::BREAK_BLOCK;
    if (normalized == "spawn_structure") return ActionType::SPAWN_STRUCTURE;
    if (normalized == "spawn_particles") return ActionType::SPAWN_PARTICLES;
    if (normalized == "play_sound") return ActionType::PLAY_SOUND;
    if (normalized == "run_command") return ActionType::RUN_COMMAND;
    if (normalized == "set_metadata") return ActionType::SET_METADATA;
    if (normalized == "trigger_update") return ActionType::TRIGGER_UPDATE;
    if (normalized == "set_variable") return ActionType::SET_VARIABLE;
    if (normalized == "get_variable") return ActionType::GET_VARIABLE;
    if (normalized == "increment_var") return ActionType::INCREMENT_VAR;
    if (normalized == "decrement_var") return ActionType::DECREMENT_VAR;
    if (normalized == "conditional") return ActionType::CONDITIONAL;

    throw std::runtime_error("Unknown action type: " + typeStr);
}

std::string actionTypeToString(ActionType type) {
    switch (type) {
        case ActionType::PLACE_BLOCK: return "place_block";
        case ActionType::BREAK_BLOCK: return "break_block";
        case ActionType::SPAWN_STRUCTURE: return "spawn_structure";
        case ActionType::SPAWN_PARTICLES: return "spawn_particles";
        case ActionType::PLAY_SOUND: return "play_sound";
        case ActionType::RUN_COMMAND: return "run_command";
        case ActionType::SET_METADATA: return "set_metadata";
        case ActionType::TRIGGER_UPDATE: return "trigger_update";
        case ActionType::SET_VARIABLE: return "set_variable";
        case ActionType::GET_VARIABLE: return "get_variable";
        case ActionType::INCREMENT_VAR: return "increment_var";
        case ActionType::DECREMENT_VAR: return "decrement_var";
        case ActionType::CONDITIONAL: return "conditional";
        default: return "unknown";
    }
}

// ============================================================================
// ScriptAction Implementation
// ============================================================================

ScriptAction ScriptAction::fromYAML(const YAML::Node& node) {
    ScriptAction action;

    // Required: type field
    if (!node["type"]) {
        throw std::runtime_error("ScriptAction missing required field 'type'");
    }

    std::string typeStr = node["type"].as<std::string>();
    action.type = parseActionType(typeStr);

    // Optional: offset (default [0, 0, 0])
    if (node["offset"]) {
        action.offset = parseOffset(node["offset"]);
    }

    // Optional: probability (default 100)
    if (node["probability"]) {
        action.probability = node["probability"].as<int>();
        if (action.probability < 0) action.probability = 0;
        if (action.probability > 100) action.probability = 100;
    }

    // Parse type-specific parameters
    switch (action.type) {
        case ActionType::PLACE_BLOCK:
            if (!node["block"]) {
                throw std::runtime_error("PLACE_BLOCK action missing required field 'block'");
            }
            action.blockName = node["block"].as<std::string>();
            break;

        case ActionType::SPAWN_STRUCTURE:
            if (!node["structure"]) {
                throw std::runtime_error("SPAWN_STRUCTURE action missing required field 'structure'");
            }
            action.structureName = node["structure"].as<std::string>();
            break;

        case ActionType::SPAWN_PARTICLES:
            if (!node["particle"]) {
                throw std::runtime_error("SPAWN_PARTICLES action missing required field 'particle'");
            }
            action.particleName = node["particle"].as<std::string>();
            break;

        case ActionType::PLAY_SOUND:
            if (!node["sound"]) {
                throw std::runtime_error("PLAY_SOUND action missing required field 'sound'");
            }
            action.soundName = node["sound"].as<std::string>();
            break;

        case ActionType::RUN_COMMAND:
            if (!node["command"]) {
                throw std::runtime_error("RUN_COMMAND action missing required field 'command'");
            }
            action.command = node["command"].as<std::string>();
            break;

        case ActionType::SET_METADATA:
            if (!node["metadata"]) {
                throw std::runtime_error("SET_METADATA action missing required field 'metadata'");
            }
            // Parse metadata as key-value pairs
            for (auto it = node["metadata"].begin(); it != node["metadata"].end(); ++it) {
                std::string key = it->first.as<std::string>();
                std::string value = it->second.as<std::string>();
                action.metadata[key] = value;
            }
            break;

        case ActionType::SET_VARIABLE:
            if (!node["name"]) {
                throw std::runtime_error("SET_VARIABLE action missing required field 'name'");
            }
            if (!node["value"]) {
                throw std::runtime_error("SET_VARIABLE action missing required field 'value'");
            }
            action.variableName = node["name"].as<std::string>();
            action.variableValue = node["value"].as<std::string>();
            break;

        case ActionType::GET_VARIABLE:
            if (!node["name"]) {
                throw std::runtime_error("GET_VARIABLE action missing required field 'name'");
            }
            action.variableName = node["name"].as<std::string>();
            // Optional default value
            if (node["default"]) {
                action.variableValue = node["default"].as<std::string>();
            }
            break;

        case ActionType::INCREMENT_VAR:
        case ActionType::DECREMENT_VAR:
            if (!node["name"]) {
                throw std::runtime_error("INCREMENT_VAR/DECREMENT_VAR action missing required field 'name'");
            }
            action.variableName = node["name"].as<std::string>();
            // Optional amount (default 1)
            if (node["amount"]) {
                action.incrementAmount = node["amount"].as<int>();
            }
            break;

        case ActionType::CONDITIONAL:
            if (!node["condition"]) {
                throw std::runtime_error("CONDITIONAL action missing required field 'condition'");
            }
            action.conditionType = parseConditionType(node["condition"].as<std::string>());

            // Parse condition-specific parameters
            if (node["value"]) {
                action.conditionValue = node["value"].as<std::string>();
            }

            // Parse then actions
            if (!node["then"]) {
                throw std::runtime_error("CONDITIONAL action missing required field 'then'");
            }
            if (!node["then"].IsSequence()) {
                throw std::runtime_error("CONDITIONAL 'then' field must be a sequence of actions");
            }
            for (size_t i = 0; i < node["then"].size(); i++) {
                action.thenActions.push_back(ScriptAction::fromYAML(node["then"][i]));
            }

            // Parse else actions (optional)
            if (node["else"]) {
                if (!node["else"].IsSequence()) {
                    throw std::runtime_error("CONDITIONAL 'else' field must be a sequence of actions");
                }
                for (size_t i = 0; i < node["else"].size(); i++) {
                    action.elseActions.push_back(ScriptAction::fromYAML(node["else"][i]));
                }
            }
            break;

        case ActionType::BREAK_BLOCK:
        case ActionType::TRIGGER_UPDATE:
            // No additional parameters needed
            break;
    }

    return action;
}

// ============================================================================
// ScriptEventHandler Implementation
// ============================================================================

ScriptEventHandler ScriptEventHandler::fromYAML(const std::string& eventType, const YAML::Node& node) {
    ScriptEventHandler handler;
    handler.eventType = eventType;

    if (!node.IsSequence()) {
        throw std::runtime_error("Event handler for '" + eventType + "' must be a sequence of actions");
    }

    // Parse each action in the sequence
    for (size_t i = 0; i < node.size(); i++) {
        try {
            ScriptAction action = ScriptAction::fromYAML(node[i]);
            handler.actions.push_back(action);
        } catch (const std::exception& e) {
            Logger::warning() << "Error parsing action " << i << " in event '" << eventType << "': " << e.what();
            // Continue parsing other actions
        }
    }

    return handler;
}

// ============================================================================
// Action Execution
// ============================================================================

/**
 * @brief Execute a single action at a specific position
 * @param action The action to execute
 * @param position World position where the event occurred
 */
static void executeAction(const ScriptAction& action, const glm::ivec3& position) {
    // Check probability
    if (!rollProbability(action.probability)) {
        return; // Failed probability check
    }

    // Calculate target position with offset
    glm::ivec3 targetPos = position + action.offset;

    Logger::debug() << "Executing action " << actionTypeToString(action.type)
                   << " at position (" << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";

    switch (action.type) {
        case ActionType::PLACE_BLOCK: {
            // Get block ID from name
            auto& registry = BlockRegistry::instance();
            int blockID = registry.getID(action.blockName);

            if (blockID < 0) {
                Logger::warning() << "Unknown block name: " << action.blockName;
                break;
            }

            // Place the block using EngineAPI
            auto& api = EngineAPI::instance();
            if (api.isInitialized()) {
                if (api.placeBlock(targetPos, blockID)) {
                    Logger::debug() << "PLACE_BLOCK: " << action.blockName << " at ("
                                   << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                } else {
                    Logger::warning() << "Failed to place block " << action.blockName << " at ("
                                     << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                }
            } else {
                Logger::warning() << "EngineAPI not initialized, cannot place block";
            }
            break;
        }

        case ActionType::BREAK_BLOCK: {
            // Break the block using EngineAPI
            auto& api = EngineAPI::instance();
            if (api.isInitialized()) {
                if (api.breakBlock(targetPos)) {
                    Logger::debug() << "BREAK_BLOCK at ("
                                   << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                } else {
                    Logger::warning() << "Failed to break block at ("
                                     << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                }
            } else {
                Logger::warning() << "EngineAPI not initialized, cannot break block";
            }
            break;
        }

        case ActionType::SPAWN_STRUCTURE: {
            // Spawn structure using EngineAPI
            auto& api = EngineAPI::instance();
            if (api.isInitialized()) {
                if (api.spawnStructure(action.structureName, targetPos)) {
                    Logger::debug() << "SPAWN_STRUCTURE: " << action.structureName << " at ("
                                   << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                } else {
                    Logger::warning() << "Failed to spawn structure " << action.structureName << " at ("
                                     << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                }
            } else {
                Logger::warning() << "EngineAPI not initialized, cannot spawn structure";
            }
            break;
        }

        case ActionType::SPAWN_PARTICLES: {
            // Spawn particle effect using EngineAPI
            auto& api = EngineAPI::instance();
            if (api.isInitialized()) {
                // Convert integer position to float for particle system
                glm::vec3 particlePos(
                    static_cast<float>(targetPos.x) + 0.5f,  // Center of block
                    static_cast<float>(targetPos.y) + 0.5f,
                    static_cast<float>(targetPos.z) + 0.5f
                );

                if (api.spawnParticles(action.particleName, particlePos)) {
                    Logger::debug() << "SPAWN_PARTICLES: " << action.particleName << " at ("
                                   << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                } else {
                    Logger::warning() << "Failed to spawn particles '" << action.particleName << "' at ("
                                     << targetPos.x << ", " << targetPos.y << ", " << targetPos.z
                                     << "): unknown effect name";
                }
            } else {
                Logger::warning() << "EngineAPI not initialized, cannot spawn particles";
            }
            break;
        }

        case ActionType::PLAY_SOUND: {
            // TODO: Play sound effect (not yet implemented in EngineAPI)
            Logger::info() << "PLAY_SOUND: " << action.soundName << " at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
            break;
        }

        case ActionType::RUN_COMMAND: {
            Logger::info() << "RUN_COMMAND: " << action.command;
            bool success = CommandRegistry::instance().executeCommand(action.command);
            if (!success) {
                Logger::warning() << "Command failed or not found: " << action.command;
            }
            break;
        }

        case ActionType::SET_METADATA: {
            auto& api = EngineAPI::instance();
            if (api.isInitialized()) {
                // Set block metadata for each key-value pair
                for (const auto& pair : action.metadata) {
                    // Parse the value as uint8_t
                    try {
                        uint8_t metadataValue = static_cast<uint8_t>(std::stoi(pair.second));
                        if (api.setBlockMetadata(targetPos, metadataValue)) {
                            Logger::debug() << "SET_METADATA at ("
                                           << targetPos.x << ", " << targetPos.y << ", " << targetPos.z
                                           << "): " << pair.first << " = " << (int)metadataValue;
                        } else {
                            Logger::warning() << "Failed to set metadata at ("
                                             << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                        }
                    } catch (const std::exception& e) {
                        Logger::warning() << "Invalid metadata value for " << pair.first
                                         << ": " << pair.second << " (" << e.what() << ")";
                    }
                }
            } else {
                Logger::warning() << "EngineAPI not initialized, cannot set metadata";
            }
            break;
        }

        case ActionType::TRIGGER_UPDATE: {
            // Dispatch a block update event
            auto& api = EngineAPI::instance();
            if (api.isInitialized()) {
                // Get the block ID at the target position
                auto blockQuery = api.getBlockAt(targetPos);
                if (blockQuery.valid) {
                    EventDispatcher::instance().dispatch(
                        std::make_unique<BlockUpdateEvent>(targetPos, blockQuery.blockID)
                    );
                    Logger::debug() << "TRIGGER_UPDATE at ("
                                   << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
                } else {
                    Logger::warning() << "Failed to trigger update at ("
                                     << targetPos.x << ", " << targetPos.y << ", " << targetPos.z
                                     << "): invalid block position";
                }
            } else {
                Logger::warning() << "EngineAPI not initialized, cannot trigger update";
            }
            break;
        }

        case ActionType::SET_VARIABLE: {
            auto& registry = ScriptVariableRegistry::instance();
            registry.setVariable(action.variableName, action.variableValue);
            Logger::debug() << "SET_VARIABLE: " << action.variableName
                           << " = " << action.variableValue;
            break;
        }

        case ActionType::GET_VARIABLE: {
            auto& registry = ScriptVariableRegistry::instance();
            std::string value = registry.getVariable(action.variableName, action.variableValue);
            Logger::debug() << "GET_VARIABLE: " << action.variableName
                           << " = " << value;
            // Note: GET_VARIABLE is primarily for future condition checking
            // For now, it just logs the value
            break;
        }

        case ActionType::INCREMENT_VAR: {
            auto& registry = ScriptVariableRegistry::instance();
            registry.increment(action.variableName, action.incrementAmount);
            int newValue = registry.getNumeric(action.variableName);
            Logger::debug() << "INCREMENT_VAR: " << action.variableName
                           << " by " << action.incrementAmount
                           << " (new value: " << newValue << ")";
            break;
        }

        case ActionType::DECREMENT_VAR: {
            auto& registry = ScriptVariableRegistry::instance();
            registry.decrement(action.variableName, action.incrementAmount);
            int newValue = registry.getNumeric(action.variableName);
            Logger::debug() << "DECREMENT_VAR: " << action.variableName
                           << " by " << action.incrementAmount
                           << " (new value: " << newValue << ")";
            break;
        }

        case ActionType::CONDITIONAL: {
            // Evaluate the condition
            bool conditionResult = false;

            switch (action.conditionType) {
                case ConditionType::BLOCK_IS: {
                    auto& api = EngineAPI::instance();
                    if (api.isInitialized()) {
                        auto blockQuery = api.getBlockAt(targetPos);
                        if (blockQuery.valid) {
                            auto& registry = BlockRegistry::instance();
                            std::string blockName = registry.getBlockName(blockQuery.blockID);
                            conditionResult = (normalizeString(blockName) == normalizeString(action.conditionValue));
                        }
                    }
                    break;
                }

                case ConditionType::BLOCK_IS_NOT: {
                    auto& api = EngineAPI::instance();
                    if (api.isInitialized()) {
                        auto blockQuery = api.getBlockAt(targetPos);
                        if (blockQuery.valid) {
                            auto& registry = BlockRegistry::instance();
                            std::string blockName = registry.getBlockName(blockQuery.blockID);
                            conditionResult = (normalizeString(blockName) != normalizeString(action.conditionValue));
                        } else {
                            conditionResult = true; // Invalid position counts as "not matching"
                        }
                    }
                    break;
                }

                case ConditionType::RANDOM_CHANCE: {
                    try {
                        int chance = std::stoi(action.conditionValue);
                        conditionResult = rollProbability(chance);
                    } catch (const std::exception& e) {
                        Logger::warning() << "Invalid random chance value: " << action.conditionValue;
                        conditionResult = false;
                    }
                    break;
                }

                case ConditionType::TIME_IS_DAY: {
                    auto& api = EngineAPI::instance();
                    if (api.isInitialized()) {
                        // Minecraft-style: daytime is 0-12000 in 24000-tick cycle
                        float time = api.getTimeOfDay();
                        conditionResult = (time >= 0.0f && time < 12000.0f);
                    }
                    break;
                }

                case ConditionType::TIME_IS_NIGHT: {
                    auto& api = EngineAPI::instance();
                    if (api.isInitialized()) {
                        // Minecraft-style: nighttime is 12000-24000 in 24000-tick cycle
                        float time = api.getTimeOfDay();
                        conditionResult = (time >= 12000.0f && time < 24000.0f);
                    }
                    break;
                }
            }

            Logger::debug() << "CONDITIONAL: condition evaluated to " << (conditionResult ? "true" : "false");

            // Execute appropriate actions based on condition result
            if (conditionResult) {
                for (const auto& thenAction : action.thenActions) {
                    executeAction(thenAction, position);
                }
            } else {
                for (const auto& elseAction : action.elseActions) {
                    executeAction(elseAction, position);
                }
            }
            break;
        }
    }
}

/**
 * @brief Execute all actions in a handler
 */
static void executeHandler(const ScriptEventHandler& handler, const glm::ivec3& position) {
    Logger::debug() << "Executing " << handler.actions.size() << " actions for event: " << handler.eventType;

    for (const auto& action : handler.actions) {
        try {
            executeAction(action, position);
        } catch (const std::exception& e) {
            Logger::warning() << "Error executing action: " << e.what();
            // Continue executing other actions
        }
    }
}

// ============================================================================
// Event Handler Registration
// ============================================================================

/**
 * @brief Convert event type string to EventType enum
 */
static EventType stringToEventType(const std::string& eventStr) {
    std::string normalized = normalizeString(eventStr);

    if (normalized == "on_break") return EventType::BLOCK_BREAK;
    if (normalized == "on_place") return EventType::BLOCK_PLACE;
    if (normalized == "on_step") return EventType::BLOCK_STEP;
    if (normalized == "on_neighbor_change" || normalized == "on_neighbor_changed") {
        return EventType::NEIGHBOR_CHANGED;
    }
    if (normalized == "on_interact") return EventType::BLOCK_INTERACT;
    if (normalized == "on_update") return EventType::BLOCK_UPDATE;

    // Chunk/World events (for biomes)
    if (normalized == "on_chunk_load" || normalized == "chunk_load") return EventType::CHUNK_LOAD;
    if (normalized == "on_chunk_unload" || normalized == "chunk_unload") return EventType::CHUNK_UNLOAD;
    if (normalized == "on_world_save" || normalized == "world_save") return EventType::WORLD_SAVE;
    if (normalized == "on_world_load" || normalized == "world_load") return EventType::WORLD_LOAD;

    // Time events (for biomes)
    if (normalized == "on_time_change" || normalized == "time_change") return EventType::TIME_CHANGE;
    if (normalized == "on_day_start" || normalized == "day_start") return EventType::DAY_START;
    if (normalized == "on_night_start" || normalized == "night_start") return EventType::NIGHT_START;

    throw std::runtime_error("Unknown event type: " + eventStr);
}

void registerBlockEventHandlers(int blockID, const std::vector<ScriptEventHandler>& handlers) {
    if (handlers.empty()) {
        return;
    }

    auto& dispatcher = EventDispatcher::instance();
    std::vector<ListenerHandle> handles;

    Logger::info() << "Registering " << handlers.size() << " event handlers for block ID " << blockID;

    for (const auto& handler : handlers) {
        try {
            EventType eventType = stringToEventType(handler.eventType);

            // Create a callback that filters by block ID and position
            auto callback = [blockID, handler](Event& event) {
                glm::ivec3 position;
                int eventBlockID = -1;

                // Extract position and block ID from event
                switch (event.type) {
                    case EventType::BLOCK_BREAK: {
                        auto& e = static_cast<BlockBreakEvent&>(event);
                        position = e.position;
                        eventBlockID = e.blockID;
                        break;
                    }
                    case EventType::BLOCK_PLACE: {
                        auto& e = static_cast<BlockPlaceEvent&>(event);
                        position = e.position;
                        eventBlockID = e.blockID;
                        break;
                    }
                    case EventType::BLOCK_STEP: {
                        auto& e = static_cast<BlockStepEvent&>(event);
                        position = e.position;
                        eventBlockID = e.blockID;
                        break;
                    }
                    case EventType::BLOCK_INTERACT: {
                        auto& e = static_cast<BlockInteractEvent&>(event);
                        position = e.position;
                        eventBlockID = e.blockID;
                        break;
                    }
                    case EventType::BLOCK_UPDATE: {
                        auto& e = static_cast<BlockUpdateEvent&>(event);
                        position = e.position;
                        eventBlockID = e.blockID;
                        break;
                    }
                    case EventType::NEIGHBOR_CHANGED: {
                        auto& e = static_cast<NeighborChangedEvent&>(event);
                        position = e.position;
                        // For neighbor events, we need to check the block at position
                        // TODO: Get block ID from world at position
                        // For now, we'll skip this check and execute for all neighbor events
                        eventBlockID = blockID;
                        break;
                    }
                    default:
                        return; // Unsupported event type for block scripts
                }

                // Only execute if the event is for this specific block
                if (eventBlockID == blockID) {
                    executeHandler(handler, position);
                }
            };

            // Subscribe to the event
            std::string ownerStr = "block:" + std::to_string(blockID);
            ListenerHandle handle = dispatcher.subscribe(
                eventType,
                callback,
                EventPriority::NORMAL,
                ownerStr
            );

            handles.push_back(handle);

            Logger::debug() << "  Registered handler for " << handler.eventType
                           << " with " << handler.actions.size() << " actions";

        } catch (const std::exception& e) {
            Logger::warning() << "Error registering event handler '" << handler.eventType
                          << "' for block " << blockID << ": " << e.what();
        }
    }

    // Store handles for cleanup
    if (!handles.empty()) {
        g_blockEventHandles[blockID] = std::move(handles);
    }
}

void unregisterBlockEventHandlers(int blockID) {
    auto it = g_blockEventHandles.find(blockID);
    if (it == g_blockEventHandles.end()) {
        return; // No handlers registered for this block
    }

    auto& dispatcher = EventDispatcher::instance();

    // Unsubscribe all handles
    for (ListenerHandle handle : it->second) {
        dispatcher.unsubscribe(handle);
    }

    // Remove from map
    g_blockEventHandles.erase(it);

    Logger::debug() << "Unregistered event handlers for block ID " << blockID;
}

// ============================================================================
// Biome Event Handler Registration
// ============================================================================

void registerBiomeEventHandlers(const std::string& biomeName, const std::vector<ScriptEventHandler>& handlers) {
    if (handlers.empty()) {
        return;
    }

    auto& dispatcher = EventDispatcher::instance();
    std::vector<ListenerHandle> handles;

    Logger::info() << "Registering " << handlers.size() << " event handlers for biome: " << biomeName;

    for (const auto& handler : handlers) {
        try {
            EventType eventType = stringToEventType(handler.eventType);

            // Create a callback that filters by biome name and position
            auto callback = [biomeName, handler](Event& event) {
                try {
                    // Safety check - don't process during world loading
                    auto& api = EngineAPI::instance();
                    if (!api.isInitialized()) {
                        return; // Skip if API not ready
                    }

                    glm::ivec3 position;
                    bool isValidEvent = false;
                    bool requiresBiomeCheck = true;

                    // Extract position from event based on event type
                    switch (event.type) {
                        case EventType::CHUNK_LOAD: {
                            auto& e = static_cast<ChunkLoadEvent&>(event);
                            // For chunk events, use chunk center position
                            position = glm::ivec3(e.chunkX * 16 + 8, 0, e.chunkZ * 16 + 8);
                            isValidEvent = true;
                            break;
                        }
                        case EventType::CHUNK_UNLOAD: {
                            auto& e = static_cast<ChunkUnloadEvent&>(event);
                            // For chunk events, use chunk center position
                            position = glm::ivec3(e.chunkX * 16 + 8, 0, e.chunkZ * 16 + 8);
                            isValidEvent = true;
                            break;
                        }
                        case EventType::TIME_CHANGE:
                        case EventType::DAY_START:
                        case EventType::NIGHT_START:
                            // Time events - skip biome filtering
                            position = glm::ivec3(0);
                            isValidEvent = true;
                            requiresBiomeCheck = false;
                            break;
                        case EventType::WORLD_SAVE:
                        case EventType::WORLD_LOAD:
                            // World events - skip biome filtering
                            position = glm::ivec3(0);
                            isValidEvent = true;
                            requiresBiomeCheck = false;
                            break;
                        default:
                            return; // Unsupported event type for biome scripts
                    }

                    // Only execute if this is a valid event
                    if (isValidEvent) {
                        if (!requiresBiomeCheck) {
                            executeHandler(handler, position);
                            return;
                        }

                        // Check if the position is in the biome using EngineAPI
                        std::string currentBiome = api.getBiomeAt(position.x, position.z);

                        // Normalize biome name for comparison
                        std::string normalizedBiome = normalizeString(currentBiome);
                        std::string normalizedTarget = normalizeString(biomeName);

                        if (normalizedBiome == normalizedTarget) {
                            executeHandler(handler, position);
                        }
                    }
                } catch (const std::exception& e) {
                    Logger::warning() << "Biome event handler error: " << e.what();
                } catch (...) {
                    Logger::warning() << "Unknown error in biome event handler";
                }
            };

            // Subscribe to the event
            std::string ownerStr = "biome:" + biomeName;
            ListenerHandle handle = dispatcher.subscribe(
                eventType,
                callback,
                EventPriority::NORMAL,
                ownerStr
            );

            handles.push_back(handle);

            Logger::debug() << "  Registered handler for " << handler.eventType
                           << " with " << handler.actions.size() << " actions";

        } catch (const std::exception& e) {
            Logger::warning() << "Error registering event handler '" << handler.eventType
                          << "' for biome " << biomeName << ": " << e.what();
        }
    }

    // Store handles for cleanup
    if (!handles.empty()) {
        g_biomeEventHandles[biomeName] = std::move(handles);
    }
}

void unregisterBiomeEventHandlers(const std::string& biomeName) {
    auto it = g_biomeEventHandles.find(biomeName);
    if (it == g_biomeEventHandles.end()) {
        return; // No handlers registered for this biome
    }

    auto& dispatcher = EventDispatcher::instance();

    // Unsubscribe all handles
    for (ListenerHandle handle : it->second) {
        dispatcher.unsubscribe(handle);
    }

    // Remove from map
    g_biomeEventHandles.erase(it);

    Logger::debug() << "Unregistered event handlers for biome: " << biomeName;
}
