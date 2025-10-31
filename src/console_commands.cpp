#include "console_commands.h"
#include "command_registry.h"
#include "convar.h"
#include "debug_state.h"
#include "player.h"
#include "world.h"
#include <sstream>

// Static member initialization
Console* ConsoleCommands::s_console = nullptr;
Player* ConsoleCommands::s_player = nullptr;
World* ConsoleCommands::s_world = nullptr;

void ConsoleCommands::registerAll(Console* console, Player* player, World* world) {
    s_console = console;
    s_player = player;
    s_world = world;

    auto& registry = CommandRegistry::instance();

    registry.registerCommand("help", "Show all available commands or help for a specific command",
                           "help [command]", cmdHelp);

    registry.registerCommand("clear", "Clear the console output",
                           "clear", cmdClear);

    registry.registerCommand("noclip", "Toggle noclip mode",
                           "noclip", cmdNoclip);

    registry.registerCommand("debug", "Toggle debug rendering modes (render, drawfps)",
                           "debug <render|drawfps>", [](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            s_console->addMessage("Usage: debug <render|drawfps>", ConsoleMessageType::WARNING);
            return;
        }

        if (args[1] == "render") {
            cmdDebugRender(args);
        } else if (args[1] == "drawfps") {
            cmdDebugDrawFPS(args);
        } else {
            s_console->addMessage("Unknown debug option: " + args[1], ConsoleMessageType::ERROR);
            s_console->addMessage("Available options: render, drawfps", ConsoleMessageType::INFO);
        }
    });

    registry.registerCommand("tp", "Teleport to coordinates",
                           "tp <x> <y> <z>", cmdTeleport);

    registry.registerCommand("echo", "Print a message to console",
                           "echo <message>", cmdEcho);

    registry.registerCommand("set", "Set a ConVar value",
                           "set <name> <value>", cmdSet);

    registry.registerCommand("get", "Get a ConVar value",
                           "get <name>", cmdGet);

    registry.registerCommand("list", "List all ConVars",
                           "list", cmdListConVars);
}

void ConsoleCommands::cmdHelp(const std::vector<std::string>& args) {
    if (args.size() > 1) {
        // Show help for specific command
        const auto& commands = CommandRegistry::instance().getCommands();
        auto it = commands.find(args[1]);
        if (it != commands.end()) {
            s_console->addMessage("Command: " + it->second.name, ConsoleMessageType::INFO);
            s_console->addMessage("  " + it->second.description, ConsoleMessageType::INFO);
            s_console->addMessage("  Usage: " + it->second.usage, ConsoleMessageType::INFO);
        } else {
            s_console->addMessage("Unknown command: " + args[1], ConsoleMessageType::ERROR);
        }
    } else {
        // Show all commands
        s_console->addMessage("Available commands:", ConsoleMessageType::INFO);
        const auto& commands = CommandRegistry::instance().getCommands();
        for (const auto& pair : commands) {
            s_console->addMessage("  " + pair.first + " - " + pair.second.description, ConsoleMessageType::INFO);
        }
        s_console->addMessage("Type 'help <command>' for detailed usage", ConsoleMessageType::INFO);
        s_console->addMessage("Type any .md file path to view documentation", ConsoleMessageType::INFO);
    }
}

void ConsoleCommands::cmdClear(const std::vector<std::string>& args) {
    // Clear is handled specially - we need to modify the console's message list
    // For now, just add a bunch of blank lines
    for (int i = 0; i < 50; i++) {
        s_console->addMessage("", ConsoleMessageType::INFO);
    }
}

void ConsoleCommands::cmdNoclip(const std::vector<std::string>& args) {
    DebugState& debug = DebugState::instance();
    debug.noclip.setValue(!debug.noclip.getValue());

    // Sync with player
    if (s_player) {
        s_player->NoclipMode = debug.noclip.getValue();
    }

    s_console->addMessage("Noclip: " + std::string(debug.noclip.getValue() ? "ON" : "OFF"),
                         ConsoleMessageType::INFO);
}

void ConsoleCommands::cmdDebugRender(const std::vector<std::string>& args) {
    DebugState& debug = DebugState::instance();
    debug.renderDebug.setValue(!debug.renderDebug.getValue());
    s_console->addMessage("Debug render: " + std::string(debug.renderDebug.getValue() ? "ON" : "OFF"),
                         ConsoleMessageType::INFO);
}

void ConsoleCommands::cmdDebugDrawFPS(const std::vector<std::string>& args) {
    DebugState& debug = DebugState::instance();
    debug.drawFPS.setValue(!debug.drawFPS.getValue());
    s_console->addMessage("FPS counter: " + std::string(debug.drawFPS.getValue() ? "ON" : "OFF"),
                         ConsoleMessageType::INFO);
}

void ConsoleCommands::cmdTeleport(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        s_console->addMessage("Usage: tp <x> <y> <z>", ConsoleMessageType::WARNING);
        return;
    }

    if (!s_player) {
        s_console->addMessage("Error: Player not available", ConsoleMessageType::ERROR);
        return;
    }

    try {
        float x = std::stof(args[1]);
        float y = std::stof(args[2]);
        float z = std::stof(args[3]);

        s_player->Position = glm::vec3(x, y, z);

        std::ostringstream oss;
        oss << "Teleported to (" << x << ", " << y << ", " << z << ")";
        s_console->addMessage(oss.str(), ConsoleMessageType::INFO);
    } catch (const std::exception& e) {
        s_console->addMessage("Error: Invalid coordinates", ConsoleMessageType::ERROR);
    }
}

void ConsoleCommands::cmdEcho(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return;
    }

    // Combine all args after "echo" into one message
    std::string message;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) message += " ";
        message += args[i];
    }

    s_console->addMessage(message, ConsoleMessageType::INFO);
}

void ConsoleCommands::cmdSet(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        s_console->addMessage("Usage: set <name> <value>", ConsoleMessageType::WARNING);
        return;
    }

    ConVarBase* convar = ConVarManager::instance().findConVar(args[1]);
    if (!convar) {
        s_console->addMessage("Unknown ConVar: " + args[1], ConsoleMessageType::ERROR);
        return;
    }

    convar->setValueFromString(args[2]);
    s_console->addMessage(args[1] + " = " + convar->getValueAsString(), ConsoleMessageType::INFO);
}

void ConsoleCommands::cmdGet(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: get <name>", ConsoleMessageType::WARNING);
        return;
    }

    ConVarBase* convar = ConVarManager::instance().findConVar(args[1]);
    if (!convar) {
        s_console->addMessage("Unknown ConVar: " + args[1], ConsoleMessageType::ERROR);
        return;
    }

    s_console->addMessage(args[1] + " = " + convar->getValueAsString(), ConsoleMessageType::INFO);
    s_console->addMessage("  " + convar->getDescription(), ConsoleMessageType::INFO);
}

void ConsoleCommands::cmdListConVars(const std::vector<std::string>& args) {
    s_console->addMessage("Console variables:", ConsoleMessageType::INFO);

    const auto& convars = ConVarManager::instance().getConVars();
    for (const auto& pair : convars) {
        std::string flags;
        if (pair.second->getFlags() & FCVAR_ARCHIVE) flags += " [ARCHIVE]";
        if (pair.second->getFlags() & FCVAR_CHEAT) flags += " [CHEAT]";

        s_console->addMessage("  " + pair.first + " = " + pair.second->getValueAsString() + flags,
                             ConsoleMessageType::INFO);
    }
}
