/**
 * @file script_action.cpp
 * @brief Implementation of YAML-based scripting system
 */

#include "script_action.h"
#include "event_dispatcher.h"
#include "event_types.h"
#include "block_system.h"
#include "structure_system.h"
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
            Logger::warn() << "Error parsing action " << i << " in event '" << eventType << "': " << e.what();
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
                Logger::warn() << "Unknown block name: " << action.blockName;
                break;
            }

            // TODO: Actually place the block in the world
            // This requires a World reference, which we'll need to pass through
            Logger::info() << "PLACE_BLOCK: " << action.blockName << " at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
            break;
        }

        case ActionType::BREAK_BLOCK: {
            // TODO: Break block at target position
            Logger::info() << "BREAK_BLOCK at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
            break;
        }

        case ActionType::SPAWN_STRUCTURE: {
            // TODO: Spawn structure at target position
            Logger::info() << "SPAWN_STRUCTURE: " << action.structureName << " at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
            break;
        }

        case ActionType::SPAWN_PARTICLES: {
            // TODO: Spawn particle effect
            Logger::info() << "SPAWN_PARTICLES: " << action.particleName << " at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
            break;
        }

        case ActionType::PLAY_SOUND: {
            // TODO: Play sound effect
            Logger::info() << "PLAY_SOUND: " << action.soundName << " at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
            break;
        }

        case ActionType::RUN_COMMAND: {
            Logger::info() << "RUN_COMMAND: " << action.command;
            // TODO: Execute console command
            break;
        }

        case ActionType::SET_METADATA: {
            Logger::info() << "SET_METADATA at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
            // TODO: Set block metadata
            for (const auto& pair : action.metadata) {
                Logger::debug() << "  " << pair.first << " = " << pair.second;
            }
            break;
        }

        case ActionType::TRIGGER_UPDATE: {
            // TODO: Schedule block update tick
            Logger::info() << "TRIGGER_UPDATE at ("
                          << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")";
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
            Logger::warn() << "Error executing action: " << e.what();
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
            Logger::warn() << "Error registering event handler '" << handler.eventType
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
