#include "command_registry.h"
#include "logger.h"
#include <algorithm>
#include <sstream>

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry instance;
    return instance;
}

void CommandRegistry::registerCommand(const std::string& name,
                                     const std::string& description,
                                     const std::string& usage,
                                     CommandHandler handler,
                                     const std::vector<std::string>& argumentSuggestions) {
    Command cmd;
    cmd.name = name;
    cmd.description = description;
    cmd.usage = usage;
    cmd.handler = handler;
    cmd.argumentSuggestions = argumentSuggestions;

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
        Logger::error() << "Unknown command: " << commandName;
        return false;
    }

    // Execute the command
    try {
        it->second.handler(args);
        return true;
    } catch (const std::exception& e) {
        Logger::error() << "Command error: " << e.what();
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

std::vector<std::string> CommandRegistry::getFullCompletions(const std::string& input) const {
    std::vector<std::string> completions;

    if (input.empty()) {
        // Return all commands
        for (const auto& pair : m_commands) {
            completions.push_back(pair.first);
        }
        return completions;
    }

    // Parse input to find command and current argument being typed
    std::vector<std::string> tokens = parseCommandLine(input);

    // Check if input ends with space (user wants next argument)
    bool endsWithSpace = !input.empty() && input.back() == ' ';

    if (tokens.empty()) {
        return completions;
    }

    std::string commandName = tokens[0];

    // If we only have one token and no trailing space, suggest commands
    if (tokens.size() == 1 && !endsWithSpace) {
        for (const auto& pair : m_commands) {
            if (pair.first.find(commandName) == 0) {
                completions.push_back(pair.first);
            }
        }
        return completions;
    }

    // Find the command
    auto it = m_commands.find(commandName);
    if (it == m_commands.end()) {
        return completions;  // Unknown command, no suggestions
    }

    const Command& cmd = it->second;
    if (cmd.argumentSuggestions.empty()) {
        return completions;  // No argument suggestions for this command
    }

    // Determine what argument we're completing
    std::string partialArg = "";
    if (!endsWithSpace && tokens.size() > 1) {
        partialArg = tokens.back();
    }

    // Build full command suggestions with arguments
    for (const auto& argSuggestion : cmd.argumentSuggestions) {
        // Check if this suggestion matches the partial argument
        if (partialArg.empty() || argSuggestion.find(partialArg) == 0) {
            completions.push_back(commandName + " " + argSuggestion);
        }
    }

    return completions;
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
