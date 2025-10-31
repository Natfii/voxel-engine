#pragma once

#include <string>
#include <functional>
#include <map>
#include <vector>

// Command handler function signature
// Takes a vector of arguments (arg[0] is the command name itself)
using CommandHandler = std::function<void(const std::vector<std::string>&)>;

// Command information
struct Command {
    std::string name;
    std::string description;
    std::string usage;
    CommandHandler handler;
};

// Singleton registry for console commands
class CommandRegistry {
public:
    static CommandRegistry& instance();

    // Register a new command
    void registerCommand(const std::string& name,
                        const std::string& description,
                        const std::string& usage,
                        CommandHandler handler);

    // Execute a command line (parses and calls handler)
    bool executeCommand(const std::string& commandLine);

    // Get all registered commands (for autocomplete/help)
    const std::map<std::string, Command>& getCommands() const { return m_commands; }

    // Get command suggestions for autocomplete
    std::vector<std::string> getSuggestions(const std::string& partial) const;

private:
    CommandRegistry() = default;
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    std::map<std::string, Command> m_commands;

    // Parse command line into tokens (handles quotes)
    std::vector<std::string> parseCommandLine(const std::string& commandLine) const;
};
