#include "console_commands.h"
#include "command_registry.h"
#include "convar.h"
#include "debug_state.h"
#include "player.h"
#include "world.h"
#include "vulkan_renderer.h"
#include "structure_system.h"
#include "raycast.h"
#include <sstream>
#include <iomanip>
#include <cmath>

// Static member initialization
Console* ConsoleCommands::s_console = nullptr;
Player* ConsoleCommands::s_player = nullptr;
World* ConsoleCommands::s_world = nullptr;
VulkanRenderer* ConsoleCommands::s_renderer = nullptr;
float ConsoleCommands::s_timeSpeed = 1.0f;      // Default: normal speed (time flows by default)
float ConsoleCommands::s_currentSkyTime = 0.25f; // Default: morning (sunrise)

void ConsoleCommands::registerAll(Console* console, Player* player, World* world, VulkanRenderer* renderer) {
    s_console = console;
    s_player = player;
    s_world = world;
    s_renderer = renderer;

    auto& registry = CommandRegistry::instance();

    // Build list of all command names for help autocomplete
    std::vector<std::string> allCommands;
    for (const auto& pair : registry.getCommands()) {
        allCommands.push_back(pair.first);
    }

    registry.registerCommand("help", "Show all available commands or help for a specific command",
                           "help [command]", cmdHelp, allCommands);

    registry.registerCommand("clear", "Clear the console output",
                           "clear", cmdClear);

    registry.registerCommand("noclip", "Toggle noclip mode",
                           "noclip", cmdNoclip);

    registry.registerCommand("wireframe", "Toggle wireframe rendering mode",
                           "wireframe", [](const std::vector<std::string>& args) {
        bool newValue = !DebugState::instance().wireframeMode.getValue();
        DebugState::instance().wireframeMode.setValue(newValue);
        s_console->addMessage("Wireframe mode: " + std::string(newValue ? "ON" : "OFF"), ConsoleMessageType::INFO);
    });

    registry.registerCommand("lighting", "Toggle voxel lighting system",
                           "lighting", [](const std::vector<std::string>& args) {
        bool newValue = !DebugState::instance().lightingEnabled.getValue();
        DebugState::instance().lightingEnabled.setValue(newValue);
        s_console->addMessage("Lighting: " + std::string(newValue ? "ON" : "OFF"), ConsoleMessageType::INFO);
        s_console->addMessage("Note: Regenerate chunks (move around) to see effect", ConsoleMessageType::INFO);
    });

    registry.registerCommand("debug", "Toggle debug rendering modes (render, drawfps, targetinfo)",
                           "debug <render|drawfps|targetinfo>", [](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            s_console->addMessage("Usage: debug <render|drawfps|targetinfo>", ConsoleMessageType::WARNING);
            return;
        }

        if (args[1] == "render") {
            cmdDebugRender(args);
        } else if (args[1] == "drawfps") {
            cmdDebugDrawFPS(args);
        } else if (args[1] == "targetinfo") {
            cmdDebugTargetInfo(args);
        } else {
            s_console->addMessage("Unknown debug option: " + args[1], ConsoleMessageType::ERROR);
            s_console->addMessage("Available options: render, drawfps, targetinfo", ConsoleMessageType::INFO);
        }
    }, {"render", "drawfps", "targetinfo"});

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

    registry.registerCommand("skytime", "Set the time of day (0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset)",
                           "skytime <0-1>", cmdSkyTime);

    registry.registerCommand("timespeed", "Set the time progression speed (0=paused, 1=normal, higher=faster)",
                           "timespeed <value>", cmdTimeSpeed, {"0", "0.1", "1", "10", "100"});

    // Get all loaded structure names for autocomplete
    std::vector<std::string> structureNames = StructureRegistry::instance().getAllStructureNames();
    registry.registerCommand("spawnstructure", "Spawn a structure at the targeted ground position",
                           "spawnstructure <name>", cmdSpawnStructure, structureNames);
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
    if (!s_player) {
        s_console->addMessage("Error: Player not available", ConsoleMessageType::ERROR);
        return;
    }

    // Toggle player's noclip state (player owns this state)
    s_player->NoclipMode = !s_player->NoclipMode;

    s_console->addMessage("Noclip: " + std::string(s_player->NoclipMode ? "ON" : "OFF"),
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

void ConsoleCommands::cmdDebugTargetInfo(const std::vector<std::string>& args) {
    DebugState& debug = DebugState::instance();
    debug.showTargetInfo.setValue(!debug.showTargetInfo.getValue());
    s_console->addMessage("Target info: " + std::string(debug.showTargetInfo.getValue() ? "ON" : "OFF"),
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

void ConsoleCommands::cmdSkyTime(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        // Show current sky time and calculated intensities
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "Current sky time: " << s_currentSkyTime;
        s_console->addMessage(oss.str(), ConsoleMessageType::INFO);

        // Calculate and show intensities (same calculation as in renderer)
        float sunIntensity = glm::smoothstep(0.2f, 0.3f, s_currentSkyTime) * (1.0f - glm::smoothstep(0.7f, 0.8f, s_currentSkyTime));
        float moonIntensity = 1.0f - glm::smoothstep(0.15f, 0.25f, s_currentSkyTime) + glm::smoothstep(0.75f, 0.85f, s_currentSkyTime);
        moonIntensity = glm::clamp(moonIntensity, 0.0f, 1.0f);
        float starIntensity = moonIntensity;

        std::ostringstream debugOss;
        debugOss << "DEBUG: sun=" << sunIntensity << " moon=" << moonIntensity << " stars=" << starIntensity;
        s_console->addMessage(debugOss.str(), ConsoleMessageType::INFO);

        s_console->addMessage("Usage: skytime <0-1> (0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset)", ConsoleMessageType::INFO);
        return;
    }

    if (!s_renderer) {
        s_console->addMessage("Error: Renderer not available", ConsoleMessageType::ERROR);
        return;
    }

    try {
        float time = std::stof(args[1]);

        // Clamp to valid range
        time = std::fmax(0.0f, std::fmin(1.0f, time));

        s_currentSkyTime = time;
        s_renderer->setSkyTime(time);

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "Sky time set to: " << time;

        // Add helpful description
        if (time < 0.1f || time > 0.9f) {
            oss << " (night)";
        } else if (time >= 0.1f && time < 0.3f) {
            oss << " (sunrise)";
        } else if (time >= 0.3f && time < 0.7f) {
            oss << " (day)";
        } else if (time >= 0.7f && time <= 0.9f) {
            oss << " (sunset)";
        }

        s_console->addMessage(oss.str(), ConsoleMessageType::INFO);

        // Show calculated intensities for debugging
        float sunIntensity = glm::smoothstep(0.2f, 0.3f, time) * (1.0f - glm::smoothstep(0.7f, 0.8f, time));
        float moonIntensity = 1.0f - glm::smoothstep(0.15f, 0.25f, time) + glm::smoothstep(0.75f, 0.85f, time);
        moonIntensity = glm::clamp(moonIntensity, 0.0f, 1.0f);

        std::ostringstream debugOss;
        debugOss << "DEBUG: sun=" << sunIntensity << " moon=" << moonIntensity;
        s_console->addMessage(debugOss.str(), ConsoleMessageType::INFO);
    } catch (const std::exception& e) {
        s_console->addMessage("Error: Invalid time value", ConsoleMessageType::ERROR);
    }
}

void ConsoleCommands::cmdTimeSpeed(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        // Show current time speed
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Current time speed: " << s_timeSpeed;
        if (s_timeSpeed == 0.0f) {
            oss << " (paused)";
        } else if (s_timeSpeed == 1.0f) {
            oss << " (normal)";
        } else if (s_timeSpeed > 1.0f) {
            oss << " (" << s_timeSpeed << "x faster)";
        } else {
            oss << " (" << (s_timeSpeed * 100.0f) << "% speed)";
        }
        s_console->addMessage(oss.str(), ConsoleMessageType::INFO);
        s_console->addMessage("Usage: timespeed <value> (0=paused, 1=normal, higher=faster)", ConsoleMessageType::INFO);
        return;
    }

    try {
        float speed = std::stof(args[1]);

        // Allow 0 or positive values
        if (speed < 0.0f) {
            s_console->addMessage("Error: Time speed cannot be negative", ConsoleMessageType::ERROR);
            return;
        }

        s_timeSpeed = speed;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Time speed set to: " << speed;
        if (speed == 0.0f) {
            oss << " (paused)";
        } else if (speed == 1.0f) {
            oss << " (normal)";
        } else if (speed > 1.0f) {
            oss << " (" << speed << "x faster)";
        } else {
            oss << " (" << (speed * 100.0f) << "% speed)";
        }
        s_console->addMessage(oss.str(), ConsoleMessageType::INFO);
    } catch (const std::exception& e) {
        s_console->addMessage("Error: Invalid speed value", ConsoleMessageType::ERROR);
    }
}

void ConsoleCommands::updateSkyTime(float deltaTime) {
    if (!s_renderer || s_timeSpeed == 0.0f) {
        return; // Don't update if paused or renderer not available
    }

    // Update sky time (Minecraft-style: 1 full cycle = 20 minutes at speed 1.0)
    // Minecraft: 24000 ticks per day, 20 ticks/second = 1200 seconds = 20 minutes
    // deltaTime is in seconds, so we divide by 1200 (20 minutes * 60 seconds)
    s_currentSkyTime += (deltaTime * s_timeSpeed) / 1200.0f;

    // Wrap around after full cycle
    s_currentSkyTime = std::fmod(s_currentSkyTime, 1.0f);
    if (s_currentSkyTime < 0.0f) {
        s_currentSkyTime += 1.0f;
    }

    // Update renderer
    s_renderer->setSkyTime(s_currentSkyTime);
}

void ConsoleCommands::cmdSpawnStructure(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: spawnstructure <name>", ConsoleMessageType::WARNING);

        // List available structures
        auto& registry = StructureRegistry::instance();
        if (registry.count() > 0) {
            s_console->addMessage("Available structures:", ConsoleMessageType::INFO);
            for (const auto& name : registry.getAllStructureNames()) {
                s_console->addMessage("  " + name, ConsoleMessageType::INFO);
            }
        } else {
            s_console->addMessage("No structures loaded. Check assets/structures/ directory.", ConsoleMessageType::WARNING);
        }
        return;
    }

    if (!s_player) {
        s_console->addMessage("Error: Player not available", ConsoleMessageType::ERROR);
        return;
    }

    if (!s_world) {
        s_console->addMessage("Error: World not available", ConsoleMessageType::ERROR);
        return;
    }

    // Get structure name (rejoin all args after command in case name has spaces)
    std::string structureName = args[1];
    for (size_t i = 2; i < args.size(); i++) {
        structureName += " " + args[i];
    }

    // Perform a long raycast from player's crosshair to find ground
    // We'll cast multiple rays downward if needed
    float maxDistance = 1000.0f;  // Very far away

    // First, cast ray in the direction player is looking
    RaycastHit hit = Raycast::castRay(s_world, s_player->Position, s_player->Front, maxDistance);

    if (!hit.hit) {
        s_console->addMessage("No ground found in crosshair direction", ConsoleMessageType::WARNING);
        return;
    }

    // The hit position is where we found a solid block
    // We want to place the structure on top of this block
    glm::ivec3 groundPos(hit.blockX, hit.blockY, hit.blockZ);

    // Calculate the position on top of the hit block (using the normal)
    glm::ivec3 spawnPos = groundPos + glm::ivec3(hit.normal);

    // However, for structures we want to find the actual ground level
    // Cast a ray downward from the hit point to ensure we're at ground level
    glm::vec3 downDirection(0.0f, -1.0f, 0.0f);
    RaycastHit groundHit = Raycast::castRay(s_world, glm::vec3(spawnPos), downDirection, 256.0f);

    if (groundHit.hit) {
        // Found solid ground below, place structure on top of it
        groundPos = glm::ivec3(groundHit.blockX, groundHit.blockY, groundHit.blockZ);
        spawnPos = groundPos + glm::ivec3(0, 1, 0);  // One block above ground
    }

    // Spawn the structure with renderer for mesh updates
    auto& registry = StructureRegistry::instance();
    bool success = registry.spawnStructure(structureName, s_world, spawnPos, s_renderer);

    if (success) {
        std::ostringstream oss;
        oss << "Spawned structure '" << structureName << "' at ("
            << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << ")";
        s_console->addMessage(oss.str(), ConsoleMessageType::INFO);
    } else {
        s_console->addMessage("Failed to spawn structure '" + structureName + "'", ConsoleMessageType::ERROR);
        s_console->addMessage("Structure may not exist. Use 'spawnstructure' without args to see available structures.", ConsoleMessageType::INFO);
    }
}
