#include "command_registry.h"
#include <algorithm>
#include <sstream>
#include <iostream>

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry instance;
    return instance;
}

void CommandRegistry::registerCommand(const std::string& name,
                                     const std::string& description,
                                     const std::string& usage,
                                     CommandHandler handler) {
    Command cmd;
    cmd.name = name;
    cmd.description = description;
    cmd.usage = usage;
    cmd.handler = handler;

    m_commands[name] = cmd;
}

bool CommandRegistry::executeCommand(const std::string& commandLine) {
    if (commandLine.empty()) {
        return false;
    }

    // Parse command line into arguments
    std::vector<std::string> args = parseCommandLine(commandLine);
    if (args.empty()) {
        return false;
    }

    // Find the command
    std::string commandName = args[0];
    auto it = m_commands.find(commandName);
    if (it == m_commands.end()) {
        std::cerr << "Unknown command: " << commandName << std::endl;
        return false;
    }

    // Execute the command
    try {
        it->second.handler(args);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Command error: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> CommandRegistry::getSuggestions(const std::string& partial) const {
    std::vector<std::string> suggestions;

    if (partial.empty()) {
        // Return all commands
        for (const auto& pair : m_commands) {
            suggestions.push_back(pair.first);
        }
    } else {
        // Return commands that start with partial
        for (const auto& pair : m_commands) {
            if (pair.first.find(partial) == 0) {
                suggestions.push_back(pair.first);
            }
        }
    }

    return suggestions;
}

std::vector<std::string> CommandRegistry::parseCommandLine(const std::string& commandLine) const {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < commandLine.length(); i++) {
        char c = commandLine[i];

        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    // Add final token
    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}
