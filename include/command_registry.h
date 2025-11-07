/**
 * @file command_registry.h
 * @brief Console command registration and execution system
 *
 * Provides a centralized registry for console commands with support for:
 * - Command registration with handlers
 * - Argument parsing and validation
 * - Autocomplete suggestions
 * - Help text and usage information
 */

#pragma once

#include <string>
#include <functional>
#include <map>
#include <vector>

/**
 * @brief Function signature for console command handlers
 *
 * Command handlers receive a vector of string arguments where:
 * - args[0] is the command name itself
 * - args[1..n] are the command arguments
 *
 * Example:
 * @code
 * void cmdTeleport(const std::vector<std::string>& args) {
 *     if (args.size() < 4) {
 *         Logger::error() << "Usage: teleport <x> <y> <z>";
 *         return;
 *     }
 *     float x = std::stof(args[1]);
 *     float y = std::stof(args[2]);
 *     float z = std::stof(args[3]);
 *     player.Position = glm::vec3(x, y, z);
 * }
 * @endcode
 */
using CommandHandler = std::function<void(const std::vector<std::string>&)>;

/**
 * @brief Command metadata and handler
 *
 * Stores all information about a registered console command.
 */
struct Command {
    std::string name;                              ///< Command name (e.g., "help", "teleport")
    std::string description;                       ///< Short description for help text
    std::string usage;                             ///< Usage string (e.g., "teleport <x> <y> <z>")
    CommandHandler handler;                        ///< Function to call when command is executed
    std::vector<std::string> argumentSuggestions;  ///< Optional autocomplete suggestions for arguments
};

/**
 * @brief Singleton registry for console commands
 *
 * The CommandRegistry provides a centralized system for registering and executing
 * console commands. It handles:
 * - Command registration with custom handlers
 * - Command-line parsing (including quoted arguments)
 * - Command execution with error handling
 * - Autocomplete suggestions for both commands and arguments
 *
 * Usage Example:
 * @code
 * // Register a command
 * CommandRegistry::instance().registerCommand(
 *     "spawn",
 *     "Spawn an entity",
 *     "spawn <entity_name>",
 *     cmdSpawn,
 *     {"zombie", "skeleton", "creeper"}  // Autocomplete suggestions
 * );
 *
 * // Execute a command from console input
 * std::string userInput = "spawn zombie";
 * CommandRegistry::instance().executeCommand(userInput);
 * @endcode
 */
class CommandRegistry {
public:
    /**
     * @brief Gets the singleton instance
     * @return Reference to the global CommandRegistry instance
     */
    static CommandRegistry& instance();

    /**
     * @brief Registers a new console command
     *
     * Adds a command to the registry with associated metadata and handler function.
     * Commands can be executed via executeCommand() or from the in-game console.
     *
     * @param name Command name (case-sensitive, no spaces)
     * @param description Short description shown in help text
     * @param usage Usage string (e.g., "command <required> [optional]")
     * @param handler Function to call when command is executed
     * @param argumentSuggestions Optional list of autocomplete suggestions for arguments
     *
     * @note If a command with the same name exists, it will be overwritten
     */
    void registerCommand(const std::string& name,
                        const std::string& description,
                        const std::string& usage,
                        CommandHandler handler,
                        const std::vector<std::string>& argumentSuggestions = {});

    /**
     * @brief Executes a command from a command-line string
     *
     * Parses the command line into tokens and executes the corresponding handler.
     * Handles quoted arguments (e.g., 'spawn "Big Zombie"').
     *
     * @param commandLine Full command string (e.g., "teleport 10 20 30")
     * @return True if command was found and executed, false if command doesn't exist
     *
     * @note Handler execution errors are caught and logged, but return value
     *       only indicates whether the command exists in the registry
     */
    bool executeCommand(const std::string& commandLine);

    /**
     * @brief Gets all registered commands
     *
     * Returns a reference to the internal command map for enumeration.
     * Useful for implementing help systems and command lists.
     *
     * @return Const reference to map of command name → Command struct
     */
    const std::map<std::string, Command>& getCommands() const { return m_commands; }

    /**
     * @brief Gets autocomplete suggestions for partial input
     *
     * Returns matching command names or argument suggestions based on input.
     * Handles both command-level completion and argument-level completion.
     *
     * @param partial Partial input string (e.g., "tel" or "spawn z")
     * @return Vector of suggestion strings
     *
     * @note Suggestions are sorted alphabetically
     */
    std::vector<std::string> getSuggestions(const std::string& partial) const;

    /**
     * @brief Gets full command-line completions
     *
     * Returns complete command lines including arguments for autocomplete.
     * More sophisticated than getSuggestions() as it returns full command strings.
     *
     * @param input Current input string
     * @return Vector of complete command-line suggestions
     */
    std::vector<std::string> getFullCompletions(const std::string& input) const;

private:
    CommandRegistry() = default;
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    std::map<std::string, Command> m_commands;

    // Parse command line into tokens (handles quotes)
    std::vector<std::string> parseCommandLine(const std::string& commandLine) const;
};
