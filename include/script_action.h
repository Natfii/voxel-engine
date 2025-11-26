#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

/**
 * @file script_action.h
 * @brief YAML-based scripting system for blocks and biomes
 *
 * This system allows content creators to define event-driven behaviors
 * entirely in YAML without writing C++ code. Actions are triggered by
 * events like block breaking, placement, neighbor changes, etc.
 *
 * Example YAML:
 * @code
 * events:
 *   on_break:
 *     - type: spawn_structure
 *       structure: "hidden_treasure"
 *       offset: [0, -1, 0]
 *       probability: 50
 *   on_step:
 *     - type: play_sound
 *       sound: "pressure_plate_click"
 * @endcode
 */

/**
 * @brief Thread-safe registry for storing script variables
 *
 * This singleton class provides a global key-value store for script variables.
 * Variables can be used to maintain state across script executions, implement
 * counters, flags, and other stateful behaviors.
 *
 * Example usage:
 * @code
 * auto& registry = ScriptVariableRegistry::instance();
 * registry.setVariable("counter", "0");
 * registry.increment("counter");
 * int count = registry.getNumeric("counter"); // Returns 1
 * @endcode
 */
class ScriptVariableRegistry {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the global ScriptVariableRegistry instance
     */
    static ScriptVariableRegistry& instance();

    /**
     * @brief Set a variable to a string value
     * @param name Variable name
     * @param value String value to store
     */
    void setVariable(const std::string& name, const std::string& value);

    /**
     * @brief Get a variable's string value
     * @param name Variable name
     * @param defaultValue Default value if variable doesn't exist
     * @return Variable value or default if not found
     */
    std::string getVariable(const std::string& name, const std::string& defaultValue = "");

    /**
     * @brief Set a variable to a numeric value
     * @param name Variable name
     * @param value Integer value to store
     */
    void setNumeric(const std::string& name, int value);

    /**
     * @brief Get a variable's numeric value
     * @param name Variable name
     * @param defaultValue Default value if variable doesn't exist or isn't numeric
     * @return Variable value as integer or default if not found/invalid
     */
    int getNumeric(const std::string& name, int defaultValue = 0);

    /**
     * @brief Increment a numeric variable
     * @param name Variable name
     * @param amount Amount to increment by (default 1)
     */
    void increment(const std::string& name, int amount = 1);

    /**
     * @brief Decrement a numeric variable
     * @param name Variable name
     * @param amount Amount to decrement by (default 1)
     */
    void decrement(const std::string& name, int amount = 1);

    /**
     * @brief Clear all variables
     */
    void clear();

private:
    std::unordered_map<std::string, std::string> m_variables;
    std::mutex m_mutex;
};

/**
 * @brief Action types that can be triggered by events
 *
 * These actions represent the operations that can be performed
 * when an event fires. Each action type has its own set of parameters.
 */
enum class ActionType {
    PLACE_BLOCK,       ///< Place a block at a position (uses blockName + offset)
    BREAK_BLOCK,       ///< Break a block at a position (uses offset)
    SPAWN_STRUCTURE,   ///< Spawn a structure at a position (uses structureName + offset)
    SPAWN_PARTICLES,   ///< Spawn particle effects (uses particleName + offset)
    PLAY_SOUND,        ///< Play a sound effect (uses soundName) - Future
    RUN_COMMAND,       ///< Execute a console command (uses command)
    SET_METADATA,      ///< Set block metadata (uses metadata map)
    TRIGGER_UPDATE,    ///< Schedule a block update tick (uses offset)
    SET_VARIABLE,      ///< Set a variable value (uses variableName + variableValue)
    GET_VARIABLE,      ///< Get a variable value (uses variableName, for conditions)
    INCREMENT_VAR,     ///< Increment a numeric variable (uses variableName + incrementAmount)
    DECREMENT_VAR,     ///< Decrement a numeric variable (uses variableName + incrementAmount)
    CONDITIONAL        ///< If/else based on conditions
};

/**
 * @brief Condition types for conditional actions
 *
 * These condition types determine what to check when evaluating
 * a conditional action's logic.
 */
enum class ConditionType {
    BLOCK_IS,        ///< Check if block at position is specific type
    BLOCK_IS_NOT,    ///< Check if block at position is NOT specific type
    RANDOM_CHANCE,   ///< Random probability check
    TIME_IS_DAY,     ///< Check if daytime
    TIME_IS_NIGHT    ///< Check if nighttime
};

/**
 * @brief Forward declaration for recursive action structure
 */
struct ScriptAction;

/**
 * @brief A single action to execute in response to an event
 *
 * Actions are the building blocks of the scripting system. Each action
 * represents a single operation that can be performed when an event fires.
 * Actions can be conditional (probability) and positional (offset).
 */
struct ScriptAction {
    ActionType type;          ///< Type of action to perform

    // Parameters (varies by type)
    std::string blockName;       ///< Block name for PLACE_BLOCK
    std::string structureName;   ///< Structure name for SPAWN_STRUCTURE
    std::string particleName;    ///< Particle effect name for SPAWN_PARTICLES
    std::string soundName;       ///< Sound effect name for PLAY_SOUND
    std::string command;         ///< Console command for RUN_COMMAND
    glm::ivec3 offset{0,0,0};    ///< Relative position offset from event location
    int probability = 100;       ///< Chance to execute (0-100, default 100 = always)

    // Metadata for SET_METADATA action
    std::unordered_map<std::string, std::string> metadata;

    // Variable-related parameters
    std::string variableName;    ///< Variable name for SET_VARIABLE, GET_VARIABLE, INCREMENT_VAR, DECREMENT_VAR
    std::string variableValue;   ///< Value to set for SET_VARIABLE
    int incrementAmount = 1;     ///< Amount to increment/decrement for INCREMENT_VAR, DECREMENT_VAR

    // Conditional action parameters
    ConditionType conditionType;           ///< Type of condition to evaluate
    std::string conditionValue;            ///< Block name for BLOCK_IS/BLOCK_IS_NOT, etc.
    std::vector<ScriptAction> thenActions; ///< Actions to execute if condition is true
    std::vector<ScriptAction> elseActions; ///< Actions to execute if condition is false (optional)

    /**
     * @brief Parse a ScriptAction from a YAML node
     *
     * @param node YAML node containing action definition
     * @return Parsed ScriptAction
     * @throws std::runtime_error if YAML is malformed or missing required fields
     *
     * Example YAML:
     * @code
     * type: place_block
     * block: "stone"
     * offset: [0, 1, 0]
     * probability: 50
     * @endcode
     */
    static ScriptAction fromYAML(const YAML::Node& node);
};

/**
 * @brief Event handler that responds to a specific event type
 *
 * Event handlers contain a list of actions that will be executed in order
 * when the specified event fires. Each handler is tied to a specific event
 * type (e.g., "on_break", "on_place", "on_step").
 */
struct ScriptEventHandler {
    std::string eventType;  ///< Event type: "on_break", "on_place", "on_step", "on_neighbor_change", etc.
    std::vector<ScriptAction> actions;  ///< Actions to execute when event fires

    /**
     * @brief Parse a ScriptEventHandler from a YAML node
     *
     * @param eventType The event type string (e.g., "on_break")
     * @param node YAML node containing list of actions
     * @return Parsed ScriptEventHandler
     * @throws std::runtime_error if YAML is malformed
     *
     * Example YAML:
     * @code
     * on_break:
     *   - type: spawn_structure
     *     structure: "hidden_door"
     *     offset: [0, -1, 0]
     *   - type: run_command
     *     command: "echo Secret revealed!"
     * @endcode
     */
    static ScriptEventHandler fromYAML(const std::string& eventType, const YAML::Node& node);
};

/**
 * @brief Register event handlers for a specific block
 *
 * This function connects YAML-defined event handlers to the EventDispatcher,
 * allowing blocks to respond to events dynamically.
 *
 * @param blockID Block ID to register handlers for
 * @param handlers List of event handlers to register
 *
 * Usage:
 * @code
 * std::vector<ScriptEventHandler> handlers;
 * handlers.push_back(ScriptEventHandler::fromYAML("on_break", yamlNode));
 * registerBlockEventHandlers(50, handlers);
 * @endcode
 */
void registerBlockEventHandlers(int blockID, const std::vector<ScriptEventHandler>& handlers);

/**
 * @brief Unregister all event handlers for a specific block
 *
 * @param blockID Block ID to unregister handlers for
 *
 * Call this when unloading blocks or cleaning up the registry.
 */
void unregisterBlockEventHandlers(int blockID);

/**
 * @brief Register event handlers for a specific biome
 *
 * This function connects YAML-defined event handlers to the EventDispatcher,
 * allowing biomes to respond to events dynamically. Events are filtered by
 * checking if the event position is within the biome.
 *
 * @param biomeName Biome name to register handlers for
 * @param handlers List of event handlers to register
 *
 * Usage:
 * @code
 * std::vector<ScriptEventHandler> handlers;
 * handlers.push_back(ScriptEventHandler::fromYAML("on_chunk_load", yamlNode));
 * registerBiomeEventHandlers("desert", handlers);
 * @endcode
 */
void registerBiomeEventHandlers(const std::string& biomeName, const std::vector<ScriptEventHandler>& handlers);

/**
 * @brief Unregister all event handlers for a specific biome
 *
 * @param biomeName Biome name to unregister handlers for
 *
 * Call this when unloading biomes or cleaning up the registry.
 */
void unregisterBiomeEventHandlers(const std::string& biomeName);

/**
 * @brief Parse action type from string
 *
 * @param typeStr Action type string (e.g., "place_block", "spawn_structure")
 * @return ActionType enum value
 * @throws std::runtime_error if type string is invalid
 */
ActionType parseActionType(const std::string& typeStr);

/**
 * @brief Convert action type to string
 *
 * @param type ActionType enum value
 * @return Action type string
 */
std::string actionTypeToString(ActionType type);
