#include "console_commands.h"
#include "command_registry.h"
#include "convar.h"
#include "debug_state.h"
#include "player.h"
#include "world.h"
#include "vulkan_renderer.h"
#include "structure_system.h"
#include "raycast.h"
#include "perf_monitor.h"
#include "block_system.h"
#include "biome_system.h"
// #include "engine_api.h"      // TODO: Implement EngineAPI
// #include "event_dispatcher.h" // TODO: Implement EventDispatcher
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

    registry.registerCommand("debug", "Toggle debug rendering modes and performance monitoring",
                           "debug <render|drawfps|targetinfo|perf>", [](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            s_console->addMessage("Usage: debug <render|drawfps|targetinfo|perf>", ConsoleMessageType::WARNING);
            return;
        }

        if (args[1] == "render") {
            cmdDebugRender(args);
        } else if (args[1] == "drawfps") {
            cmdDebugDrawFPS(args);
        } else if (args[1] == "targetinfo") {
            cmdDebugTargetInfo(args);
        } else if (args[1] == "perf") {
            auto& monitor = PerformanceMonitor::instance();

            if (args.size() >= 3) {
                // Set report interval: debug perf <interval>
                try {
                    float interval = std::stof(args[2]);
                    if (interval < 1.0f) {
                        s_console->addMessage("Interval must be at least 1 second", ConsoleMessageType::ERROR);
                        return;
                    }
                    monitor.setReportInterval(interval);
                    s_console->addMessage("Performance report interval: " + std::to_string(interval) + " seconds", ConsoleMessageType::INFO);
                } catch (...) {
                    s_console->addMessage("Invalid interval value", ConsoleMessageType::ERROR);
                    return;
                }
            }

            bool newValue = !monitor.isEnabled();
            monitor.setEnabled(newValue);
            s_console->addMessage("Performance monitoring: " + std::string(newValue ? "ON" : "OFF"), ConsoleMessageType::INFO);
            if (newValue) {
                s_console->addMessage("Performance reports will print to console", ConsoleMessageType::INFO);
                s_console->addMessage("Use 'debug perf <seconds>' to change report interval", ConsoleMessageType::INFO);
            }
        } else {
            s_console->addMessage("Unknown debug option: " + args[1], ConsoleMessageType::ERROR);
            s_console->addMessage("Available options: render, drawfps, targetinfo, perf", ConsoleMessageType::INFO);
        }
    }, {"render", "drawfps", "targetinfo", "perf", "perf 5", "perf 10", "perf 30"});

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

    // Get all loaded block names for autocomplete
    std::vector<std::string> blockNames;
    for (int i = 1; i < BlockRegistry::instance().count(); i++) {
        blockNames.push_back(BlockRegistry::instance().get(i).name);
    }

    registry.registerCommand("reload", "Hot-reload assets from disk",
                           "reload <all|blocks|structures|biomes>", cmdReload,
                           {"all", "blocks", "structures", "biomes"});

    registry.registerCommand("api", "Engine API commands for block manipulation",
                           "api <place|fill|sphere|replace> <args>", cmdApi,
                           {"place", "fill", "sphere", "replace"});

    registry.registerCommand("brush", "Terrain brush tools",
                           "brush <raise|lower|smooth|paint|flatten> <args>", cmdBrush,
                           {"raise", "lower", "smooth", "paint", "flatten"});

    registry.registerCommand("spawn", "Spawn entities in the world",
                           "spawn <sphere|cube|cylinder> <args>", cmdSpawn,
                           {"sphere", "cube", "cylinder"});

    registry.registerCommand("entity", "Entity management commands",
                           "entity <list|remove|clear> [args]", cmdEntity,
                           {"list", "remove", "clear"});
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

void ConsoleCommands::cmdReload(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: reload <all|blocks|structures|biomes>", ConsoleMessageType::WARNING);
        return;
    }

    if (!s_world) {
        s_console->addMessage("Error: World not available", ConsoleMessageType::ERROR);
        return;
    }

    if (!s_renderer) {
        s_console->addMessage("Error: Renderer not available", ConsoleMessageType::ERROR);
        return;
    }

    std::string reloadType = args[1];

    if (reloadType == "all" || reloadType == "blocks") {
        s_console->addMessage("Reloading block definitions...", ConsoleMessageType::INFO);

        // Reload block definitions
        if (!BlockRegistry::instance().loadBlocks("assets/blocks", s_renderer)) {
            s_console->addMessage("Error: Failed to reload blocks", ConsoleMessageType::ERROR);
            return;
        }

        // Mark all chunks as needing mesh regeneration
        s_console->addMessage("Regenerating chunk meshes...", ConsoleMessageType::INFO);
        int regeneratedCount = 0;

        s_renderer->beginBufferCopyBatch();
        for (Chunk* chunk : s_world->getChunks()) {
            if (chunk) {
                chunk->generateMesh(s_world);
                chunk->createVertexBufferBatched(s_renderer);
                regeneratedCount++;
            }
        }
        s_renderer->submitBufferCopyBatch();

        s_console->addMessage("Blocks reloaded successfully! Regenerated " + std::to_string(regeneratedCount) + " chunks", ConsoleMessageType::INFO);
    }

    if (reloadType == "all" || reloadType == "structures") {
        s_console->addMessage("Reloading structure definitions...", ConsoleMessageType::INFO);

        if (!StructureRegistry::instance().loadStructures("assets/structures")) {
            s_console->addMessage("Error: Failed to reload structures", ConsoleMessageType::ERROR);
            return;
        }

        s_console->addMessage("Structures reloaded successfully!", ConsoleMessageType::INFO);
    }

    if (reloadType == "all" || reloadType == "biomes") {
        s_console->addMessage("Reloading biome definitions...", ConsoleMessageType::INFO);

        // Clear and reload biomes
        BiomeRegistry::getInstance().clear();
        if (!BiomeRegistry::getInstance().loadBiomes("assets/biomes")) {
            s_console->addMessage("Error: Failed to reload biomes", ConsoleMessageType::ERROR);
            return;
        }

        s_console->addMessage("Biomes reloaded successfully!", ConsoleMessageType::INFO);
        s_console->addMessage("Note: Existing chunks keep their biomes. New chunks will use updated definitions.", ConsoleMessageType::INFO);
    }

    if (reloadType != "all" && reloadType != "blocks" && reloadType != "structures" && reloadType != "biomes") {
        s_console->addMessage("Unknown reload type: " + reloadType, ConsoleMessageType::ERROR);
        s_console->addMessage("Available types: all, blocks, structures, biomes", ConsoleMessageType::INFO);
    }
}

void ConsoleCommands::cmdApi(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: api <place|fill|sphere|replace> <args>", ConsoleMessageType::WARNING);
        s_console->addMessage("  api place <blockName> <x> <y> <z>", ConsoleMessageType::INFO);
        s_console->addMessage("  api fill <blockName> <x1> <y1> <z1> <x2> <y2> <z2>", ConsoleMessageType::INFO);
        s_console->addMessage("  api sphere <blockName> <x> <y> <z> <radius>", ConsoleMessageType::INFO);
        s_console->addMessage("  api replace <fromBlock> <toBlock> <x1> <y1> <z1> <x2> <y2> <z2>", ConsoleMessageType::INFO);
        return;
    }

    if (!s_world) {
        s_console->addMessage("Error: World not available", ConsoleMessageType::ERROR);
        return;
    }

    if (!s_renderer) {
        s_console->addMessage("Error: Renderer not available", ConsoleMessageType::ERROR);
        return;
    }

    std::string subcommand = args[1];

    if (subcommand == "place") {
        if (args.size() < 6) {
            s_console->addMessage("Usage: api place <blockName> <x> <y> <z>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            std::string blockName = args[2];
            int blockID = BlockRegistry::instance().getID(blockName);
            if (blockID == -1) {
                s_console->addMessage("Error: Unknown block '" + blockName + "'", ConsoleMessageType::ERROR);
                return;
            }

            float x = std::stof(args[3]);
            float y = std::stof(args[4]);
            float z = std::stof(args[5]);

            s_world->placeBlock(x, y, z, blockID, s_renderer);
            s_console->addMessage("Placed " + blockName + " at (" + args[3] + ", " + args[4] + ", " + args[5] + ")", ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else if (subcommand == "fill") {
        if (args.size() < 9) {
            s_console->addMessage("Usage: api fill <blockName> <x1> <y1> <z1> <x2> <y2> <z2>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            std::string blockName = args[2];
            int blockID = BlockRegistry::instance().getID(blockName);
            if (blockID == -1) {
                s_console->addMessage("Error: Unknown block '" + blockName + "'", ConsoleMessageType::ERROR);
                return;
            }

            int x1 = std::stoi(args[3]);
            int y1 = std::stoi(args[4]);
            int z1 = std::stoi(args[5]);
            int x2 = std::stoi(args[6]);
            int y2 = std::stoi(args[7]);
            int z2 = std::stoi(args[8]);

            // Ensure min/max ordering
            if (x1 > x2) std::swap(x1, x2);
            if (y1 > y2) std::swap(y1, y2);
            if (z1 > z2) std::swap(z1, z2);

            int blocksPlaced = 0;
            for (int x = x1; x <= x2; x++) {
                for (int y = y1; y <= y2; y++) {
                    for (int z = z1; z <= z2; z++) {
                        s_world->setBlockAt(x, y, z, blockID, false);
                        blocksPlaced++;
                    }
                }
            }

            // Regenerate affected chunks
            s_renderer->beginBufferCopyBatch();
            for (int x = x1; x <= x2; x += 32) {
                for (int y = y1; y <= y2; y += 32) {
                    for (int z = z1; z <= z2; z += 32) {
                        Chunk* chunk = s_world->getChunkAtWorldPos(x, y, z);
                        if (chunk) {
                            chunk->generateMesh(s_world);
                            chunk->createVertexBufferBatched(s_renderer);
                        }
                    }
                }
            }
            s_renderer->submitBufferCopyBatch();

            s_console->addMessage("Filled " + std::to_string(blocksPlaced) + " blocks with " + blockName, ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else if (subcommand == "sphere") {
        if (args.size() < 7) {
            s_console->addMessage("Usage: api sphere <blockName> <x> <y> <z> <radius>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            std::string blockName = args[2];
            int blockID = BlockRegistry::instance().getID(blockName);
            if (blockID == -1) {
                s_console->addMessage("Error: Unknown block '" + blockName + "'", ConsoleMessageType::ERROR);
                return;
            }

            int centerX = std::stoi(args[3]);
            int centerY = std::stoi(args[4]);
            int centerZ = std::stoi(args[5]);
            int radius = std::stoi(args[6]);

            if (radius <= 0) {
                s_console->addMessage("Error: Radius must be positive", ConsoleMessageType::ERROR);
                return;
            }

            int blocksPlaced = 0;
            for (int x = centerX - radius; x <= centerX + radius; x++) {
                for (int y = centerY - radius; y <= centerY + radius; y++) {
                    for (int z = centerZ - radius; z <= centerZ + radius; z++) {
                        // Check if point is inside sphere
                        int dx = x - centerX;
                        int dy = y - centerY;
                        int dz = z - centerZ;
                        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

                        if (distance <= radius) {
                            s_world->setBlockAt(x, y, z, blockID, false);
                            blocksPlaced++;
                        }
                    }
                }
            }

            // Regenerate affected chunks
            s_renderer->beginBufferCopyBatch();
            for (int x = centerX - radius; x <= centerX + radius; x += 32) {
                for (int y = centerY - radius; y <= centerY + radius; y += 32) {
                    for (int z = centerZ - radius; z <= centerZ + radius; z += 32) {
                        Chunk* chunk = s_world->getChunkAtWorldPos(x, y, z);
                        if (chunk) {
                            chunk->generateMesh(s_world);
                            chunk->createVertexBufferBatched(s_renderer);
                        }
                    }
                }
            }
            s_renderer->submitBufferCopyBatch();

            s_console->addMessage("Created sphere with " + std::to_string(blocksPlaced) + " blocks of " + blockName, ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else if (subcommand == "replace") {
        if (args.size() < 10) {
            s_console->addMessage("Usage: api replace <fromBlock> <toBlock> <x1> <y1> <z1> <x2> <y2> <z2>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            std::string fromBlockName = args[2];
            std::string toBlockName = args[3];

            int fromBlockID = BlockRegistry::instance().getID(fromBlockName);
            int toBlockID = BlockRegistry::instance().getID(toBlockName);

            if (fromBlockID == -1) {
                s_console->addMessage("Error: Unknown block '" + fromBlockName + "'", ConsoleMessageType::ERROR);
                return;
            }
            if (toBlockID == -1) {
                s_console->addMessage("Error: Unknown block '" + toBlockName + "'", ConsoleMessageType::ERROR);
                return;
            }

            int x1 = std::stoi(args[4]);
            int y1 = std::stoi(args[5]);
            int z1 = std::stoi(args[6]);
            int x2 = std::stoi(args[7]);
            int y2 = std::stoi(args[8]);
            int z2 = std::stoi(args[9]);

            // Ensure min/max ordering
            if (x1 > x2) std::swap(x1, x2);
            if (y1 > y2) std::swap(y1, y2);
            if (z1 > z2) std::swap(z1, z2);

            int blocksReplaced = 0;
            for (int x = x1; x <= x2; x++) {
                for (int y = y1; y <= y2; y++) {
                    for (int z = z1; z <= z2; z++) {
                        if (s_world->getBlockAt(x, y, z) == fromBlockID) {
                            s_world->setBlockAt(x, y, z, toBlockID, false);
                            blocksReplaced++;
                        }
                    }
                }
            }

            // Regenerate affected chunks
            s_renderer->beginBufferCopyBatch();
            for (int x = x1; x <= x2; x += 32) {
                for (int y = y1; y <= y2; y += 32) {
                    for (int z = z1; z <= z2; z += 32) {
                        Chunk* chunk = s_world->getChunkAtWorldPos(x, y, z);
                        if (chunk) {
                            chunk->generateMesh(s_world);
                            chunk->createVertexBufferBatched(s_renderer);
                        }
                    }
                }
            }
            s_renderer->submitBufferCopyBatch();

            s_console->addMessage("Replaced " + std::to_string(blocksReplaced) + " blocks from " + fromBlockName + " to " + toBlockName, ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else {
        s_console->addMessage("Unknown API command: " + subcommand, ConsoleMessageType::ERROR);
        s_console->addMessage("Available: place, fill, sphere, replace", ConsoleMessageType::INFO);
    }
}

void ConsoleCommands::cmdBrush(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: brush <raise|lower|smooth|paint|flatten> <args>", ConsoleMessageType::WARNING);
        s_console->addMessage("  brush raise <radius> <height>", ConsoleMessageType::INFO);
        s_console->addMessage("  brush lower <radius> <depth>", ConsoleMessageType::INFO);
        s_console->addMessage("  brush smooth <radius>", ConsoleMessageType::INFO);
        s_console->addMessage("  brush paint <blockName> <radius>", ConsoleMessageType::INFO);
        s_console->addMessage("  brush flatten <radius> [targetY]", ConsoleMessageType::INFO);
        return;
    }

    if (!s_world) {
        s_console->addMessage("Error: World not available", ConsoleMessageType::ERROR);
        return;
    }

    if (!s_renderer) {
        s_console->addMessage("Error: Renderer not available", ConsoleMessageType::ERROR);
        return;
    }

    if (!s_player) {
        s_console->addMessage("Error: Player not available", ConsoleMessageType::ERROR);
        return;
    }

    // Perform raycast to find target position
    RaycastHit hit = Raycast::castRay(s_world, s_player->Position, s_player->Front, 100.0f);
    if (!hit.hit) {
        s_console->addMessage("No block targeted. Point at terrain to use brush.", ConsoleMessageType::WARNING);
        return;
    }

    glm::ivec3 targetPos(hit.blockX, hit.blockY, hit.blockZ);
    std::string subcommand = args[1];

    if (subcommand == "raise") {
        if (args.size() < 4) {
            s_console->addMessage("Usage: brush raise <radius> <height>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            int radius = std::stoi(args[2]);
            int height = std::stoi(args[3]);

            if (radius <= 0 || height <= 0) {
                s_console->addMessage("Error: Radius and height must be positive", ConsoleMessageType::ERROR);
                return;
            }

            int blocksPlaced = 0;
            for (int x = targetPos.x - radius; x <= targetPos.x + radius; x++) {
                for (int z = targetPos.z - radius; z <= targetPos.z + radius; z++) {
                    int dx = x - targetPos.x;
                    int dz = z - targetPos.z;
                    float distance = std::sqrt(dx * dx + dz * dz);

                    if (distance <= radius) {
                        // Get surface block at this position
                        int surfaceBlock = s_world->getBlockAt(x, targetPos.y, z);
                        if (surfaceBlock > 0) {
                            // Raise terrain by placing blocks above
                            for (int h = 1; h <= height; h++) {
                                s_world->setBlockAt(x, targetPos.y + h, z, surfaceBlock, false);
                                blocksPlaced++;
                            }
                        }
                    }
                }
            }

            // Regenerate affected chunks
            s_renderer->beginBufferCopyBatch();
            for (int x = targetPos.x - radius - 1; x <= targetPos.x + radius + 1; x += 32) {
                for (int y = targetPos.y - 1; y <= targetPos.y + height + 1; y += 32) {
                    for (int z = targetPos.z - radius - 1; z <= targetPos.z + radius + 1; z += 32) {
                        Chunk* chunk = s_world->getChunkAtWorldPos(x, y, z);
                        if (chunk) {
                            chunk->generateMesh(s_world);
                            chunk->createVertexBufferBatched(s_renderer);
                        }
                    }
                }
            }
            s_renderer->submitBufferCopyBatch();

            s_console->addMessage("Raised terrain: " + std::to_string(blocksPlaced) + " blocks placed", ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else if (subcommand == "lower") {
        if (args.size() < 4) {
            s_console->addMessage("Usage: brush lower <radius> <depth>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            int radius = std::stoi(args[2]);
            int depth = std::stoi(args[3]);

            if (radius <= 0 || depth <= 0) {
                s_console->addMessage("Error: Radius and depth must be positive", ConsoleMessageType::ERROR);
                return;
            }

            int blocksRemoved = 0;
            for (int x = targetPos.x - radius; x <= targetPos.x + radius; x++) {
                for (int z = targetPos.z - radius; z <= targetPos.z + radius; z++) {
                    int dx = x - targetPos.x;
                    int dz = z - targetPos.z;
                    float distance = std::sqrt(dx * dx + dz * dz);

                    if (distance <= radius) {
                        // Remove blocks downward
                        for (int h = 0; h < depth; h++) {
                            if (s_world->getBlockAt(x, targetPos.y - h, z) > 0) {
                                s_world->setBlockAt(x, targetPos.y - h, z, 0, false);
                                blocksRemoved++;
                            }
                        }
                    }
                }
            }

            // Regenerate affected chunks
            s_renderer->beginBufferCopyBatch();
            for (int x = targetPos.x - radius - 1; x <= targetPos.x + radius + 1; x += 32) {
                for (int y = targetPos.y - depth - 1; y <= targetPos.y + 1; y += 32) {
                    for (int z = targetPos.z - radius - 1; z <= targetPos.z + radius + 1; z += 32) {
                        Chunk* chunk = s_world->getChunkAtWorldPos(x, y, z);
                        if (chunk) {
                            chunk->generateMesh(s_world);
                            chunk->createVertexBufferBatched(s_renderer);
                        }
                    }
                }
            }
            s_renderer->submitBufferCopyBatch();

            s_console->addMessage("Lowered terrain: " + std::to_string(blocksRemoved) + " blocks removed", ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else if (subcommand == "smooth") {
        if (args.size() < 3) {
            s_console->addMessage("Usage: brush smooth <radius>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            int radius = std::stoi(args[2]);

            if (radius <= 0) {
                s_console->addMessage("Error: Radius must be positive", ConsoleMessageType::ERROR);
                return;
            }

            s_console->addMessage("Smooth brush not yet implemented", ConsoleMessageType::WARNING);
            s_console->addMessage("TODO: Implement terrain smoothing algorithm", ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else if (subcommand == "paint") {
        if (args.size() < 4) {
            s_console->addMessage("Usage: brush paint <blockName> <radius>", ConsoleMessageType::WARNING);
            return;
        }

        try {
            std::string blockName = args[2];
            int blockID = BlockRegistry::instance().getID(blockName);
            if (blockID == -1) {
                s_console->addMessage("Error: Unknown block '" + blockName + "'", ConsoleMessageType::ERROR);
                return;
            }

            int radius = std::stoi(args[3]);

            if (radius <= 0) {
                s_console->addMessage("Error: Radius must be positive", ConsoleMessageType::ERROR);
                return;
            }

            int blocksPainted = 0;
            for (int x = targetPos.x - radius; x <= targetPos.x + radius; x++) {
                for (int z = targetPos.z - radius; z <= targetPos.z + radius; z++) {
                    int dx = x - targetPos.x;
                    int dz = z - targetPos.z;
                    float distance = std::sqrt(dx * dx + dz * dz);

                    if (distance <= radius) {
                        // Replace surface block
                        if (s_world->getBlockAt(x, targetPos.y, z) > 0) {
                            s_world->setBlockAt(x, targetPos.y, z, blockID, false);
                            blocksPainted++;
                        }
                    }
                }
            }

            // Regenerate affected chunks
            s_renderer->beginBufferCopyBatch();
            for (int x = targetPos.x - radius - 1; x <= targetPos.x + radius + 1; x += 32) {
                for (int z = targetPos.z - radius - 1; z <= targetPos.z + radius + 1; z += 32) {
                    Chunk* chunk = s_world->getChunkAtWorldPos(x, targetPos.y, z);
                    if (chunk) {
                        chunk->generateMesh(s_world);
                        chunk->createVertexBufferBatched(s_renderer);
                    }
                }
            }
            s_renderer->submitBufferCopyBatch();

            s_console->addMessage("Painted " + std::to_string(blocksPainted) + " blocks with " + blockName, ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else if (subcommand == "flatten") {
        if (args.size() < 3) {
            s_console->addMessage("Usage: brush flatten <radius> [targetY]", ConsoleMessageType::WARNING);
            return;
        }

        try {
            int radius = std::stoi(args[2]);
            int targetY = targetPos.y;

            if (args.size() >= 4) {
                targetY = std::stoi(args[3]);
            }

            if (radius <= 0) {
                s_console->addMessage("Error: Radius must be positive", ConsoleMessageType::ERROR);
                return;
            }

            int blocksChanged = 0;
            for (int x = targetPos.x - radius; x <= targetPos.x + radius; x++) {
                for (int z = targetPos.z - radius; z <= targetPos.z + radius; z++) {
                    int dx = x - targetPos.x;
                    int dz = z - targetPos.z;
                    float distance = std::sqrt(dx * dx + dz * dz);

                    if (distance <= radius) {
                        // Get the block type to use
                        int surfaceBlock = s_world->getBlockAt(x, targetPos.y, z);
                        if (surfaceBlock == 0) surfaceBlock = BlockRegistry::instance().getID("dirt");

                        // Fill up to target height or remove above target height
                        for (int y = targetY - 5; y <= targetY + 5; y++) {
                            int currentBlock = s_world->getBlockAt(x, y, z);
                            if (y <= targetY && currentBlock == 0) {
                                s_world->setBlockAt(x, y, z, surfaceBlock, false);
                                blocksChanged++;
                            } else if (y > targetY && currentBlock > 0) {
                                s_world->setBlockAt(x, y, z, 0, false);
                                blocksChanged++;
                            }
                        }
                    }
                }
            }

            // Regenerate affected chunks
            s_renderer->beginBufferCopyBatch();
            for (int x = targetPos.x - radius - 1; x <= targetPos.x + radius + 1; x += 32) {
                for (int y = targetY - 6; y <= targetY + 6; y += 32) {
                    for (int z = targetPos.z - radius - 1; z <= targetPos.z + radius + 1; z += 32) {
                        Chunk* chunk = s_world->getChunkAtWorldPos(x, y, z);
                        if (chunk) {
                            chunk->generateMesh(s_world);
                            chunk->createVertexBufferBatched(s_renderer);
                        }
                    }
                }
            }
            s_renderer->submitBufferCopyBatch();

            s_console->addMessage("Flattened terrain at Y=" + std::to_string(targetY) + ": " + std::to_string(blocksChanged) + " blocks changed", ConsoleMessageType::INFO);
        } catch (const std::exception& e) {
            s_console->addMessage("Error: Invalid arguments", ConsoleMessageType::ERROR);
        }
    }
    else {
        s_console->addMessage("Unknown brush command: " + subcommand, ConsoleMessageType::ERROR);
        s_console->addMessage("Available: raise, lower, smooth, paint, flatten", ConsoleMessageType::INFO);
    }
}

void ConsoleCommands::cmdSpawn(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: spawn <sphere|cube|cylinder> <args>", ConsoleMessageType::WARNING);
        s_console->addMessage("  spawn sphere <radius> [r] [g] [b]", ConsoleMessageType::INFO);
        s_console->addMessage("  spawn cube <size> [r] [g] [b]", ConsoleMessageType::INFO);
        s_console->addMessage("  spawn cylinder <radius> <height> [r] [g] [b]", ConsoleMessageType::INFO);
        return;
    }

    if (!s_world) {
        s_console->addMessage("Error: World not available", ConsoleMessageType::ERROR);
        return;
    }

    if (!s_player) {
        s_console->addMessage("Error: Player not available", ConsoleMessageType::ERROR);
        return;
    }

    // Perform raycast to find spawn position
    RaycastHit hit = Raycast::castRay(s_world, s_player->Position, s_player->Front, 100.0f);
    if (!hit.hit) {
        s_console->addMessage("No spawn location found. Point at terrain to spawn entity.", ConsoleMessageType::WARNING);
        return;
    }

    glm::vec3 spawnPos = glm::vec3(hit.blockX, hit.blockY, hit.blockZ) + glm::vec3(hit.normal);
    std::string subcommand = args[1];

    s_console->addMessage("Entity spawning not yet implemented", ConsoleMessageType::WARNING);
    s_console->addMessage("TODO: Implement entity system with physics and rendering", ConsoleMessageType::INFO);
    s_console->addMessage("Spawn position would be: (" + std::to_string((int)spawnPos.x) + ", " +
                         std::to_string((int)spawnPos.y) + ", " + std::to_string((int)spawnPos.z) + ")", ConsoleMessageType::INFO);
}

void ConsoleCommands::cmdEntity(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        s_console->addMessage("Usage: entity <list|remove|clear> [args]", ConsoleMessageType::WARNING);
        s_console->addMessage("  entity list - List all spawned entities", ConsoleMessageType::INFO);
        s_console->addMessage("  entity remove <id> - Remove entity by ID", ConsoleMessageType::INFO);
        s_console->addMessage("  entity clear - Remove all entities", ConsoleMessageType::INFO);
        return;
    }

    std::string subcommand = args[1];

    if (subcommand == "list") {
        s_console->addMessage("Entity list not yet implemented", ConsoleMessageType::WARNING);
        s_console->addMessage("TODO: Implement entity system", ConsoleMessageType::INFO);
    }
    else if (subcommand == "remove") {
        if (args.size() < 3) {
            s_console->addMessage("Usage: entity remove <id>", ConsoleMessageType::WARNING);
            return;
        }
        s_console->addMessage("Entity removal not yet implemented", ConsoleMessageType::WARNING);
        s_console->addMessage("TODO: Implement entity system", ConsoleMessageType::INFO);
    }
    else if (subcommand == "clear") {
        s_console->addMessage("Entity clearing not yet implemented", ConsoleMessageType::WARNING);
        s_console->addMessage("TODO: Implement entity system", ConsoleMessageType::INFO);
    }
    else {
        s_console->addMessage("Unknown entity command: " + subcommand, ConsoleMessageType::ERROR);
        s_console->addMessage("Available: list, remove, clear", ConsoleMessageType::INFO);
    }
}
