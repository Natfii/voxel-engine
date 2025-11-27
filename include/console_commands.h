#pragma once

#include "console.h"
#include <memory>

// Forward declarations
class Player;
class World;
class VulkanRenderer;
class SkeletalEditor;
class ParticleEditor;

// Register all built-in console commands
class ConsoleCommands {
public:
    static void registerAll(Console* console, Player* player, World* world, VulkanRenderer* renderer);

    // Get/set time speed for game loop integration
    static float getTimeSpeed() { return s_timeSpeed; }
    static float getCurrentSkyTime() { return s_currentSkyTime; }
    static void updateSkyTime(float deltaTime);

    // Editor access
    static SkeletalEditor* getSkeletalEditor() { return s_skeletalEditor.get(); }
    static ParticleEditor* getParticleEditor() { return s_particleEditor.get(); }
    static void updateEditors(float deltaTime);
    static void renderEditors();

private:
    static Console* s_console;
    static Player* s_player;
    static World* s_world;
    static VulkanRenderer* s_renderer;

    // Sky time control
    static float s_timeSpeed;      // Time progression speed (0 = paused, 1 = normal)
    static float s_currentSkyTime; // Current time of day (0-1)

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
    static void cmdSkyTime(const std::vector<std::string>& args);
    static void cmdTimeSpeed(const std::vector<std::string>& args);
    static void cmdSpawnStructure(const std::vector<std::string>& args);
    static void cmdReload(const std::vector<std::string>& args);
    static void cmdApi(const std::vector<std::string>& args);
    static void cmdBrush(const std::vector<std::string>& args);
    static void cmdSpawn(const std::vector<std::string>& args);
    static void cmdEntity(const std::vector<std::string>& args);

    // Editor commands
    static void cmd3DEditor(const std::vector<std::string>& args);
    static void cmdParticleEditor(const std::vector<std::string>& args);

    // Editor instances
    static std::unique_ptr<SkeletalEditor> s_skeletalEditor;
    static std::unique_ptr<ParticleEditor> s_particleEditor;
};
