#pragma once

#include "console.h"

// Forward declarations
class Player;
class World;

// Register all built-in console commands
class ConsoleCommands {
public:
    static void registerAll(Console* console, Player* player, World* world);

private:
    static Console* s_console;
    static Player* s_player;
    static World* s_world;

    // Built-in commands
    static void cmdHelp(const std::vector<std::string>& args);
    static void cmdClear(const std::vector<std::string>& args);
    static void cmdNoclip(const std::vector<std::string>& args);
    static void cmdDebugRender(const std::vector<std::string>& args);
    static void cmdDebugDrawFPS(const std::vector<std::string>& args);
    static void cmdDebugTargetInfo(const std::vector<std::string>& args);
    static void cmdTeleport(const std::vector<std::string>& args);
    static void cmdEcho(const std::vector<std::string>& args);
    static void cmdSet(const std::vector<std::string>& args);
    static void cmdGet(const std::vector<std::string>& args);
    static void cmdListConVars(const std::vector<std::string>& args);
};
