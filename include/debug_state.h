#pragma once

#include "convar.h"

// Global debug state accessible from anywhere
class DebugState {
public:
    static DebugState& instance();

    // Debug rendering flags
    ConVar<bool> renderDebug;
    ConVar<bool> drawFPS;
    ConVar<bool> showTargetInfo;
    ConVar<bool> showCullingStats;
    ConVar<bool> noclip;

    // FPS tracking
    float lastFPS = 0.0f;
    float fpsUpdateTimer = 0.0f;
    int frameCount = 0;

    // Chunk rendering statistics
    int chunksRendered = 0;
    int chunksDistanceCulled = 0;
    int chunksFrustumCulled = 0;
    int chunksTotalInWorld = 0;

    void updateFPS(float deltaTime);

private:
    DebugState();
    DebugState(const DebugState&) = delete;
    DebugState& operator=(const DebugState&) = delete;
};
